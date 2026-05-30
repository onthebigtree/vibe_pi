#include "power_manager.h"
#include "config.h"
#include "settings_manager.h"
#include "../ui/ui_manager.h"
#include "watchdog.h"
#include "hal/board.h"
#include <Wire.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include "../display/display.h"
#include "../../drivers/power/axp2101.h"   // g_axp for VBUS detection

// Defined in serial_config.cpp; declared here to avoid include coupling.
extern bool serial_transport_is_active();

// On external power (USB host attached or VBUS present) a desktop puck should
// never deep-sleep: it must resume instantly on a glance and keep its host link
// alive. Deep sleep is reserved for true battery operation.
static bool on_external_power() {
    return serial_transport_is_active() || g_axp.vbus_present();
}

static PowerState currentState = PowerState::ACTIVE;
static unsigned long lastActivity = 0;
static uint8_t currentBrightness = DEFAULT_BRIGHTNESS;
static bool imuAvailable = false;

// DIM brightness scales with the user's configured ACTIVE brightness, so a user
// who prefers a bright screen isn't slammed down to a fixed 20/255 dim level.
static uint8_t dim_brightness() {
    uint8_t active = settings_get().brightness;
    uint8_t dim = (uint8_t)((uint16_t)active * 25 / 100);
    return dim < 5 ? 5 : dim;
}

// MAX_MODEM sleeps the WiFi radio between DTIM beacons when the screen is off —
// a large idle-power saving. Keepalive pings still get through. MIN_MODEM is the
// balanced mode used while active. No-ops harmlessly if WiFi isn't started.
static void wifi_set_power_save(bool save) {
    esp_wifi_set_ps(save ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
}

#define QMI8658_ADDR    0x6B
#define QMI8658_WHO_AM_I 0x00
#define QMI8658_CTRL1    0x02
#define QMI8658_CTRL2    0x03
#define QMI8658_CTRL7    0x08
#define QMI8658_AX_L     0x35

static bool qmi8658_init() {
    if (!board_get_info().caps.has_imu) return false;

    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(QMI8658_WHO_AM_I);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    uint8_t id = Wire.read();
    if (id != 0x05) { Serial.printf("[IMU] Unknown ID: 0x%02X\n", id); return false; }

    // Enable accelerometer: 4G range, 125Hz ODR
    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(QMI8658_CTRL2);
    Wire.write(0x25); // aODR=125Hz, aFS=4G
    Wire.endTransmission();

    // Enable accel in CTRL7
    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(QMI8658_CTRL7);
    Wire.write(0x01); // aEN=1
    Wire.endTransmission();

    Serial.println("[IMU] QMI8658 initialized");
    return true;
}

static float qmi8658_read_accel_magnitude() {
    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(QMI8658_AX_L);
    if (Wire.endTransmission(false) != 0) return 0;
    Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)6);
    if (Wire.available() < 6) return 0;
    int16_t ax = Wire.read() | (Wire.read() << 8);
    int16_t ay = Wire.read() | (Wire.read() << 8);
    int16_t az = Wire.read() | (Wire.read() << 8);
    float gx = ax / 8192.0f, gy = ay / 8192.0f, gz = az / 8192.0f;
    return sqrtf(gx*gx + gy*gy + gz*gz);
}

void power_init() {
    lastActivity = millis();
    DeviceSettings &s = settings_get();
    currentBrightness = s.brightness;
    display_set_brightness(currentBrightness);
    imuAvailable = qmi8658_init();
}

void power_loop() {
    unsigned long now = millis();
    unsigned long idle = now - lastActivity;
    DeviceSettings &s = settings_get();

    switch (currentState) {
        case PowerState::ACTIVE:
            if (idle > s.sleep_timeout_ms) {
                currentState = PowerState::SCREEN_OFF;
                display_set_brightness(0);
                wifi_set_power_save(true);
                ui_sleep();
                Serial.println("[Power] → SCREEN_OFF");
            } else if (idle > DIM_TIMEOUT_MS) {
                currentState = PowerState::DIM;
                display_set_brightness(dim_brightness());
                Serial.println("[Power] → DIM");
            }
            break;

        case PowerState::DIM:
            if (idle > s.sleep_timeout_ms) {
                currentState = PowerState::SCREEN_OFF;
                display_set_brightness(0);
                wifi_set_power_save(true);
                ui_sleep();
                Serial.println("[Power] → SCREEN_OFF");
            }
            break;

        case PowerState::SCREEN_OFF:
            if (idle > DEEP_SLEEP_TIMEOUT_MS && !on_external_power()) {
                Serial.println("[Power] → DEEP_SLEEP (button to wake)");
                Serial.flush();
                display_set_brightness(0);
                // Wake on button GPIO0 (LOW = pressed). EXT0 also wakes from RTC pin.
                esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
                // Also wake every 5 min to check WiFi/host status briefly
                esp_sleep_enable_timer_wakeup(5ULL * 60 * 1000 * 1000);
                esp_deep_sleep_start();  // never returns; chip resets on wake
            }
            // Throttle the wrist-raise check to ~20Hz. Polling the QMI8658 over
            // I2C every loop (~200Hz) needlessly saturates the bus it shares
            // with the touch + battery ICs while the screen is off.
            {
                static unsigned long lastImuCheck = 0;
                if (now - lastImuCheck >= 50) {
                    lastImuCheck = now;
                    if (power_check_imu_wake()) power_register_activity();
                }
            }
            break;

        case PowerState::DEEP_SLEEP:
            // Should never reach here — esp_deep_sleep_start() doesn't return
            break;
    }

    // Memory pressure check — log warning when heap is low
    static unsigned long lastMemWarn = 0;
    if (mem_is_low() && millis() - lastMemWarn > 10000) {
        lastMemWarn = millis();
        mem_log_stats();
        Serial.println("[Power] WARNING: Low memory");
    }
}

void power_register_activity() {
    lastActivity = millis();

    if (currentState != PowerState::ACTIVE) {
        PowerState prev = currentState;
        currentState = PowerState::ACTIVE;
        currentBrightness = settings_get().brightness;
        display_set_brightness(currentBrightness);
        if (prev == PowerState::SCREEN_OFF || prev == PowerState::DEEP_SLEEP) {
            wifi_set_power_save(false);  // back to full radio perf when active
            ui_wake();
        }
        Serial.println("[Power] → ACTIVE (wake)");
    }
}

PowerState power_get_state() { return currentState; }

void power_force_state(PowerState state) {
    currentState = state;
    switch (state) {
        case PowerState::ACTIVE:
            display_set_brightness(settings_get().brightness);
            wifi_set_power_save(false);
            ui_wake();
            break;
        case PowerState::DIM:
            display_set_brightness(dim_brightness());
            break;
        case PowerState::SCREEN_OFF:
        case PowerState::DEEP_SLEEP:
            display_set_brightness(0);
            wifi_set_power_save(true);
            ui_sleep();
            break;
    }
}

void power_set_brightness(uint8_t level) {
    currentBrightness = level;
    display_set_brightness(level);
}

uint8_t power_get_brightness() { return currentBrightness; }

bool power_check_imu_wake() {
    if (!imuAvailable) return false;
    static float lastMag = 1.0f;
    float mag = qmi8658_read_accel_magnitude();
    float delta = fabsf(mag - lastMag);
    lastMag = mag;
    return delta > 0.3f; // significant motion threshold
}

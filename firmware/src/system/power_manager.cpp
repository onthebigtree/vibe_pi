#include "power_manager.h"
#include "config.h"
#include "settings_manager.h"
#include "../ui/ui_manager.h"
#include "hal/board.h"
#include <Wire.h>
#include <esp_wifi.h>

static PowerState currentState = PowerState::ACTIVE;
static unsigned long lastActivity = 0;
static uint8_t currentBrightness = DEFAULT_BRIGHTNESS;
static bool imuAvailable = false;

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
                ui_sleep();
                Serial.println("[Power] → SCREEN_OFF");
            } else if (idle > DIM_TIMEOUT_MS) {
                currentState = PowerState::DIM;
                display_set_brightness(DIM_BRIGHTNESS);
                Serial.println("[Power] → DIM");
            }
            break;

        case PowerState::DIM:
            if (idle > s.sleep_timeout_ms) {
                currentState = PowerState::SCREEN_OFF;
                display_set_brightness(0);
                ui_sleep();
                Serial.println("[Power] → SCREEN_OFF");
            }
            break;

        case PowerState::SCREEN_OFF:
            if (idle > DEEP_SLEEP_TIMEOUT_MS) {
                currentState = PowerState::DEEP_SLEEP;
                Serial.println("[Power] → DEEP_SLEEP");
                // Keep WiFi alive for reconnect, but reduce CPU
                esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
            }
            if (power_check_imu_wake()) {
                power_register_activity();
            }
            break;

        case PowerState::DEEP_SLEEP:
            if (power_check_imu_wake()) {
                power_register_activity();
            }
            break;
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
            ui_wake();
            break;
        case PowerState::DIM:
            display_set_brightness(DIM_BRIGHTNESS);
            break;
        case PowerState::SCREEN_OFF:
        case PowerState::DEEP_SLEEP:
            display_set_brightness(0);
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

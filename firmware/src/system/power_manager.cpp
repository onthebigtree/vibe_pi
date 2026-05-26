#include "power_manager.h"
#include "config.h"
#include "settings_manager.h"
#include "../ui/ui_manager.h"

static PowerState currentState = PowerState::ACTIVE;
static unsigned long lastActivity = 0;
static uint8_t currentBrightness = DEFAULT_BRIGHTNESS;

void power_init() {
    lastActivity = millis();
    DeviceSettings &s = settings_get();
    currentBrightness = s.brightness;
    display_set_brightness(currentBrightness);

    // TODO: Init QMI8658 IMU for wake-on-motion
    // Wire.begin(TOUCH_SDA, TOUCH_SCL);
    // qmi8658_init_wake_on_motion();
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
                // TODO: esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
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
    // TODO: Read QMI8658 interrupt or acceleration threshold
    // return qmi8658_motion_detected();
    return false;
}

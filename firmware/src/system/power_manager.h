#pragma once

#include <Arduino.h>

enum class PowerState : uint8_t {
    ACTIVE,      // full brightness, normal operation
    DIM,         // reduced brightness after inactivity
    SCREEN_OFF,  // display off, WiFi on, processing continues
    DEEP_SLEEP,  // minimal power, only wake sources active
};

void        power_init();
void        power_loop();
void        power_register_activity();
PowerState  power_get_state();
void        power_force_state(PowerState state);
void        power_set_brightness(uint8_t level);
uint8_t     power_get_brightness();
bool        power_check_imu_wake();

#pragma once

#include <Arduino.h>

enum class ResetLevel : uint8_t {
    SOFT_RESTART  = 0,  // just reboot
    DISPLAY_RESET = 1,  // brightness, theme, sleep → defaults
    NETWORK_RESET = 2,  // WiFi, host, pairing → cleared
    FACTORY_RESET = 3,  // everything → cleared, back to OOBE
};

void reset_execute(ResetLevel level, bool reboot = true);
const char *reset_level_name(ResetLevel level);

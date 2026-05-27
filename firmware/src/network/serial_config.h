#pragma once

#include <Arduino.h>

void serial_config_init();
void serial_config_loop();

// Returns true when WiFi credentials have been received via serial
bool serial_config_has_wifi();
const char *serial_config_get_ssid();
const char *serial_config_get_pass();
void serial_config_clear();

#pragma once

#include <Arduino.h>

void serial_config_init();
void serial_config_loop();

bool serial_config_has_wifi();
const char *serial_config_get_ssid();
const char *serial_config_get_pass();
bool serial_config_is_setup_done();
void serial_config_clear();

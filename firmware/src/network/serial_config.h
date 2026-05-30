#pragma once

#include <Arduino.h>

void serial_config_init();
void serial_config_loop();

bool serial_config_has_wifi();
const char *serial_config_get_ssid();
const char *serial_config_get_pass();
bool serial_config_is_setup_done();
void serial_config_clear();

// Serial transport (protocol v2 over USB)
void serial_transport_send_register();
void serial_transport_send(const char *json);
bool serial_transport_is_active();
unsigned long serial_transport_last_status();
// Drop the serial-transport latch (e.g. USB host disappeared) so the state
// machine can fall back to WiFi/WS or show a disconnected screen.
void serial_transport_reset();

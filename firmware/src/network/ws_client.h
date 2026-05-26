#pragma once

#include <ArduinoJson.h>

void ws_client_init(const char *host, uint16_t port);
void ws_client_loop();
bool ws_client_is_connected();
void ws_client_reconnect();
void ws_client_send_register();
void ws_client_send_ping();
void ws_client_send_settings(JsonDocument &doc);
unsigned long ws_client_last_status_time();

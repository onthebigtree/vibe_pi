#pragma once

#include <ArduinoJson.h>

void ws_client_init(const char *host, uint16_t port);
void ws_client_loop();
bool ws_client_is_connected();
void ws_client_reconnect();
void ws_client_send_register();
void ws_client_send_ping();
void ws_client_send_pair_request(const char *deviceId, const char *pairCode, const char *deviceName);
void ws_client_send_settings(JsonDocument &doc);
void ws_client_send_json(JsonDocument &doc);
void ws_client_send_ota_progress(const char *version, uint8_t pct, uint32_t bytesReceived);
void ws_client_send_ota_done(const char *version, bool success, bool sha256Ok);
void ws_client_send_ota_failed(const char *version, const char *error);
unsigned long ws_client_last_status_time();

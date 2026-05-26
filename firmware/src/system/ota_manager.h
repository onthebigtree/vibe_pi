#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class OtaState : uint8_t {
    IDLE,
    AVAILABLE,
    DOWNLOADING,
    VERIFYING,
    INSTALLING,
    SUCCESS,
    FAILED,
};

struct OtaInfo {
    char     version[16];
    char     current_version[16];
    uint32_t size_bytes;
    char     sha256[65];
    char     changelog[128];
    char     changelog_zh[128];
    char     url[256];
    bool     force;
};

void        ota_init();
void        ota_on_available(const JsonObject &payload);
void        ota_on_start(const JsonObject &payload);
bool        ota_start_download();
void        ota_loop();
OtaState    ota_get_state();
OtaInfo    &ota_get_info();
uint8_t     ota_get_progress();
const char *ota_get_error();
void        ota_reset();

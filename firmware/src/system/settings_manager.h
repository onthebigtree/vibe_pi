#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "i18n.h"

struct DeviceSettings {
    // Display
    uint8_t  brightness       = 80;
    uint32_t sleep_timeout_ms = 60000;
    char     theme[16]        = "minimal";

    // Network (managed by provision, but stored here)
    char     wifi_ssid[64]    = "";
    char     wifi_pass[64]    = "";
    char     host_addr[64]    = "";
    uint16_t host_port        = 8765;

    // Pairing
    bool     paired           = false;
    char     pair_token[65]   = "";
    char     host_name[32]    = "";

    // System
    Lang     language         = Lang::ZH;
    char     device_name[32]  = "Vibe Pi";
    char     timezone[32]     = "Asia/Shanghai";
    char     ota_channel[16]  = "stable";

    // Notifications
    uint8_t  alert_usage_pct  = 80;
    bool     alert_disconnect = true;

    // State
    bool     oobe_done        = false;
    uint32_t boot_count       = 0;
};

void     settings_init();
void     settings_load();
void     settings_save();
void     settings_save_wifi(const char *ssid, const char *pass);
void     settings_save_pairing(bool paired, const char *token, const char *host_name,
                               const char *host_addr, uint16_t host_port);
void     settings_mark_oobe_done();

DeviceSettings &settings_get();

// Reset levels
void     settings_reset_display();   // L1: brightness, theme, sleep
void     settings_reset_network();   // L2: wifi, host, pairing
void     settings_reset_factory();   // L3: everything

// Export/Import
void     settings_export_json(JsonDocument &doc);
bool     settings_import_json(JsonDocument &doc);

// Apply remote sync
bool     settings_apply_sync(const JsonObject &payload);

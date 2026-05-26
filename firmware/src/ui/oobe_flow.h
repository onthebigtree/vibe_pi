#pragma once

#include <lvgl.h>

enum class OobeStep : uint8_t {
    LANGUAGE,
    WIFI_SCAN,
    WIFI_PASSWORD,
    WIFI_CONNECTING,
    PAIRING,
    SYNCING,
    COMPLETE,
    DONE,
};

void      oobe_init();
void      oobe_show();
void      oobe_loop();
OobeStep  oobe_get_step();
bool      oobe_is_finished();

// Callbacks from network layer
void      oobe_on_wifi_scan_done(int count);
void      oobe_on_wifi_connected(const char *ip);
void      oobe_on_wifi_failed();
void      oobe_on_pair_confirmed();
void      oobe_on_pair_rejected();
void      oobe_on_pair_timeout();
void      oobe_on_sync_done();

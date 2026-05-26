#pragma once

#include <ArduinoJson.h>
#include <lvgl.h>

// ── Lifecycle ──
void ui_init();

// ── Screen transitions ──
void ui_show_boot();
void ui_show_provision(const char *ap_ssid, const char *ap_ip);
void ui_show_connecting_wifi();
void ui_show_wifi_failed();
void ui_show_discovering();
void ui_show_reconnecting();
void ui_show_dashboard();

// ── Data updates ──
void ui_update_status(JsonObject &payload);
void ui_set_page(int page_index);
int  ui_get_current_page();
int  ui_get_page_count();

// ── Display control ──
void display_set_brightness(uint8_t level);
void ui_sleep();
void ui_wake();
bool ui_is_sleeping();

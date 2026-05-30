#pragma once

#include <ArduinoJson.h>
#include <lvgl.h>

// ── Lifecycle ──
void ui_init();

// ── Screen transitions ──
void ui_show_boot();
void ui_show_connecting_wifi();
void ui_show_wifi_failed();
void ui_show_discovering();
void ui_show_reconnecting();
void ui_show_dashboard();
void ui_show_pairing(const char *code);
void ui_show_safe_mode();

// ── Utility screens (long-press dashboard → settings; settings → diag/OTA;
// long-press any utility screen → back to dashboard, so the user is never trapped) ──
void ui_open_settings();
void ui_open_diagnostics();
void ui_open_ota();
void ui_open_dashboard();

// ── Data updates ──
void ui_update_status(JsonObject &payload);
void ui_set_page(int page_index);
int  ui_get_current_page();
int  ui_get_page_count();
void ui_cycle_tool();
void ui_update_battery();

// ── Display control ──
void display_set_brightness(uint8_t level);
void ui_sleep();
void ui_wake();
bool ui_is_sleeping();

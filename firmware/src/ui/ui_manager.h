#pragma once

#include <ArduinoJson.h>
#include <lvgl.h>

void ui_show_splash();
void ui_show_connecting();
void ui_show_wifi_error();
void ui_show_dashboard();
void ui_update_data(const JsonDocument &doc);

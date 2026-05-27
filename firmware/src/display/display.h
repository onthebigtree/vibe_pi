#pragma once

#include <lvgl.h>

void display_init();
void display_set_brightness(uint8_t level);
void display_update_touch();
uint32_t display_get_touch_cb_count();

#pragma once

#include <lvgl.h>

enum class NotifType : uint8_t {
    INFO,
    WARNING,
    ERROR,
    SUCCESS,
};

void notif_init();
void notif_show(const char *message, NotifType type = NotifType::INFO, uint32_t duration_ms = 3000);
void notif_dismiss();
void notif_loop();

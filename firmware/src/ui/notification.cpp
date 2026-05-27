#include "notification.h"
#include "theme.h"
#include "hal/board.h"
#include <Arduino.h>

static lv_obj_t *notif_bar = nullptr;
static lv_obj_t *notif_label = nullptr;
static unsigned long notif_show_time = 0;
static uint32_t notif_duration = 0;
static bool notif_visible = false;

void notif_init() {}

void notif_show(const char *message, NotifType type, uint32_t duration_ms) {
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;

    notif_dismiss();

    notif_bar = lv_obj_create(scr);
    lv_obj_set_size(notif_bar, pct_w(80), pct_h(10));
    lv_obj_set_style_radius(notif_bar, 12, 0);
    lv_obj_set_style_border_width(notif_bar, 0, 0);
    lv_obj_set_style_pad_all(notif_bar, 8, 0);
    lv_obj_set_scrollbar_mode(notif_bar, LV_SCROLLBAR_MODE_OFF);

    lv_color_t bg;
    switch (type) {
        case NotifType::SUCCESS: bg = lv_color_hex(0x1A3A1A); break;
        case NotifType::WARNING: bg = lv_color_hex(0x3A2A0A); break;
        case NotifType::ERROR:   bg = lv_color_hex(0x3A1A1A); break;
        default:                 bg = lv_color_hex(0x1A2A3A); break;
    }
    lv_obj_set_style_bg_color(notif_bar, bg, 0);
    lv_obj_set_style_bg_opa(notif_bar, LV_OPA_90, 0);

    // Slide in from top
    lv_obj_align(notif_bar, LV_ALIGN_TOP_MID, 0, scr_round() ? pct_h(12) : SP_M);

    notif_label = lv_label_create(notif_bar);
    lv_label_set_text(notif_label, message);
    lv_obj_set_style_text_font(notif_label, FONT_SMALL, 0);

    lv_color_t text_clr;
    switch (type) {
        case NotifType::SUCCESS: text_clr = CLR_SUCCESS; break;
        case NotifType::WARNING: text_clr = CLR_WARNING; break;
        case NotifType::ERROR:   text_clr = CLR_ERROR;   break;
        default:                 text_clr = CLR_ACCENT;  break;
    }
    lv_obj_set_style_text_color(notif_label, text_clr, 0);
    lv_obj_center(notif_label);

    // Animate in
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, notif_bar);
    lv_anim_set_values(&a, -50, scr_round() ? pct_h(12) : SP_M);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_y((lv_obj_t *)obj, v);
    });
    lv_anim_start(&a);

    notif_show_time = millis();
    notif_duration = duration_ms;
    notif_visible = true;
}

void notif_dismiss() {
    if (notif_bar) {
        lv_obj_delete(notif_bar);
        notif_bar = nullptr;
        notif_label = nullptr;
    }
    notif_visible = false;
}

void notif_loop() {
    if (notif_visible && notif_duration > 0) {
        if (millis() - notif_show_time > notif_duration) {
            notif_dismiss();
        }
    }
}

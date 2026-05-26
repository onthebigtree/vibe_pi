#include "diagnostic_page.h"
#include "theme.h"
#include "../system/i18n.h"
#include "../system/health_manager.h"
#include "../system/settings_manager.h"
#include "config.h"
#include <WiFi.h>

static lv_obj_t *page = nullptr;
static lv_obj_t *lbl_heap = nullptr;
static lv_obj_t *lbl_uptime = nullptr;
static lv_obj_t *lbl_wifi = nullptr;
static lv_obj_t *lbl_temp = nullptr;
static lv_obj_t *lbl_crash = nullptr;
static lv_obj_t *lbl_boot = nullptr;
static lv_obj_t *lbl_safe = nullptr;

static lv_obj_t *create_diag_row(lv_obj_t *list, const char *label) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_size(row, 360, 34);
    lv_obj_set_style_bg_color(row, CLR_SURFACE_ALT, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, label);
    lv_obj_set_style_text_color(name, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(name, FONT_SMALL, 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, "—");
    lv_obj_set_style_text_color(val, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(val, FONT_SMALL, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

    return val;
}

lv_obj_t *diagnostic_page_create(lv_obj_t *parent) {
    page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, i18n(S_DIAGNOSTICS));
    lv_obj_set_style_text_color(title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 65);

    lv_obj_t *list = lv_obj_create(page);
    lv_obj_set_size(list, 380, 300);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 15);

    lbl_uptime = create_diag_row(list, "Uptime");
    lbl_heap   = create_diag_row(list, "Free Heap");
    lbl_wifi   = create_diag_row(list, "WiFi RSSI");
    lbl_temp   = create_diag_row(list, "Temperature");
    lbl_crash  = create_diag_row(list, "Crash Count");
    lbl_boot   = create_diag_row(list, "Boot Count");
    lbl_safe   = create_diag_row(list, "Safe Mode");

    diagnostic_page_refresh();
    return page;
}

void diagnostic_page_refresh() {
    if (!page) return;

    HealthData d = health_get_data();
    DeviceSettings &s = settings_get();

    uint32_t sec = d.uptime_sec;
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    lv_label_set_text_fmt(lbl_uptime, "%luh %lum", h, m);

    lv_label_set_text_fmt(lbl_heap, "%lu KB", d.free_heap / 1024);
    lv_label_set_text_fmt(lbl_wifi, "%d dBm", d.wifi_rssi);
    lv_label_set_text_fmt(lbl_temp, "%.1f °C", d.temperature_c);
    lv_label_set_text_fmt(lbl_crash, "%lu", d.crash_count);
    lv_label_set_text_fmt(lbl_boot, "%lu", s.boot_count);

    lv_label_set_text(lbl_safe, d.safe_mode ? "YES" : "NO");
    lv_obj_set_style_text_color(lbl_safe, d.safe_mode ? CLR_ERROR : CLR_SUCCESS, 0);
}

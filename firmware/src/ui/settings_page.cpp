#include "settings_page.h"
#include "theme.h"
#include "ui_manager.h"
#include "../system/i18n.h"
#include "../system/settings_manager.h"
#include "../system/reset_manager.h"
#include "../system/power_manager.h"
#include "config.h"

static lv_obj_t *page = nullptr;
static lv_obj_t *lbl_brightness_val = nullptr;
static lv_obj_t *lbl_lang_val = nullptr;
static lv_obj_t *lbl_device_name_val = nullptr;

static lv_obj_t *create_section(lv_obj_t *list, const char *title) {
    lv_obj_t *lbl = lv_label_create(list);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
    lv_obj_set_style_pad_top(lbl, 12, 0);
    return lbl;
}

static lv_obj_t *create_item(lv_obj_t *list, const char *label, const char *value,
                             lv_event_cb_t cb = nullptr, void *user_data = nullptr) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_size(row, 360, 44);
    lv_obj_set_style_bg_color(row, CLR_SURFACE_ALT, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, label);
    lv_obj_set_style_text_color(name, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, FONT_SMALL, 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_color(val, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(val, FONT_SMALL, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

    return val;
}

// Single tappable row → cycle brightness 20→40→…→100→20 (wraps, so it can be
// turned back down — the old +10-only handler got stuck at 100).
static void on_brightness_cycle(lv_event_t *e) {
    DeviceSettings &s = settings_get();
    int b = (int)s.brightness + 20;
    if (b > 100) b = 20;
    s.brightness = b;
    power_set_brightness(s.brightness);
    settings_save();
    lv_label_set_text_fmt(lbl_brightness_val, "%d%%", s.brightness);
}

static void on_toggle_lang(lv_event_t *e) {
    DeviceSettings &s = settings_get();
    s.language = (s.language == Lang::ZH) ? Lang::EN : Lang::ZH;
    i18n_set_lang(s.language);
    settings_save();
    lv_label_set_text(lbl_lang_val, (s.language == Lang::ZH) ? "中文" : "English");
}

static void on_reset(lv_event_t *e) {
    ResetLevel level = (ResetLevel)(intptr_t)lv_event_get_user_data(e);
    reset_execute(level, true);
}

static void on_open_diag(lv_event_t *e) { ui_open_diagnostics(); }
static void on_open_ota(lv_event_t *e)  { ui_open_ota(); }

lv_obj_t *settings_page_create(lv_obj_t *parent) {
    page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, i18n(S_SETTINGS));
    lv_obj_set_style_text_color(title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 65);

    // Scrollable list
    lv_obj_t *list = lv_obj_create(page);
    lv_obj_set_size(list, 380, 320);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 20);

    DeviceSettings &s = settings_get();

    // Display section
    create_section(list, i18n(S_DISPLAY));
    lbl_brightness_val = create_item(list, i18n(S_BRIGHTNESS), "",
                                     on_brightness_cycle, nullptr);
    lv_label_set_text_fmt(lbl_brightness_val, "%d%%", s.brightness);

    // System section
    create_section(list, i18n(S_SYSTEM_SETTINGS));
    lbl_lang_val = create_item(list, i18n(S_LANGUAGE),
                               (s.language == Lang::ZH) ? "中文" : "English",
                               on_toggle_lang, nullptr);
    lbl_device_name_val = create_item(list, i18n(S_DEVICE_NAME), s.device_name);

    // System tools: diagnostics + firmware update (tap → those screens)
    create_section(list, i18n(S_SYSTEM));
    create_item(list, i18n(S_DIAGNOSTICS), ">", on_open_diag, nullptr);
    create_item(list, i18n(S_FIRMWARE_UPDATE), ">", on_open_ota, nullptr);

    // About section
    create_section(list, i18n(S_ABOUT));
    create_item(list, i18n(S_FIRMWARE_VER), FW_VERSION);
    create_item(list, i18n(S_HARDWARE_INFO), DEVICE_HARDWARE);

    // Reset section
    create_section(list, i18n(S_RESET));
    create_item(list, i18n(S_SOFT_RESTART), ">",
                on_reset, (void *)(intptr_t)ResetLevel::SOFT_RESTART);
    create_item(list, i18n(S_DISPLAY_RESET), ">",
                on_reset, (void *)(intptr_t)ResetLevel::DISPLAY_RESET);
    create_item(list, i18n(S_NETWORK_RESET), ">",
                on_reset, (void *)(intptr_t)ResetLevel::NETWORK_RESET);
    create_item(list, i18n(S_FACTORY_RESET), ">",
                on_reset, (void *)(intptr_t)ResetLevel::FACTORY_RESET);

    return page;
}

void settings_page_refresh() {
    if (!page) return;
    DeviceSettings &s = settings_get();
    if (lbl_brightness_val) lv_label_set_text_fmt(lbl_brightness_val, "%d%%", s.brightness);
    if (lbl_lang_val) lv_label_set_text(lbl_lang_val, (s.language == Lang::ZH) ? "中文" : "English");
    if (lbl_device_name_val) lv_label_set_text(lbl_device_name_val, s.device_name);
}

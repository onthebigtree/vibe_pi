#include "oobe_flow.h"
#include "theme.h"
#include "../system/i18n.h"
#include "../system/settings_manager.h"
#include "../network/serial_config.h"
#include "config.h"
#include <WiFi.h>

static bool _finished = false;
static lv_obj_t *_scr = nullptr;
static lv_obj_t *_status_label = nullptr;

void oobe_init() {
    serial_config_init();
    _finished = false;
}

void oobe_show() {
    _scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_scr, CLR_BG, 0);
    lv_obj_set_scrollbar_mode(_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    // USB icon
    lv_obj_t *icon = lv_label_create(_scr);
    lv_label_set_text(icon, LV_SYMBOL_USB);
    lv_obj_set_style_text_color(icon, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(icon, &font_zh_28, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -80);

    // Title
    lv_obj_t *title = lv_label_create(_scr);
    lv_label_set_text(title, "Vibe Pi");
    lv_obj_set_style_text_color(title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &font_zh_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    // Instructions
    lv_obj_t *instr = lv_label_create(_scr);
    lv_label_set_text(instr, "Open in Chrome:");
    lv_obj_set_style_text_color(instr, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(instr, &font_zh_14, 0);
    lv_obj_set_style_text_align(instr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(instr, 360);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, 5);

    // URL
    lv_obj_t *url = lv_label_create(_scr);
    lv_label_set_text(url, "onthebigtree.github.io\n/vibe_pi/setup");
    lv_obj_set_style_text_color(url, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(url, &font_zh_20, 0);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(url, 400);
    lv_obj_align(url, LV_ALIGN_CENTER, 0, 35);

    // Status
    _status_label = lv_label_create(_scr);
    lv_label_set_text(_status_label, "Waiting for connection...");
    lv_obj_set_style_text_color(_status_label, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(_status_label, &font_zh_14, 0);
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_status_label, 360);
    lv_obj_align(_status_label, LV_ALIGN_CENTER, 0, 80);

    // Spinner
    lv_obj_t *spinner = lv_spinner_create(_scr);
    lv_obj_set_size(spinner, 30, 30);
    lv_obj_set_style_arc_color(spinner, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 120);

    lv_screen_load(_scr);

    Serial.println("[OOBE] Waiting for setup via USB serial...");
    Serial.println("[OOBE] Open web/index.html in Chrome, click Connect");
}

void oobe_loop() {
    serial_config_loop();

    // WiFi received → connect
    if (serial_config_has_wifi()) {
        const char *ssid = serial_config_get_ssid();
        const char *pass = serial_config_get_pass();

        if (_status_label) {
            lv_label_set_text_fmt(_status_label, "Connecting to %s...", ssid);
            lv_obj_set_style_text_color(_status_label, CLR_ACCENT, 0);
        }
        lv_timer_handler();

        settings_save_wifi(ssid, pass);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("{\"event\":\"wifi_connected\",\"ip\":\"%s\"}\n",
                          WiFi.localIP().toString().c_str());
            if (_status_label) {
                lv_label_set_text_fmt(_status_label, "WiFi OK: %s",
                                      WiFi.localIP().toString().c_str());
                lv_obj_set_style_text_color(_status_label, CLR_SUCCESS, 0);
            }
        } else {
            Serial.println("{\"event\":\"wifi_failed\"}");
            if (_status_label) {
                lv_label_set_text(_status_label, "WiFi failed, retry in web");
                lv_obj_set_style_text_color(_status_label, CLR_ERROR, 0);
            }
        }
        lv_timer_handler();
        serial_config_clear();
    }

    // Setup finished via web
    if (serial_config_is_setup_done()) {
        if (_status_label) {
            lv_label_set_text(_status_label, "Setup complete!");
            lv_obj_set_style_text_color(_status_label, CLR_SUCCESS, 0);
        }
        lv_timer_handler();
        delay(1000);
        _finished = true;
    }
}

bool oobe_is_finished() { return _finished; }

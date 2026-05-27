#include "oobe_flow.h"
#include "theme.h"
#include "ui_manager.h"
#include "../system/i18n.h"
#include "../system/settings_manager.h"
#include "../system/pairing_manager.h"
#include "config.h"
#include <WiFi.h>

static OobeStep step = OobeStep::LANGUAGE;
static lv_obj_t *oobe_scr = nullptr;
static lv_obj_t *content_area = nullptr;

// WiFi scan results
static String scannedSSIDs[16];
static int    scannedRSSIs[16];
static int    scanCount = 0;
static String selectedSSID;

static void clear_content();
static void show_language_step();
static void show_wifi_scan_step();
static void show_wifi_password_step();
static void show_wifi_connecting_step();
static void show_pairing_step();
static void show_syncing_step();
static void show_complete_step();

// ── Helpers ──

static lv_obj_t *create_centered_label(lv_obj_t *parent, const char *text,
                                        const lv_font_t *font, lv_color_t color, int y_offset) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl, 360);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, y_offset);
    return lbl;
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, int y_offset,
                               lv_event_cb_t cb, void *user_data = nullptr) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 240, 50);
    lv_obj_set_style_bg_color(btn, CLR_ACCENT, 0);
    lv_obj_set_style_radius(btn, 25, 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
    lv_obj_center(lbl);

    return btn;
}

// ── Init ──

void oobe_init() {
    step = OobeStep::LANGUAGE;
}

void oobe_show() {
    oobe_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(oobe_scr, CLR_BG, 0);
    lv_obj_set_scrollbar_mode(oobe_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(oobe_scr, LV_OBJ_FLAG_SCROLLABLE);

    // No intermediate container — buttons go directly on screen
    content_area = oobe_scr;

    show_language_step();
    lv_screen_load(oobe_scr);
}

void oobe_loop() {
    pairing_loop();
}

OobeStep oobe_get_step() { return step; }
bool oobe_is_finished() { return step == OobeStep::DONE; }

// ── Clear content for next step ──

static void clear_content() {
    if (content_area) {
        lv_obj_clean(content_area);
        // Re-apply screen style after clean
        lv_obj_set_style_bg_color(content_area, CLR_BG, 0);
        lv_obj_set_scrollbar_mode(content_area, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
    }
}

// ═══════════════════════════════════════════════════════════════
// Step 1: Language Selection
// ═══════════════════════════════════════════════════════════════

static void on_lang_zh(lv_event_t *e) {
    Serial.println("[OOBE] >>> CHINESE button clicked! <<<");
    i18n_set_lang(Lang::ZH);
    settings_get().language = Lang::ZH;
    step = OobeStep::WIFI_SCAN;
    clear_content();
    show_wifi_scan_step();
}

static void on_lang_en(lv_event_t *e) {
    Serial.println("[OOBE] >>> ENGLISH button clicked! <<<");
    i18n_set_lang(Lang::EN);
    settings_get().language = Lang::EN;
    step = OobeStep::WIFI_SCAN;
    clear_content();
    show_wifi_scan_step();
}

static void show_language_step() {
    create_centered_label(content_area, "Vibe Pi", FONT_TITLE, CLR_TEXT_PRIMARY, -80);
    create_centered_label(content_area, "Select Language / 选择语言", FONT_BODY, CLR_TEXT_SECONDARY, -30);
    create_button(content_area, "中文", 30, on_lang_zh);
    create_button(content_area, "English", 90, on_lang_en);
}

// ═══════════════════════════════════════════════════════════════
// Step 2: WiFi Scan
// ═══════════════════════════════════════════════════════════════

static void on_wifi_selected(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < scanCount) {
        selectedSSID = scannedSSIDs[idx];
        step = OobeStep::WIFI_PASSWORD;
        clear_content();
        show_wifi_password_step();
    }
}

static void show_wifi_scan_step() {
    create_centered_label(content_area, i18n(S_SCAN_WIFI), FONT_LARGE, CLR_TEXT_PRIMARY, -100);

    lv_obj_t *spinner = lv_spinner_create(content_area);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_set_style_arc_color(spinner, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

    create_centered_label(content_area, i18n(S_SCANNING), FONT_SMALL, CLR_TEXT_MUTED, 40);

    // Start WiFi scan
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true); // async
}

void oobe_on_wifi_scan_done(int count) {
    scanCount = min(count, 16);
    for (int i = 0; i < scanCount; i++) {
        scannedSSIDs[i] = WiFi.SSID(i);
        scannedRSSIs[i] = WiFi.RSSI(i);
    }
    WiFi.scanDelete();

    clear_content();
    create_centered_label(content_area, i18n(S_SELECT_NETWORK), FONT_LARGE, CLR_TEXT_PRIMARY, -120);

    // Scrollable list of networks
    lv_obj_t *list = lv_obj_create(content_area);
    lv_obj_set_size(list, 340, 240);
    lv_obj_set_style_bg_color(list, CLR_SURFACE, 0);
    lv_obj_set_style_border_color(list, CLR_BORDER, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 12, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 20);

    for (int i = 0; i < scanCount; i++) {
        lv_obj_t *item = lv_btn_create(list);
        lv_obj_set_size(item, 320, 40);
        lv_obj_set_style_bg_color(item, CLR_SURFACE_ALT, 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_add_event_cb(item, on_wifi_selected, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(item);
        lv_label_set_text(name, scannedSSIDs[i].c_str());
        lv_obj_set_style_text_color(name, CLR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(name, FONT_SMALL, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);

        // Signal strength indicator
        lv_obj_t *rssi = lv_label_create(item);
        int bars = (scannedRSSIs[i] > -50) ? 3 : (scannedRSSIs[i] > -70) ? 2 : 1;
        char barStr[8];
        snprintf(barStr, sizeof(barStr), "%s %ddBm",
                 bars >= 3 ? LV_SYMBOL_WIFI : (bars >= 2 ? LV_SYMBOL_WIFI : LV_SYMBOL_WIFI),
                 scannedRSSIs[i]);
        lv_label_set_text(rssi, barStr);
        lv_obj_set_style_text_color(rssi, CLR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(rssi, FONT_SMALL, 0);
        lv_obj_align(rssi, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

// ═══════════════════════════════════════════════════════════════
// Step 3: WiFi Password
// ═══════════════════════════════════════════════════════════════

static lv_obj_t *password_ta = nullptr;

static void on_wifi_connect_btn(lv_event_t *e) {
    const char *pass = lv_textarea_get_text(password_ta);
    step = OobeStep::WIFI_CONNECTING;
    clear_content();
    show_wifi_connecting_step();

    // Save and connect
    settings_save_wifi(selectedSSID.c_str(), pass);
    WiFi.begin(selectedSSID.c_str(), pass);
}

static void show_wifi_password_step() {
    create_centered_label(content_area, selectedSSID.c_str(), FONT_LARGE, CLR_ACCENT, -110);
    create_centered_label(content_area, i18n(S_ENTER_PASSWORD), FONT_SMALL, CLR_TEXT_SECONDARY, -75);

    password_ta = lv_textarea_create(content_area);
    lv_obj_set_size(password_ta, 300, 44);
    lv_textarea_set_password_mode(password_ta, true);
    lv_textarea_set_one_line(password_ta, true);
    lv_textarea_set_placeholder_text(password_ta, "WiFi Password");
    lv_obj_set_style_bg_color(password_ta, CLR_SURFACE, 0);
    lv_obj_set_style_text_color(password_ta, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(password_ta, CLR_BORDER, 0);
    lv_obj_set_style_radius(password_ta, 8, 0);
    lv_obj_align(password_ta, LV_ALIGN_CENTER, 0, -30);

    // On-screen keyboard
    lv_obj_t *kb = lv_keyboard_create(content_area);
    lv_obj_set_size(kb, 380, 200);
    lv_obj_set_style_bg_color(kb, CLR_SURFACE, 0);
    lv_obj_set_style_text_color(kb, CLR_TEXT_PRIMARY, 0);
    lv_keyboard_set_textarea(kb, password_ta);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -40);

    create_button(content_area, i18n(S_CONNECTING_WIFI), 30, on_wifi_connect_btn);
}

// ═══════════════════════════════════════════════════════════════
// Step 4: WiFi Connecting
// ═══════════════════════════════════════════════════════════════

static void show_wifi_connecting_step() {
    lv_obj_t *spinner = lv_spinner_create(content_area);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_set_style_arc_color(spinner, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    create_centered_label(content_area, i18n(S_CONNECTING_WIFI), FONT_BODY, CLR_TEXT_SECONDARY, 30);
}

void oobe_on_wifi_connected(const char *ip) {
    step = OobeStep::PAIRING;
    clear_content();
    show_pairing_step();
}

void oobe_on_wifi_failed() {
    clear_content();
    create_centered_label(content_area, LV_SYMBOL_WARNING, FONT_TITLE, CLR_ERROR, -40);
    create_centered_label(content_area, i18n(S_WIFI_FAILED), FONT_LARGE, CLR_TEXT_PRIMARY, 10);

    create_button(content_area, i18n(S_SCAN_WIFI), 70, [](lv_event_t *e) {
        step = OobeStep::WIFI_SCAN;
        clear_content();
        show_wifi_scan_step();
    });
}

// ═══════════════════════════════════════════════════════════════
// Step 5: Device Pairing
// ═══════════════════════════════════════════════════════════════

static void show_pairing_step() {
    String code = pairing_generate_code();

    create_centered_label(content_area, i18n(S_PAIRING), FONT_LARGE, CLR_TEXT_PRIMARY, -90);

    // Large pair code display
    lv_obj_t *codeLabel = lv_label_create(content_area);
    // Add spaces between digits for readability
    char formatted[16];
    snprintf(formatted, sizeof(formatted), "%c%c%c  %c%c%c",
             code[0], code[1], code[2], code[3], code[4], code[5]);
    lv_label_set_text(codeLabel, formatted);
    lv_obj_set_style_text_color(codeLabel, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(codeLabel, FONT_TITLE, 0);
    lv_obj_set_style_text_letter_space(codeLabel, 6, 0);
    lv_obj_align(codeLabel, LV_ALIGN_CENTER, 0, -20);

    create_centered_label(content_area, i18n(S_PAIR_WAITING), FONT_SMALL, CLR_TEXT_MUTED, 30);

    lv_obj_t *spinner = lv_spinner_create(content_area);
    lv_obj_set_size(spinner, 30, 30);
    lv_obj_set_style_arc_color(spinner, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 70);
}

void oobe_on_pair_confirmed() {
    step = OobeStep::SYNCING;
    clear_content();
    show_syncing_step();
}

void oobe_on_pair_rejected() {
    clear_content();
    create_centered_label(content_area, LV_SYMBOL_CLOSE, FONT_TITLE, CLR_ERROR, -40);
    create_centered_label(content_area, i18n(S_PAIR_FAILED), FONT_LARGE, CLR_TEXT_PRIMARY, 10);
    create_button(content_area, i18n(S_PAIRING), 70, [](lv_event_t *e) {
        clear_content();
        show_pairing_step();
    });
}

void oobe_on_pair_timeout() {
    clear_content();
    create_centered_label(content_area, i18n(S_PAIR_TIMEOUT), FONT_LARGE, CLR_WARNING, -10);
    create_button(content_area, i18n(S_PAIRING), 60, [](lv_event_t *e) {
        clear_content();
        show_pairing_step();
    });
}

// ═══════════════════════════════════════════════════════════════
// Step 6: Syncing
// ═══════════════════════════════════════════════════════════════

static void show_syncing_step() {
    lv_obj_t *spinner = lv_spinner_create(content_area);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_set_style_arc_color(spinner, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, CLR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    create_centered_label(content_area, i18n(S_SYNCING), FONT_BODY, CLR_TEXT_SECONDARY, 30);
}

void oobe_on_sync_done() {
    step = OobeStep::COMPLETE;
    clear_content();
    show_complete_step();
}

// ═══════════════════════════════════════════════════════════════
// Step 7: Complete
// ═══════════════════════════════════════════════════════════════

static void show_complete_step() {
    create_centered_label(content_area, LV_SYMBOL_OK, FONT_TITLE, CLR_SUCCESS, -60);
    create_centered_label(content_area, i18n(S_SETUP_COMPLETE), FONT_LARGE, CLR_TEXT_PRIMARY, -10);
    create_centered_label(content_area, i18n(S_SWIPE_HINT), FONT_SMALL, CLR_TEXT_MUTED, 30);

    create_button(content_area, "OK", 90, [](lv_event_t *e) {
        settings_mark_oobe_done();
        settings_save();
        step = OobeStep::DONE;
    });
}

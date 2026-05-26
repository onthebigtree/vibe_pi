#include "ui_manager.h"
#include "config.h"

static lv_obj_t *scr_splash = nullptr;
static lv_obj_t *scr_dashboard = nullptr;

// Dashboard elements
static lv_obj_t *lbl_tool_name = nullptr;
static lv_obj_t *lbl_status = nullptr;
static lv_obj_t *lbl_model = nullptr;
static lv_obj_t *lbl_tokens = nullptr;
static lv_obj_t *lbl_cost = nullptr;
static lv_obj_t *arc_usage = nullptr;

static lv_style_t style_bg;
static lv_style_t style_title;
static lv_style_t style_value;
static lv_style_t style_label;

static void init_styles() {
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_black());
    lv_style_set_radius(&style_bg, LV_RADIUS_CIRCLE);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_28);

    lv_style_init(&style_value);
    lv_style_set_text_color(&style_value, lv_color_make(0x64, 0xB5, 0xF6));
    lv_style_set_text_font(&style_value, &lv_font_montserrat_22);

    lv_style_init(&style_label);
    lv_style_set_text_color(&style_label, lv_color_make(0x90, 0x90, 0x90));
    lv_style_set_text_font(&style_label, &lv_font_montserrat_16);
}

void ui_show_splash() {
    init_styles();

    scr_splash = lv_obj_create(nullptr);
    lv_obj_add_style(scr_splash, &style_bg, 0);

    lv_obj_t *label = lv_label_create(scr_splash);
    lv_label_set_text(label, "Vibe Pi");
    lv_obj_add_style(label, &style_title, 0);
    lv_obj_center(label);

    lv_obj_t *sub = lv_label_create(scr_splash);
    lv_label_set_text(sub, "AI Dev Monitor");
    lv_obj_add_style(sub, &style_label, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 40);

    lv_screen_load(scr_splash);
}

void ui_show_connecting() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_add_style(scr, &style_bg, 0);

    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Connecting...");
    lv_obj_add_style(label, &style_label, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 30);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
}

void ui_show_wifi_error() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_add_style(scr, &style_bg, 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, LV_SYMBOL_WARNING " WiFi Failed");
    lv_obj_add_style(label, &style_title, 0);
    lv_obj_center(label);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
}

static void create_dashboard() {
    scr_dashboard = lv_obj_create(nullptr);
    lv_obj_add_style(scr_dashboard, &style_bg, 0);

    // Usage arc (circular progress)
    arc_usage = lv_arc_create(scr_dashboard);
    lv_obj_set_size(arc_usage, 380, 380);
    lv_arc_set_rotation(arc_usage, 135);
    lv_arc_set_bg_angles(arc_usage, 0, 270);
    lv_arc_set_value(arc_usage, 0);
    lv_obj_remove_flag(arc_usage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc_usage, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_usage, lv_color_make(0x64, 0xB5, 0xF6), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_usage, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_usage, 8, LV_PART_INDICATOR);
    lv_obj_center(arc_usage);

    // Tool name
    lbl_tool_name = lv_label_create(scr_dashboard);
    lv_label_set_text(lbl_tool_name, "Claude Code");
    lv_obj_add_style(lbl_tool_name, &style_title, 0);
    lv_obj_align(lbl_tool_name, LV_ALIGN_CENTER, 0, -80);

    // Status indicator
    lbl_status = lv_label_create(scr_dashboard);
    lv_label_set_text(lbl_status, "-- idle --");
    lv_obj_add_style(lbl_status, &style_label, 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, -50);

    // Model
    lbl_model = lv_label_create(scr_dashboard);
    lv_label_set_text(lbl_model, "");
    lv_obj_add_style(lbl_model, &style_value, 0);
    lv_obj_align(lbl_model, LV_ALIGN_CENTER, 0, -10);

    // Token count
    lbl_tokens = lv_label_create(scr_dashboard);
    lv_label_set_text(lbl_tokens, "0 tokens");
    lv_obj_add_style(lbl_tokens, &style_value, 0);
    lv_obj_align(lbl_tokens, LV_ALIGN_CENTER, 0, 30);

    // Cost
    lbl_cost = lv_label_create(scr_dashboard);
    lv_label_set_text(lbl_cost, "$0.00");
    lv_obj_add_style(lbl_cost, &style_label, 0);
    lv_obj_align(lbl_cost, LV_ALIGN_CENTER, 0, 65);
}

void ui_show_dashboard() {
    if (!scr_dashboard) {
        create_dashboard();
    }
    lv_screen_load_anim(scr_dashboard, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

void ui_update_data(const JsonDocument &doc) {
    if (!scr_dashboard) return;

    if (doc["tool"].is<const char*>()) {
        lv_label_set_text(lbl_tool_name, doc["tool"].as<const char*>());
    }
    if (doc["status"].is<const char*>()) {
        lv_label_set_text(lbl_status, doc["status"].as<const char*>());
    }
    if (doc["model"].is<const char*>()) {
        lv_label_set_text(lbl_model, doc["model"].as<const char*>());
    }
    if (doc["tokens"].is<const char*>()) {
        lv_label_set_text(lbl_tokens, doc["tokens"].as<const char*>());
    }
    if (doc["cost"].is<const char*>()) {
        lv_label_set_text(lbl_cost, doc["cost"].as<const char*>());
    }
    if (doc["usage_pct"].is<int>()) {
        lv_arc_set_value(arc_usage, doc["usage_pct"].as<int>());
    }
}

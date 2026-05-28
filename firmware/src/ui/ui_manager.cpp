#include "ui_manager.h"
#include "theme.h"
#include "notification.h"
#include "../display/display.h"
#include "../system/i18n.h"
#include "../system/settings_manager.h"
#include "hal/board.h"
#include "config.h"

static void apply_round_clip(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    if (scr_round()) {
        lv_obj_set_style_radius(scr, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(scr, true, 0);
    }
}

// ── Screen objects ──
static lv_obj_t *scr_boot = nullptr;
static lv_obj_t *scr_provision = nullptr;
static lv_obj_t *scr_connecting = nullptr;
static lv_obj_t *scr_dashboard = nullptr;

// ── Dashboard pages (embedded in tileview) ──
static lv_obj_t *tv = nullptr;             // tileview for swipe navigation
static lv_obj_t *tile_overview = nullptr;
static lv_obj_t *tile_tool_detail = nullptr;
static lv_obj_t *tile_system = nullptr;

// ── Overview page widgets ──
static lv_obj_t *ov_arc_main = nullptr;
static lv_obj_t *ov_lbl_tool = nullptr;
static lv_obj_t *ov_lbl_status = nullptr;
static lv_obj_t *ov_lbl_model = nullptr;
static lv_obj_t *ov_lbl_tokens = nullptr;
static lv_obj_t *ov_lbl_cost = nullptr;
static lv_obj_t *ov_dot_indicators = nullptr;

// ── Tool detail page widgets ──
static lv_obj_t *td_lbl_title = nullptr;
static lv_obj_t *td_lbl_task = nullptr;
static lv_obj_t *td_lbl_session = nullptr;
static lv_obj_t *td_lbl_uptime = nullptr;
static lv_obj_t *td_arc_usage = nullptr;
static lv_obj_t *td_lbl_usage_pct = nullptr;

// ── System page widgets ──
static lv_obj_t *sys_arc_cpu = nullptr;
static lv_obj_t *sys_arc_mem = nullptr;
static lv_obj_t *sys_lbl_cpu = nullptr;
static lv_obj_t *sys_lbl_mem = nullptr;
static lv_obj_t *sys_lbl_net = nullptr;

// ── State ──
static int current_page = 0;
static const int PAGE_COUNT = 3;
static bool sleeping = false;
static lv_color_t current_tool_color = CLR_ACCENT;

// ── Forward declarations ──
static void create_overview_tile();
static void create_detail_tile();
static void create_system_tile();
static lv_obj_t *create_status_screen(const char *icon, const char *title, const char *subtitle);
static void create_dot_indicators(lv_obj_t *parent, int active, int total);
static void update_dot_indicators(int active);

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

void ui_init() {
    theme_init();
}

// ═══════════════════════════════════════════════════════════════════
// Status / transitional screens
// ═══════════════════════════════════════════════════════════════════

static lv_obj_t *create_status_screen(const char *icon, const char *title, const char *subtitle) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    apply_round_clip(scr);

    if (icon && strlen(icon) > 0) {
        lv_obj_t *ic = lv_label_create(scr);
        lv_label_set_text(ic, icon);
        lv_obj_set_style_text_color(ic, CLR_ACCENT, 0);
        lv_obj_set_style_text_font(ic, FONT_TITLE, 0);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, -40);
    }

    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lbl, FONT_LARGE, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, icon ? 10 : -10);

    if (subtitle && strlen(subtitle) > 0) {
        lv_obj_t *sub = lv_label_create(scr);
        lv_label_set_text(sub, subtitle);
        lv_obj_set_style_text_color(sub, CLR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(sub, FONT_BODY, 0);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(sub, 300);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 45);
    }

    return scr;
}

void ui_show_boot() {
    scr_boot = lv_obj_create(nullptr);
    apply_round_clip(scr_boot);

    // Animated ring
    lv_obj_t *ring = lv_arc_create(scr_boot);
    lv_obj_set_size(ring, 200, 200);
    lv_arc_set_rotation(ring, 0);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_value(ring, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ring, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR);
    lv_obj_center(ring);

    // Boot animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_arc_set_value((lv_obj_t *)obj, v);
    });
    lv_anim_start(&a);

    lv_obj_t *name = lv_label_create(scr_boot);
    lv_label_set_text(name, "Vibe Pi");
    lv_obj_set_style_text_color(name, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, FONT_TITLE, 0);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *ver = lv_label_create(scr_boot);
    lv_label_set_text_fmt(ver, "v%s", FW_VERSION);
    lv_obj_set_style_text_color(ver, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ver, FONT_SMALL, 0);
    lv_obj_align(ver, LV_ALIGN_CENTER, 0, 20);

    lv_screen_load(scr_boot);
}

void ui_show_provision(const char *ap_ssid, const char *ap_ip) {
    char subtitle[128];
    snprintf(subtitle, sizeof(subtitle), "Connect to WiFi:\n%s\n\nThen open:\nhttp://%s", ap_ssid, ap_ip);

    scr_provision = create_status_screen(LV_SYMBOL_WIFI, "Setup Required", subtitle);
    lv_screen_load(scr_provision);
}

// Shared cached screens — created once, reused
static lv_obj_t *scr_connecting_wifi = nullptr;
static lv_obj_t *scr_wifi_failed = nullptr;
static lv_obj_t *scr_discovering = nullptr;
static lv_obj_t *scr_reconnecting = nullptr;

static lv_obj_t *make_spinner_screen(const char *text, lv_color_t indicator_color) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    apply_round_clip(scr);

    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_set_style_arc_color(spinner, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, indicator_color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_eq(indicator_color, CLR_WARNING) ? CLR_WARNING : CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);
    return scr;
}

void ui_show_connecting_wifi() {
    if (!scr_connecting_wifi) {
        scr_connecting_wifi = make_spinner_screen("Connecting to WiFi...", CLR_ACCENT);
        scr_connecting = scr_connecting_wifi;
    }
    lv_screen_load(scr_connecting_wifi);
}

void ui_show_wifi_failed() {
    if (!scr_wifi_failed) {
        scr_wifi_failed = create_status_screen(LV_SYMBOL_WARNING, "WiFi Failed",
            "Could not connect.\nHold button to\nre-enter setup.");
        lv_obj_set_style_text_color(lv_obj_get_child(scr_wifi_failed, 0), CLR_ERROR, 0);
    }
    lv_screen_load(scr_wifi_failed);
}

void ui_show_discovering() {
    if (!scr_discovering) {
        scr_discovering = make_spinner_screen("Finding host...", CLR_ACCENT);
    }
    lv_screen_load(scr_discovering);
}

void ui_show_reconnecting() {
    if (!scr_reconnecting) {
        scr_reconnecting = make_spinner_screen("Reconnecting...", CLR_WARNING);
    }
    lv_screen_load(scr_reconnecting);
}

// ═══════════════════════════════════════════════════════════════════
// Dashboard (main screen with tileview for swipe)
// ═══════════════════════════════════════════════════════════════════

static void on_tile_changed(lv_event_t *e) {
    lv_obj_t *active = lv_tileview_get_tile_active(tv);
    if (active == tile_overview)        current_page = 0;
    else if (active == tile_tool_detail) current_page = 1;
    else if (active == tile_system)      current_page = 2;
    update_dot_indicators(current_page);
}

void ui_show_dashboard() {
    Serial.printf("[DBG] ui_show_dashboard entry, heap=%lu\n", ESP.getFreeHeap());
    if (scr_dashboard) {
        Serial.println("[DBG] reusing cached dashboard");
        lv_screen_load(scr_dashboard);
        return;
    }

    Serial.println("[DBG] creating dashboard screen (no round_clip)");
    scr_dashboard = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_dashboard, lv_color_black(), 0);
    // SKIP apply_round_clip(scr_dashboard);  // expensive alpha mask
    Serial.println("[DBG] after round_clip skip");

    // Tileview for horizontal swipe between pages
    tv = lv_tileview_create(scr_dashboard);
    lv_obj_set_size(tv, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    tile_overview    = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    tile_tool_detail = lv_tileview_add_tile(tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    tile_system      = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);

    lv_obj_set_style_bg_opa(tile_overview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(tile_tool_detail, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(tile_system, LV_OPA_TRANSP, 0);

    Serial.println("[DBG] creating overview");
    create_overview_tile();
    Serial.println("[DBG] creating detail");
    create_detail_tile();
    Serial.println("[DBG] creating system");
    create_system_tile();
    Serial.printf("[DBG] tiles done heap=%lu\n", ESP.getFreeHeap());

    lv_obj_add_event_cb(tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    // Page indicator dots
    ov_dot_indicators = lv_obj_create(scr_dashboard);
    lv_obj_set_size(ov_dot_indicators, 60, 10);
    lv_obj_set_style_bg_opa(ov_dot_indicators, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ov_dot_indicators, 0, 0);
    lv_obj_set_style_pad_all(ov_dot_indicators, 0, 0);
    lv_obj_set_flex_flow(ov_dot_indicators, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ov_dot_indicators, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ov_dot_indicators, 8, 0);
    lv_obj_align(ov_dot_indicators, LV_ALIGN_BOTTOM_MID, 0, -50);
    create_dot_indicators(ov_dot_indicators, 0, PAGE_COUNT);
    Serial.printf("[DBG] dashboard ready heap=%lu, loading screen\n", ESP.getFreeHeap());
    lv_screen_load(scr_dashboard);
    Serial.println("[DBG] dashboard loaded");
}

// ── Overview page ──

static void create_overview_tile() {
    // Outer arc (usage)
    ov_arc_main = lv_arc_create(tile_overview);
    lv_obj_set_size(ov_arc_main, ARC_OUTER_SIZE, ARC_OUTER_SIZE);
    lv_arc_set_rotation(ov_arc_main, 135);
    lv_arc_set_bg_angles(ov_arc_main, 0, 270);
    lv_arc_set_range(ov_arc_main, 0, 100);
    lv_arc_set_value(ov_arc_main, 0);
    lv_obj_remove_flag(ov_arc_main, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ov_arc_main, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ov_arc_main, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ov_arc_main, ARC_OUTER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ov_arc_main, ARC_OUTER_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ov_arc_main, true, LV_PART_INDICATOR);
    lv_obj_center(ov_arc_main);

    // Tool name
    ov_lbl_tool = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_tool, "Idle");
    lv_obj_set_style_text_color(ov_lbl_tool, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ov_lbl_tool, FONT_TITLE, 0);
    lv_obj_align(ov_lbl_tool, LV_ALIGN_CENTER, 0, -60);

    // Status dot + text
    ov_lbl_status = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_status, "No active tools");
    lv_obj_set_style_text_color(ov_lbl_status, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ov_lbl_status, FONT_SMALL, 0);
    lv_obj_align(ov_lbl_status, LV_ALIGN_CENTER, 0, -30);

    // Model
    ov_lbl_model = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_model, "");
    lv_obj_set_style_text_color(ov_lbl_model, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ov_lbl_model, FONT_LARGE, 0);
    lv_obj_align(ov_lbl_model, LV_ALIGN_CENTER, 0, 10);

    // Tokens
    ov_lbl_tokens = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_tokens, "");
    lv_obj_set_style_text_color(ov_lbl_tokens, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(ov_lbl_tokens, FONT_BODY, 0);
    lv_obj_align(ov_lbl_tokens, LV_ALIGN_CENTER, 0, 45);

    // Cost
    ov_lbl_cost = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_cost, "");
    lv_obj_set_style_text_color(ov_lbl_cost, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ov_lbl_cost, FONT_SMALL, 0);
    lv_obj_align(ov_lbl_cost, LV_ALIGN_CENTER, 0, 70);
}

// ── Tool detail page ──

static void create_detail_tile() {
    td_lbl_title = lv_label_create(tile_tool_detail);
    lv_label_set_text(td_lbl_title, "Tool Details");
    lv_obj_set_style_text_color(td_lbl_title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(td_lbl_title, FONT_LARGE, 0);
    lv_obj_align(td_lbl_title, LV_ALIGN_TOP_MID, 0, 80);

    // Usage arc
    td_arc_usage = lv_arc_create(tile_tool_detail);
    lv_obj_set_size(td_arc_usage, 160, 160);
    lv_arc_set_rotation(td_arc_usage, 135);
    lv_arc_set_bg_angles(td_arc_usage, 0, 270);
    lv_arc_set_range(td_arc_usage, 0, 100);
    lv_arc_set_value(td_arc_usage, 0);
    lv_obj_remove_flag(td_arc_usage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(td_arc_usage, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(td_arc_usage, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(td_arc_usage, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(td_arc_usage, 6, LV_PART_INDICATOR);
    lv_obj_align(td_arc_usage, LV_ALIGN_CENTER, 0, -20);

    td_lbl_usage_pct = lv_label_create(tile_tool_detail);
    lv_label_set_text(td_lbl_usage_pct, "0%");
    lv_obj_set_style_text_color(td_lbl_usage_pct, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(td_lbl_usage_pct, FONT_TITLE, 0);
    lv_obj_align(td_lbl_usage_pct, LV_ALIGN_CENTER, 0, -20);

    td_lbl_task = lv_label_create(tile_tool_detail);
    lv_label_set_text(td_lbl_task, "");
    lv_obj_set_style_text_color(td_lbl_task, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(td_lbl_task, FONT_SMALL, 0);
    lv_obj_set_width(td_lbl_task, 320);
    lv_label_set_long_mode(td_lbl_task, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(td_lbl_task, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(td_lbl_task, LV_ALIGN_CENTER, 0, 80);

    td_lbl_session = lv_label_create(tile_tool_detail);
    lv_label_set_text(td_lbl_session, "");
    lv_obj_set_style_text_color(td_lbl_session, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(td_lbl_session, FONT_SMALL, 0);
    lv_obj_align(td_lbl_session, LV_ALIGN_CENTER, 0, 105);

    td_lbl_uptime = lv_label_create(tile_tool_detail);
    lv_label_set_text(td_lbl_uptime, "");
    lv_obj_set_style_text_color(td_lbl_uptime, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(td_lbl_uptime, FONT_SMALL, 0);
    lv_obj_align(td_lbl_uptime, LV_ALIGN_CENTER, 0, 125);
}

// ── System page ──

static lv_obj_t *create_gauge_arc(lv_obj_t *parent, int x, int y, int size, lv_color_t color) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_align(arc, LV_ALIGN_CENTER, x, y);
    return arc;
}

static void create_system_tile() {
    lv_obj_t *title = lv_label_create(tile_system);
    lv_label_set_text(title, "System");
    lv_obj_set_style_text_color(title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

    // CPU gauge
    sys_arc_cpu = create_gauge_arc(tile_system, -70, -20, 140, CLR_ACCENT);
    sys_lbl_cpu = lv_label_create(tile_system);
    lv_label_set_text(sys_lbl_cpu, "0%");
    lv_obj_set_style_text_color(sys_lbl_cpu, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(sys_lbl_cpu, FONT_LARGE, 0);
    lv_obj_align(sys_lbl_cpu, LV_ALIGN_CENTER, -70, -20);

    lv_obj_t *cpu_label = lv_label_create(tile_system);
    lv_label_set_text(cpu_label, "CPU");
    lv_obj_set_style_text_color(cpu_label, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(cpu_label, FONT_SMALL, 0);
    lv_obj_align(cpu_label, LV_ALIGN_CENTER, -70, 10);

    // Memory gauge
    sys_arc_mem = create_gauge_arc(tile_system, 70, -20, 140, CLR_WARNING);
    sys_lbl_mem = lv_label_create(tile_system);
    lv_label_set_text(sys_lbl_mem, "0%");
    lv_obj_set_style_text_color(sys_lbl_mem, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(sys_lbl_mem, FONT_LARGE, 0);
    lv_obj_align(sys_lbl_mem, LV_ALIGN_CENTER, 70, -20);

    lv_obj_t *mem_label = lv_label_create(tile_system);
    lv_label_set_text(mem_label, "MEM");
    lv_obj_set_style_text_color(mem_label, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(mem_label, FONT_SMALL, 0);
    lv_obj_align(mem_label, LV_ALIGN_CENTER, 70, 10);

    // Network
    sys_lbl_net = lv_label_create(tile_system);
    lv_label_set_text(sys_lbl_net, "");
    lv_obj_set_style_text_color(sys_lbl_net, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(sys_lbl_net, FONT_SMALL, 0);
    lv_obj_set_style_text_align(sys_lbl_net, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sys_lbl_net, LV_ALIGN_CENTER, 0, 90);
}

// ── Dot indicators ──

static void create_dot_indicators(lv_obj_t *parent, int active, int total) {
    for (int i = 0; i < total; i++) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, (i == active) ? CLR_ACCENT : CLR_TEXT_MUTED, 0);
        lv_obj_set_scrollbar_mode(dot, LV_SCROLLBAR_MODE_OFF);
    }
}

static void update_dot_indicators(int active) {
    if (!ov_dot_indicators) return;
    uint32_t cnt = lv_obj_get_child_count(ov_dot_indicators);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *dot = lv_obj_get_child(ov_dot_indicators, i);
        lv_obj_set_style_bg_color(dot, ((int)i == active) ? CLR_ACCENT : CLR_TEXT_MUTED, 0);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Data update
// ═══════════════════════════════════════════════════════════════════

void ui_update_status(JsonObject &payload) {
    const char *activeTool = payload["active_tool"];

    // -- Update overview page --
    JsonObject tools = payload["tools"];
    if (activeTool && strcmp(activeTool, "idle") != 0 && tools.containsKey(activeTool)) {
        JsonObject tool = tools[activeTool];

        current_tool_color = theme_tool_color(activeTool);

        // Display name mapping
        const char *displayName = activeTool;
        if (strcmp(activeTool, "claude_code") == 0) displayName = "Claude Code";
        else if (strcmp(activeTool, "codex") == 0) displayName = "Codex";
        else if (strcmp(activeTool, "gemini_cli") == 0) displayName = "Gemini CLI";

        lv_label_set_text(ov_lbl_tool, displayName);
        lv_obj_set_style_text_color(ov_lbl_tool, current_tool_color, 0);

        const char *status = tool["status"] | "unknown";
        lv_label_set_text_fmt(ov_lbl_status, "%s %s", (strcmp(status, "active") == 0) ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE, status);
        lv_obj_set_style_text_color(ov_lbl_status, (strcmp(status, "active") == 0) ? CLR_SUCCESS : CLR_TEXT_MUTED, 0);

        const char *model = tool["model"] | "";
        lv_label_set_text(ov_lbl_model, model);

        const char *tokensDisp = tool["tokens_display"] | "";
        lv_label_set_text(ov_lbl_tokens, tokensDisp);

        const char *costDisp = tool["cost_display"] | "";
        lv_label_set_text(ov_lbl_cost, costDisp);

        int usagePct = tool["usage_pct"] | 0;
        lv_arc_set_value(ov_arc_main, usagePct);
        lv_obj_set_style_arc_color(ov_arc_main, current_tool_color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ov_lbl_tokens, current_tool_color, 0);

        // -- Update detail page --
        lv_label_set_text(td_lbl_title, displayName);
        lv_obj_set_style_text_color(td_lbl_title, current_tool_color, 0);

        lv_arc_set_value(td_arc_usage, usagePct);
        lv_obj_set_style_arc_color(td_arc_usage, current_tool_color, LV_PART_INDICATOR);
        lv_label_set_text_fmt(td_lbl_usage_pct, "%d%%", usagePct);

        const char *task = tool["current_task"] | "";
        lv_label_set_text(td_lbl_task, task[0] ? task : "No active task");

        int sessions = tool["session_count"] | 0;
        lv_label_set_text_fmt(td_lbl_session, "%d session%s", sessions, sessions != 1 ? "s" : "");

        int uptime = tool["uptime_min"] | 0;
        if (uptime > 0) lv_label_set_text_fmt(td_lbl_uptime, "%d min uptime", uptime);
        else lv_label_set_text(td_lbl_uptime, "");

    } else {
        lv_label_set_text(ov_lbl_tool, "Idle");
        lv_obj_set_style_text_color(ov_lbl_tool, CLR_TEXT_PRIMARY, 0);
        lv_label_set_text(ov_lbl_status, "No active tools");
        lv_obj_set_style_text_color(ov_lbl_status, CLR_TEXT_MUTED, 0);
        lv_label_set_text(ov_lbl_model, "");
        lv_label_set_text(ov_lbl_tokens, "");
        lv_label_set_text(ov_lbl_cost, "");
        lv_arc_set_value(ov_arc_main, 0);

        lv_label_set_text(td_lbl_title, "No Tool Active");
        lv_arc_set_value(td_arc_usage, 0);
        lv_label_set_text(td_lbl_usage_pct, "—");
        lv_label_set_text(td_lbl_task, "");
        lv_label_set_text(td_lbl_session, "");
        lv_label_set_text(td_lbl_uptime, "");
    }

    // -- Update system page --
    JsonObject sys = payload["system"];
    if (!sys.isNull()) {
        int cpu = sys["cpu_pct"] | 0;
        int mem = sys["mem_pct"] | 0;

        lv_arc_set_value(sys_arc_cpu, cpu);
        lv_label_set_text_fmt(sys_lbl_cpu, "%d%%", cpu);
        if (cpu > 80) lv_obj_set_style_arc_color(sys_arc_cpu, CLR_ERROR, LV_PART_INDICATOR);
        else if (cpu > 60) lv_obj_set_style_arc_color(sys_arc_cpu, CLR_WARNING, LV_PART_INDICATOR);
        else lv_obj_set_style_arc_color(sys_arc_cpu, CLR_ACCENT, LV_PART_INDICATOR);

        lv_arc_set_value(sys_arc_mem, mem);
        lv_label_set_text_fmt(sys_lbl_mem, "%d%%", mem);

        int netUp = sys["net_up_kbps"] | 0;
        int netDown = sys["net_down_kbps"] | 0;
        lv_label_set_text_fmt(sys_lbl_net, LV_SYMBOL_UPLOAD " %d KB/s    " LV_SYMBOL_DOWNLOAD " %d KB/s", netUp, netDown);
    }
}

void ui_set_page(int page_index) {
    if (!tv || page_index < 0 || page_index >= PAGE_COUNT) return;
    lv_obj_t *tiles[] = {tile_overview, tile_tool_detail, tile_system};
    lv_tileview_set_tile(tv, tiles[page_index], LV_ANIM_ON);
    current_page = page_index;
    update_dot_indicators(current_page);
}

int ui_get_current_page() { return current_page; }
int ui_get_page_count() { return PAGE_COUNT; }

// ── Display control ──

void ui_sleep() {
    if (sleeping) return;
    sleeping = true;
    display_set_brightness(0);
    Serial.println("[UI] Display sleep");
}

void ui_wake() {
    if (!sleeping) return;
    sleeping = false;
    display_set_brightness(80);
    Serial.println("[UI] Display wake");
}

bool ui_is_sleeping() { return sleeping; }

// ── Pairing screen ──

void ui_show_pairing(const char *code) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    apply_round_clip(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, i18n(S_PAIRING));
    lv_obj_set_style_text_color(title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t *codeLabel = lv_label_create(scr);
    char formatted[16];
    if (code && strlen(code) >= 6) {
        snprintf(formatted, sizeof(formatted), "%c%c%c  %c%c%c",
                 code[0], code[1], code[2], code[3], code[4], code[5]);
    } else {
        strlcpy(formatted, code ? code : "------", sizeof(formatted));
    }
    lv_label_set_text(codeLabel, formatted);
    lv_obj_set_style_text_color(codeLabel, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(codeLabel, FONT_TITLE, 0);
    lv_obj_set_style_text_letter_space(codeLabel, 6, 0);
    lv_obj_align(codeLabel, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, i18n(S_PAIR_WAITING));
    lv_obj_set_style_text_color(hint, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, 320);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 30, 30);
    lv_obj_set_style_arc_color(spinner, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 75);

    lv_screen_load(scr);
}

// ── Safe mode screen ──

void ui_show_safe_mode() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    apply_round_clip(scr);

    lv_obj_t *icon = lv_label_create(scr);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, CLR_WARNING, 0);
    lv_obj_set_style_text_font(icon, FONT_TITLE, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, i18n(S_SAFE_MODE));
    lv_obj_set_style_text_color(title, CLR_WARNING, 0);
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Hold button 10s\nto factory reset");
    lv_obj_set_style_text_color(hint, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 50);

    lv_screen_load(scr);
}

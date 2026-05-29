#include "ui_manager.h"
#include "theme.h"
#include "notification.h"
#include "../display/display.h"
#include "../system/i18n.h"
#include "../system/settings_manager.h"
#include "../system/watchdog.h"
#include "../system/power_manager.h"
#include "hal/board.h"
#include "config.h"
#include <math.h>

static void apply_round_clip(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    if (scr_round()) {
        // Round visual only. Do NOT enable clip_corner: the radial alpha mask
        // combined with arc widgets (spinners on connecting/pairing/reconnect
        // screens) deadlocks the QSPI flush path (dev-boundaries rule #1).
        // The AMOLED is physically round + bezel-masked, so the corners are
        // never visible anyway.
        lv_obj_set_style_radius(scr, LV_RADIUS_CIRCLE, 0);
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
static lv_obj_t *ov_arc_main = nullptr;   // outer ring = 7d window
static lv_obj_t *ov_arc_5h = nullptr;     // inner ring = 5h window
static lv_obj_t *ov_spark = nullptr;      // animated Claude sunburst
static lv_obj_t *ov_lbl_tool = nullptr;
static lv_obj_t *ov_lbl_status = nullptr;  // repurposed as task-state badge
static lv_obj_t *ov_lbl_model = nullptr;
static lv_obj_t *ov_rd_7d = nullptr;       // top readout: 7d window "7d 9%"
static lv_obj_t *ov_rd_5h = nullptr;       // top readout: 5h window "5h 92%"
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

// ── Always-visible battery indicator (top-right of dashboard) ──
static lv_obj_t *battery_lbl = nullptr;

// ── Mini-tool indicator row on Overview ──
static lv_obj_t *mini_tools_row = nullptr;
static lv_obj_t *mini_tool_dots[6] = {nullptr};
static lv_obj_t *sys_lbl_cpu = nullptr;
static lv_obj_t *sys_lbl_mem = nullptr;
static lv_obj_t *sys_lbl_net = nullptr;

// ── State ──
static int current_page = 0;
static const int PAGE_COUNT = 3;
static bool sleeping = false;
static lv_color_t current_tool_color = CLR_ACCENT;

// Tool selection state — exposed so ui_update_status and gesture handlers share it.
char selected_tool[24] = "";          // empty = auto-pick active_tool
char available_tools[6][24];           // populated from each status payload
int available_tools_count = 0;

extern uint16_t axp_voltage();
extern uint8_t  axp_percent();
extern bool     axp_charging();
extern bool     axp_vbus();

void ui_update_battery() {
    if (!battery_lbl) return;
    uint16_t mv = axp_voltage();
    if (mv == 0) {
        // No AXP2101 detected → hide
        lv_label_set_text(battery_lbl, "");
        return;
    }
    uint8_t pct = axp_percent();
    const char *icon;
    if (axp_charging() || axp_vbus()) icon = LV_SYMBOL_CHARGE;
    else if (pct > 80) icon = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 60) icon = LV_SYMBOL_BATTERY_3;
    else if (pct > 40) icon = LV_SYMBOL_BATTERY_2;
    else if (pct > 20) icon = LV_SYMBOL_BATTERY_1;
    else icon = LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(battery_lbl, "%s %d%%", icon, pct);
    lv_obj_set_style_text_color(battery_lbl,
        pct < 20 ? CLR_ERROR : (pct < 40 ? CLR_WARNING : CLR_TEXT_SECONDARY), 0);
}

// Forward decl — defined alongside the spark builder further down, but used by
// ui_cycle_tool() above it for instant recolor on tap.
static void spark_set_color(lv_obj_t *spark, lv_color_t c);

// De-dupe hash for ui_update_status, hoisted to module scope so ui_cycle_tool()
// can invalidate it and force the next status to redraw even if the host data
// is byte-identical (selected_tool is not part of the hash).
static uint32_t g_status_hash = 0;
static bool     g_force_status_redraw = false;

static const char *tool_display_name(const char *tool) {
    if (!tool) return "";
    if (strcmp(tool, "claude_code") == 0) return "Claude Code";
    if (strcmp(tool, "codex") == 0)       return "Codex";
    if (strcmp(tool, "gemini_cli") == 0)  return "Gemini CLI";
    if (strcmp(tool, "cursor") == 0)      return "Cursor";
    if (strcmp(tool, "windsurf") == 0)    return "Windsurf";
    return tool;
}

// Refresh the mini-dot row from selected_tool/available_tools. Safe to call
// any time (e.g. instantly on tap, before the host round-trips fresh data).
static void update_mini_dots() {
    for (int i = 0; i < 6; i++) {
        if (!mini_tool_dots[i]) continue;
        if (i < available_tools_count) {
            lv_obj_remove_flag(mini_tool_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_color_t c = theme_tool_color(available_tools[i]);
            lv_obj_set_style_bg_color(mini_tool_dots[i], c, 0);
            bool is_selected = (strcmp(available_tools[i], selected_tool) == 0);
            lv_obj_set_size(mini_tool_dots[i], is_selected ? 16 : 10, is_selected ? 16 : 10);
            lv_obj_set_style_bg_opa(mini_tool_dots[i], is_selected ? LV_OPA_COVER : LV_OPA_60, 0);
        } else {
            lv_obj_add_flag(mini_tool_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_cycle_tool() {
    if (available_tools_count == 0) return;
    int idx = 0;
    for (int i = 0; i < available_tools_count; i++) {
        if (strcmp(available_tools[i], selected_tool) == 0) { idx = i; break; }
    }
    idx = (idx + 1) % available_tools_count;
    strlcpy(selected_tool, available_tools[idx], sizeof(selected_tool));
    Serial.printf("[UI] Cycled to tool: %s\n", selected_tool);

    // Instant local feedback: reflect the new selection immediately so the tap
    // feels responsive, instead of waiting for the host status round-trip.
    if (ov_lbl_tool) {
        lv_color_t c = theme_tool_color(selected_tool);
        lv_label_set_text(ov_lbl_tool, tool_display_name(selected_tool));
        lv_obj_set_style_text_color(ov_lbl_tool, c, 0);
        spark_set_color(ov_spark, c);
    }
    update_mini_dots();
    lv_obj_invalidate(lv_screen_active());  // clean repaint, no partial-render ghosts
    // Force the next status to redraw even if its data is unchanged.
    g_force_status_redraw = true;

    // Ask host for an immediate status refresh so the new tool's data appears now
    Serial.println("{\"v\":2,\"type\":\"refresh_request\",\"ts\":0,\"payload\":{}}");
}

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
    // Lightweight: just text, no arc/anim/round_clip (those hang the renderer)
    if (scr_boot) { lv_screen_load(scr_boot); return; }
    scr_boot = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_boot, lv_color_black(), 0);

    lv_obj_t *name = lv_label_create(scr_boot);
    lv_label_set_text(name, "Vibe Pi");
    lv_obj_set_style_text_color(name, CLR_ACCENT, 0);
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
    lv_obj_set_style_pad_all(scr_dashboard, 0, 0);
    lv_obj_set_style_border_width(scr_dashboard, 0, 0);
    lv_obj_remove_flag(scr_dashboard, LV_OBJ_FLAG_SCROLLABLE);
    Serial.println("[DBG] after style setup");

    // Tileview for horizontal swipe between pages
    tv = lv_tileview_create(scr_dashboard);
    lv_obj_set_size(tv, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tv, 0, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(tv, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_dir(tv, LV_DIR_HOR);

    tile_overview    = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    tile_tool_detail = lv_tileview_add_tile(tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    tile_system      = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);

    // Tiles: transparent bg, disable internal scrolling (tileview handles swipe)
    for (lv_obj_t *tile : {tile_overview, tile_tool_detail, tile_system}) {
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
    }

    Serial.println("[DBG] creating overview");
    create_overview_tile();
    Serial.println("[DBG] creating detail");
    create_detail_tile();
    Serial.println("[DBG] creating system");
    create_system_tile();
    Serial.printf("[DBG] tiles done heap=%lu\n", ESP.getFreeHeap());

    lv_obj_add_event_cb(tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    // Page indicator dots — make them NON-CLICKABLE so they don't intercept tileview swipes
    ov_dot_indicators = lv_obj_create(scr_dashboard);
    lv_obj_set_size(ov_dot_indicators, 60, 10);
    lv_obj_set_style_bg_opa(ov_dot_indicators, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ov_dot_indicators, 0, 0);
    lv_obj_set_style_pad_all(ov_dot_indicators, 0, 0);
    lv_obj_set_flex_flow(ov_dot_indicators, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ov_dot_indicators, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ov_dot_indicators, 8, 0);
    lv_obj_align(ov_dot_indicators, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_remove_flag(ov_dot_indicators, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov_dot_indicators, LV_OBJ_FLAG_SCROLLABLE);
    create_dot_indicators(ov_dot_indicators, 0, PAGE_COUNT);

    // Battery indicator — top-right, visible on every tile (sits above tileview)
    battery_lbl = lv_label_create(scr_dashboard);
    lv_label_set_text(battery_lbl, "");
    lv_obj_set_style_text_color(battery_lbl, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(battery_lbl, FONT_SMALL, 0);
    lv_obj_align(battery_lbl, LV_ALIGN_TOP_RIGHT, -20, 40);
    lv_obj_remove_flag(battery_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(battery_lbl, LV_OBJ_FLAG_SCROLLABLE);

    // Screen-level gesture handler — instant page switch, bypass slow tileview anim
    lv_obj_add_event_cb(scr_dashboard, [](lv_event_t *e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_LEFT && current_page < PAGE_COUNT - 1) {
            ui_set_page(current_page + 1);
        } else if (dir == LV_DIR_RIGHT && current_page > 0) {
            ui_set_page(current_page - 1);
        } else if (dir == LV_DIR_TOP) {
            // Swipe up → cycle to next AI tool
            ui_cycle_tool();
        } else if (dir == LV_DIR_BOTTOM) {
            // Swipe down → wake (or take to system page)
            if (power_get_state() != PowerState::ACTIVE) {
                power_force_state(PowerState::ACTIVE);
            } else {
                ui_set_page(PAGE_COUNT - 1);  // jump to system page
            }
        }
    }, LV_EVENT_GESTURE, nullptr);

    // Double-tap to wake from dim/off
    lv_obj_add_event_cb(scr_dashboard, [](lv_event_t *e) {
        static unsigned long lastTap = 0;
        unsigned long now = millis();
        if (now - lastTap < 400) {
            // Double-tap detected
            power_force_state(PowerState::ACTIVE);
            lastTap = 0;
        } else {
            lastTap = now;
        }
        power_register_activity();
    }, LV_EVENT_CLICKED, nullptr);

    // Long-press (500ms) to open settings page
    lv_obj_add_event_cb(scr_dashboard, [](lv_event_t *e) {
        Serial.println("[GEST] long-press: open settings");
        // Placeholder: cycle to next page as feedback for now
        int next = (current_page + 1) % PAGE_COUNT;
        ui_set_page(next);
    }, LV_EVENT_LONG_PRESSED, nullptr);

    Serial.printf("[DBG] dashboard ready heap=%lu, loading screen\n", ESP.getFreeHeap());
    lv_screen_load(scr_dashboard);
    Serial.println("[DBG] dashboard loaded");
}

// ── Claude Code pixel mascot (animated) ──
// The little blocky creature, drawn from a pixel grid as filled rectangles
// (one rect per horizontal run). It hops with a small translate_y animation —
// only this ~65px region redraws, so it never approaches the full-screen flush
// limit that froze the tileview. Body cells recolor per tool; eyes stay black.
// 'B' = body, 'E' = eye, '.' = transparent.
static const char *PET_GRID[] = {
    "...BBBBBBBB...",   // rounded top (corners cut)
    ".BBBBBBBBBBBB.",
    "BBBBBBBBBBBBBB",   // widest body
    "BBBEBBBBBBEBBB",   // eyes — symmetric: col3 <-> col10 (14-wide, axis 6.5)
    "BBBEBBBBBBEBBB",
    "BBBBBBBBBBBBBB",
    ".BBBBBBBBBBBB.",   // rounded bottom
    ".BB.BB..BB.BB.",   // 4 little feet, symmetric (1-2,4-5 <-> 8-9,11-12)
    ".BB.BB..BB.BB.",
};
#define PET_ROWS 9
#define PET_COLS 14

static lv_obj_t *build_claude_pet(lv_obj_t *parent, int cell) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, PET_COLS * cell, PET_ROWS * cell);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    for (int r = 0; r < PET_ROWS; r++) {
        const char *row = PET_GRID[r];
        int c = 0;
        while (c < PET_COLS) {
            char ch = row[c];
            if (ch == '.') { c++; continue; }
            int start = c;
            while (c < PET_COLS && row[c] == ch) c++;   // extend run
            int len = c - start;
            lv_obj_t *px = lv_obj_create(cont);
            lv_obj_set_size(px, len * cell, cell);
            lv_obj_set_pos(px, start * cell, r * cell);
            lv_obj_set_style_radius(px, 0, 0);
            lv_obj_set_style_border_width(px, 0, 0);
            lv_obj_set_style_pad_all(px, 0, 0);
            lv_obj_set_style_bg_opa(px, LV_OPA_COVER, 0);
            lv_obj_remove_flag(px, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(px, LV_OBJ_FLAG_CLICKABLE);
            if (ch == 'E') {
                lv_obj_set_style_bg_color(px, lv_color_black(), 0);
                lv_obj_set_user_data(px, (void *)0);   // eye — never recolor
            } else {
                lv_obj_set_style_bg_color(px, CLR_CLAUDE, 0);
                lv_obj_set_user_data(px, (void *)1);   // body — recolor per tool
            }
        }
    }
    return cont;
}

// Recolor body cells (eyes stay black).
static void spark_set_color(lv_obj_t *pet, lv_color_t c) {
    if (!pet) return;
    uint32_t n = lv_obj_get_child_count(pet);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(pet, i);
        if (lv_obj_get_user_data(ch) != (void *)0)
            lv_obj_set_style_bg_color(ch, c, 0);
    }
}

static void pet_opa_cb(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void start_spark_breath(lv_obj_t *pet) {
    // Intentionally static. Any continuous animation here forces per-frame
    // partial redraws whose flush artifacts accumulate as on-screen "花花" that
    // only a full repaint clears. A still mascot keeps the screen clean with no
    // periodic full-refresh needed.
    (void)pet;
    (void)pet_opa_cb;
}

// Smoothly animate an arc to a value — but ONLY for small deltas. A large sweep
// invalidates the arc's huge bounding box every frame (the full-screen flush
// that stalls this display), so snap instead.
static void arc_anim_exec(void *obj, int32_t v) { lv_arc_set_value((lv_obj_t *)obj, v); }

static void arc_anim_to(lv_obj_t *arc, int32_t target) {
    // Direct set — no animation. Continuous arc animation drives repeated partial
    // redraws which accumulate flush artifacts ("花花") on this hold-type AMOLED.
    if (arc) lv_arc_set_value(arc, target);
    (void)arc_anim_exec;
}

// Continuous fuel gauge color by REMAINING %: smooth red(0)→amber→green(100),
// so every percentage point has a distinct hue (no threshold steps).
static lv_color_t fuel_color(int rem) {
    if (rem < 0) rem = 0; else if (rem > 100) rem = 100;
    uint16_t h = (uint16_t)(rem * 120 / 100);   // 0°=red (empty) … 120°=green (full)
    return lv_color_hsv_to_rgb(h, 85, 100);
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
    lv_obj_remove_flag(ov_arc_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_color(ov_arc_main, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ov_arc_main, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ov_arc_main, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ov_arc_main, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ov_arc_main, true, LV_PART_INDICATOR);
    lv_obj_center(ov_arc_main);

    // Inner ring = 5h window (the tighter, more urgent limit). Concentric,
    // slightly smaller; colored by severity in ui_update_status.
    ov_arc_5h = lv_arc_create(tile_overview);
    lv_obj_set_size(ov_arc_5h, 300, 300);   // r≈150 vs outer r≈200 → ~50px band for the 7d readout
    lv_arc_set_rotation(ov_arc_5h, 135);
    lv_arc_set_bg_angles(ov_arc_5h, 0, 270);
    lv_arc_set_range(ov_arc_5h, 0, 100);
    lv_arc_set_value(ov_arc_5h, 0);
    lv_obj_remove_flag(ov_arc_5h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov_arc_5h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_color(ov_arc_5h, CLR_SURFACE_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ov_arc_5h, CLR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ov_arc_5h, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ov_arc_5h, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ov_arc_5h, true, LV_PART_INDICATOR);
    lv_obj_center(ov_arc_5h);

    // Top readouts — the two ring windows, big and colored by usage level.
    // "7d" (outer ring) sits above "5h" (inner ring), mirroring the ring order.
    // 7d readout NESTED in the outer ring's band (between the two arcs, top) so
    // it visually belongs to the outer ring. 5h readout sits just inside the
    // inner ring — each number maps to its own ring.
    ov_rd_7d = lv_label_create(tile_overview);
    lv_label_set_text(ov_rd_7d, "");
    lv_obj_set_style_text_font(ov_rd_7d, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ov_rd_7d, CLR_TEXT_MUTED, 0);
    lv_obj_align(ov_rd_7d, LV_ALIGN_CENTER, 0, -174);   // outer-ring band (7d)

    ov_rd_5h = lv_label_create(tile_overview);
    lv_label_set_text(ov_rd_5h, "");
    lv_obj_set_style_text_font(ov_rd_5h, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ov_rd_5h, CLR_TEXT_MUTED, 0);
    lv_obj_align(ov_rd_5h, LV_ALIGN_CENTER, 0, -120);   // just inside inner ring (5h)

    // Claude Code pixel mascot — centered, static (no animation → no flush ghosts).
    ov_spark = build_claude_pet(tile_overview, 6);   // 14×9 grid → 84×54 px
    lv_obj_align(ov_spark, LV_ALIGN_CENTER, 0, -48);
    start_spark_breath(ov_spark);

    // Tool name (ASCII → large Montserrat)
    ov_lbl_tool = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_tool, "Idle");
    lv_obj_set_style_text_color(ov_lbl_tool, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ov_lbl_tool, &lv_font_montserrat_28, 0);
    lv_obj_align(ov_lbl_tool, LV_ALIGN_CENTER, 0, 22);

    // Big invisible tap target over the entire top half of the tile — taps cycle tools.
    // Sits ABOVE all other widgets, but doesn't block tileview drag (we forward release
    // to scroll handling via setting parent's scroll dir).
    lv_obj_t *tap_target = lv_obj_create(tile_overview);
    lv_obj_set_size(tap_target, SCREEN_WIDTH, SCREEN_HEIGHT / 2);
    lv_obj_align(tap_target, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(tap_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap_target, 0, 0);
    lv_obj_set_style_pad_all(tap_target, 0, 0);
    lv_obj_remove_flag(tap_target, LV_OBJ_FLAG_SCROLLABLE);
    // PRESSED fires on touch-down (no wait for release), but debounce 200ms
    // so a press-and-drag doesn't cycle multiple tools.
    lv_obj_add_event_cb(tap_target, [](lv_event_t *e) {
        static unsigned long lastCycle = 0;
        if (millis() - lastCycle < 200) return;
        lastCycle = millis();
        ui_cycle_tool();
    }, LV_EVENT_PRESSED, nullptr);

    // Model + plan (secondary line under the tool name)
    ov_lbl_model = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_model, "");
    lv_obj_set_style_text_color(ov_lbl_model, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ov_lbl_model, &lv_font_montserrat_16, 0);
    lv_obj_align(ov_lbl_model, LV_ALIGN_CENTER, 0, 50);

    // Task-state badge (+ running session count)
    ov_lbl_status = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_status, "");
    lv_obj_set_style_text_color(ov_lbl_status, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ov_lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_align(ov_lbl_status, LV_ALIGN_CENTER, 0, 78);

    // Mini-tool dot row: one colored dot per active AI tool
    mini_tools_row = lv_obj_create(tile_overview);
    lv_obj_set_size(mini_tools_row, 200, 20);
    lv_obj_align(mini_tools_row, LV_ALIGN_CENTER, 0, 104);
    lv_obj_set_style_bg_opa(mini_tools_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mini_tools_row, 0, 0);
    lv_obj_set_style_pad_all(mini_tools_row, 0, 0);
    lv_obj_set_flex_flow(mini_tools_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mini_tools_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mini_tools_row, 10, 0);
    lv_obj_remove_flag(mini_tools_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(mini_tools_row, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 6; i++) {
        mini_tool_dots[i] = lv_obj_create(mini_tools_row);
        lv_obj_set_size(mini_tool_dots[i], 12, 12);
        lv_obj_set_style_radius(mini_tool_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(mini_tool_dots[i], 0, 0);
        lv_obj_set_style_bg_color(mini_tool_dots[i], CLR_TEXT_MUTED, 0);
        lv_obj_set_style_pad_all(mini_tool_dots[i], 0, 0);
        lv_obj_add_flag(mini_tool_dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mini_tool_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

// ── Tool detail page ──

static void create_detail_tile() {
    td_lbl_title = lv_label_create(tile_tool_detail);
    lv_label_set_text(td_lbl_title, "Tool Details");
    lv_obj_set_style_text_color(td_lbl_title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(td_lbl_title, FONT_LARGE, 0);
    lv_obj_align(td_lbl_title, LV_ALIGN_TOP_MID, 0, 80);

    td_arc_usage = lv_arc_create(tile_tool_detail);
    lv_obj_set_size(td_arc_usage, 160, 160);
    lv_arc_set_rotation(td_arc_usage, 135);
    lv_arc_set_bg_angles(td_arc_usage, 0, 270);
    lv_arc_set_range(td_arc_usage, 0, 100);
    lv_arc_set_value(td_arc_usage, 0);
    lv_obj_remove_flag(td_arc_usage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(td_arc_usage, LV_OBJ_FLAG_SCROLLABLE);
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
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
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
    // Memory degradation: skip updates entirely when heap critically low
    // to give recovery a chance. Status will resume on next refresh.
    if (mem_is_low()) {
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 10000) {
            lastWarn = millis();
            Serial.printf("[UI] Low memory %lu KB — skipping update\n",
                          mem_get_free_heap() / 1024);
        }
        return;
    }

    // De-dupe: hash the payload, skip if unchanged from last call
    uint32_t h = 5381;
    const char *at = payload["active_tool"] | "";
    for (const char *p = at; *p; p++) h = ((h << 5) + h) + (uint32_t)*p;
    JsonObject t = payload["tools"];
    if (!t.isNull()) {
        for (JsonPair kv : t) {
            JsonObject td = kv.value().as<JsonObject>();
            const char *tk = td["tokens_display"] | "";
            const char *ck = td["cost_display"] | "";
            const char *task = td["task"] | "";
            const char *u5 = td["u5"] | "";
            const char *u7 = td["u7"] | "";
            for (const char *p = tk; *p; p++) h = ((h << 5) + h) + (uint32_t)*p;
            for (const char *p = ck; *p; p++) h = ((h << 5) + h) + (uint32_t)*p;
            for (const char *p = task; *p; p++) h = ((h << 5) + h) + (uint32_t)*p;
            for (const char *p = u5; *p; p++) h = ((h << 5) + h) + (uint32_t)*p;
            for (const char *p = u7; *p; p++) h = ((h << 5) + h) + (uint32_t)*p;
            h = ((h << 5) + h) + (uint32_t)(td["usage_pct"] | 0);
        }
    }
    JsonObject s = payload["system"];
    if (!s.isNull()) {
        h = ((h << 5) + h) + (uint32_t)(s["cpu_pct"] | 0);
        h = ((h << 5) + h) + (uint32_t)(s["mem_pct"] | 0);
    }
    if (h == g_status_hash && !g_force_status_redraw) return;  // nothing changed, skip redraw
    g_status_hash = h;
    g_force_status_redraw = false;

    const char *activeTool = payload["active_tool"];

    // -- Cache the list of tools in payload so swipe-up can cycle through them --
    JsonObject tools = payload["tools"];
    available_tools_count = 0;
    for (JsonPair kv : tools) {
        if (available_tools_count >= 6) break;
        strlcpy(available_tools[available_tools_count], kv.key().c_str(), 24);
        available_tools_count++;
    }

    // Update mini-tool dot row: one dot per active tool, colored by tool theme,
    // larger if it's the currently selected one.
    update_mini_dots();
    // If selected_tool not in the list, fall back to active_tool
    bool selected_valid = false;
    for (int i = 0; i < available_tools_count; i++) {
        if (strcmp(available_tools[i], selected_tool) == 0) { selected_valid = true; break; }
    }
    if (!selected_valid && activeTool && strcmp(activeTool, "idle") != 0) {
        strlcpy(selected_tool, activeTool, sizeof(selected_tool));
    }

    // Use selected_tool for display (falls back to active_tool)
    const char *displayedTool = selected_tool[0] ? selected_tool : activeTool;

    // -- Update overview page --
    if (displayedTool && strcmp(displayedTool, "idle") != 0 && tools.containsKey(displayedTool)) {
        JsonObject tool = tools[displayedTool];
        activeTool = displayedTool;  // alias for downstream code

        current_tool_color = theme_tool_color(activeTool);

        // 5h window % + its severity color (shared by the inner ring and the
        // color-coded "5h" label below).
        int p5 = tool["p5"] | 0;
        lv_color_t c5 = fuel_color(100 - p5);   // gradient by 5h remaining

        // Display name mapping
        const char *displayName = tool_display_name(activeTool);

        lv_label_set_text(ov_lbl_tool, displayName);
        lv_obj_set_style_text_color(ov_lbl_tool, current_tool_color, 0);

        // Spark: recolor to the active tool, ensure it's visible.
        spark_set_color(ov_spark, current_tool_color);
        if (ov_spark) lv_obj_remove_flag(ov_spark, LV_OBJ_FLAG_HIDDEN);

        // Task-state badge: Working / Waiting / Idle + how many CLI sessions run.
        const char *taskState = tool["task"] | "";
        const char *status = tool["status"] | "unknown";
        int tasks = tool["tasks"] | 0;
        const char *sym; const char *word; lv_color_t badgeCol;
        if (strcmp(taskState, "working") == 0)      { sym = LV_SYMBOL_PLAY;  word = "Working"; badgeCol = CLR_SUCCESS; }
        else if (strcmp(taskState, "waiting") == 0) { sym = LV_SYMBOL_OK;    word = "Waiting"; badgeCol = CLR_WARNING; }
        else if (strcmp(taskState, "idle") == 0)    { sym = LV_SYMBOL_PAUSE; word = "Idle";    badgeCol = CLR_TEXT_MUTED; }
        else {
            bool act = (strcmp(status, "active") == 0);
            sym = act ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
            word = act ? "Active" : "Idle";
            badgeCol = act ? CLR_SUCCESS : CLR_TEXT_MUTED;
        }
        if (tasks > 1)
            lv_label_set_text_fmt(ov_lbl_status, "%s %s    %d running", sym, word, tasks);
        else
            lv_label_set_text_fmt(ov_lbl_status, "%s %s", sym, word);
        lv_obj_set_style_text_color(ov_lbl_status, badgeCol, 0);

        const char *model = tool["model"] | "";
        lv_label_set_text(ov_lbl_model, model);

        // Rings show REMAINING quota (fuel-gauge): full = plenty left, empty =
        // nearly out. Numbers are the remaining %. Color still by severity
        // (little left = red), so low fuel reads as urgent.
        int usagePct = tool["usage_pct"] | 0;   // 7d used
        int rem7 = 100 - usagePct;              // 7d remaining
        int rem5 = 100 - p5;                    // 5h remaining
        lv_color_t c7 = fuel_color(rem7);       // gradient by 7d remaining
        lv_label_set_text_fmt(ov_rd_7d, "7d  %d%%", rem7);
        lv_obj_set_style_text_color(ov_rd_7d, c7, 0);
        lv_label_set_text_fmt(ov_rd_5h, "5h  %d%%", rem5);
        lv_obj_set_style_text_color(ov_rd_5h, c5, 0);

        arc_anim_to(ov_arc_main, rem7);
        lv_obj_set_style_arc_color(ov_arc_main, c7, LV_PART_INDICATOR);
        arc_anim_to(ov_arc_5h, rem5);
        lv_obj_set_style_arc_color(ov_arc_5h, c5, LV_PART_INDICATOR);

        // -- Update detail page --
        if (td_lbl_title) {
            lv_label_set_text(td_lbl_title, displayName);
            lv_obj_set_style_text_color(td_lbl_title, current_tool_color, 0);
        }
        if (td_arc_usage) {
            arc_anim_to(td_arc_usage, usagePct);
            lv_obj_set_style_arc_color(td_arc_usage, current_tool_color, LV_PART_INDICATOR);
        }
        if (td_lbl_usage_pct) lv_label_set_text_fmt(td_lbl_usage_pct, "%d%%", usagePct);

        const char *task = tool["current_task"] | "";
        if (td_lbl_task) lv_label_set_text(td_lbl_task, task[0] ? task : "No active task");

        int sessions = tool["session_count"] | 0;
        if (td_lbl_session) lv_label_set_text_fmt(td_lbl_session, "%d session%s", sessions, sessions != 1 ? "s" : "");

        int uptime = tool["uptime_min"] | 0;
        if (td_lbl_uptime) {
            if (uptime > 0) lv_label_set_text_fmt(td_lbl_uptime, "%d min uptime", uptime);
            else lv_label_set_text(td_lbl_uptime, "");
        }

    } else {
        lv_label_set_text(ov_lbl_tool, "Idle");
        lv_obj_set_style_text_color(ov_lbl_tool, CLR_TEXT_PRIMARY, 0);
        lv_label_set_text(ov_lbl_status, "No active tools");
        lv_obj_set_style_text_color(ov_lbl_status, CLR_TEXT_MUTED, 0);
        lv_label_set_text(ov_lbl_model, "");
        if (ov_rd_7d) lv_label_set_text(ov_rd_7d, "");
        if (ov_rd_5h) lv_label_set_text(ov_rd_5h, "");
        if (ov_spark) lv_obj_add_flag(ov_spark, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(ov_arc_main, 0);
        if (ov_arc_5h) lv_arc_set_value(ov_arc_5h, 0);

        if (td_lbl_title) lv_label_set_text(td_lbl_title, "No Tool Active");
        if (td_arc_usage) lv_arc_set_value(td_arc_usage, 0);
        if (td_lbl_usage_pct) lv_label_set_text(td_lbl_usage_pct, "—");
        if (td_lbl_task) lv_label_set_text(td_lbl_task, "");
        if (td_lbl_session) lv_label_set_text(td_lbl_session, "");
        if (td_lbl_uptime) lv_label_set_text(td_lbl_uptime, "");
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

    // Repaint the whole screen in the SAME render pass that applied these
    // changes. On this hold-type AMOLED a bare partial redraw leaves stale
    // pixels ("花花"); doing a full invalidate right after the (infrequent)
    // content change overwrites them before they're ever shown — clean, and no
    // blind periodic flash since the screen is otherwise static.
    lv_obj_invalidate(lv_screen_active());
}

void ui_set_page(int page_index) {
    if (!tv || page_index < 0 || page_index >= PAGE_COUNT) return;
    lv_obj_t *tiles[] = {tile_overview, tile_tool_detail, tile_system};
    // LV_ANIM_OFF: instant — swipe animation is too slow on this display + complex widgets
    lv_tileview_set_tile(tv, tiles[page_index], LV_ANIM_OFF);
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

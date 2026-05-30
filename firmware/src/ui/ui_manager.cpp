#include "ui_manager.h"
#include "theme.h"
#include "notification.h"
#include "settings_page.h"
#include "ota_page.h"
#include "diagnostic_page.h"
#include "../display/display.h"
#include "../system/i18n.h"
#include "../system/settings_manager.h"
#include "../system/watchdog.h"
#include "../system/power_manager.h"
#include "../network/ws_client.h"
#include "../network/serial_config.h"
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
// Utility screens, built lazily on first navigation (saves heap until needed).
static lv_obj_t *scr_settings = nullptr;
static lv_obj_t *scr_diag = nullptr;
static lv_obj_t *scr_ota = nullptr;

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
static lv_obj_t *ov_tag_7d = nullptr;      // ring identity label: outer = 7d
static lv_obj_t *ov_tag_5h = nullptr;      // ring identity label: inner = 5h

// ── Tool detail page widgets ──
static lv_obj_t *td_lbl_title = nullptr;
static lv_obj_t *td_lbl_task = nullptr;
static lv_obj_t *td_lbl_session = nullptr;
static lv_obj_t *td_lbl_uptime = nullptr;
static lv_obj_t *td_arc_usage = nullptr;
static lv_obj_t *td_lbl_usage_pct = nullptr;

// ── System page widgets ──
static lv_obj_t *sys_arc_cpu = nullptr;
static lv_obj_t *sys_arc_gpu = nullptr;
static lv_obj_t *sys_arc_mem = nullptr;

// ── Always-visible battery indicator (top-right of dashboard) ──
static lv_obj_t *battery_lbl = nullptr;

// ── Mini-tool indicator row on Overview ──
static lv_obj_t *mini_tools_row = nullptr;
static lv_obj_t *mini_tool_dots[6] = {nullptr};
static lv_obj_t *sys_lbl_cpu = nullptr;
static lv_obj_t *sys_lbl_gpu = nullptr;
static lv_obj_t *sys_lbl_mem = nullptr;
static lv_obj_t *sys_lbl_net = nullptr;   // repurposed: temp + disk footer
static lv_obj_t *sys_lbl_link = nullptr;  // connection mode: USB / WiFi / offline

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
    // Connection mode indicator (System page): which transport is live right now.
    if (sys_lbl_link) {
        const char *mode; lv_color_t c = CLR_TEXT_MUTED;
        if (serial_transport_is_active())      mode = "Link: USB";
        else if (ws_client_is_connected())     mode = "Link: WiFi";
        else                                 { mode = "Link: offline"; c = CLR_WARNING; }
        lv_label_set_text(sys_lbl_link, mode);
        lv_obj_set_style_text_color(sys_lbl_link, c, 0);
    }
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
static void set_tool_icon(const char *tool, lv_color_t color);

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
        set_tool_icon(selected_tool, c);
    }
    update_mini_dots();
    // No full-screen invalidate needed: the rounder keeps per-widget partial
    // redraws clean. Just force the next status to redraw even if data is same.
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

    // Long-press (500ms) opens the settings screen (→ diagnostics / OTA from there).
    lv_obj_add_event_cb(scr_dashboard, [](lv_event_t *e) {
        Serial.println("[GEST] long-press: open settings");
        ui_open_settings();
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

// Per-tool pixel emblems, same blocky aesthetic as the Claude creature.
// 'B' = body (recolored to the tool color), 'E' = eye (stays black), '.' = empty.
struct PixelIcon { const char *const *grid; int rows; int cols; };

// Codex → an OpenAI-style nested hexagonal "knot" (approximation).
static const char *CODEX_GRID[] = {
    ".BBBBBBB.",
    "B.......B",
    "B..BBB..B",
    "B.B...B.B",
    "B.B...B.B",
    "B.B...B.B",
    "B..BBB..B",
    "B.......B",
    ".BBBBBBB.",
};
// Gemini → a 4-point gem / sparkle.
static const char *GEMINI_GRID[] = {
    "....B....",
    "...BBB...",
    "..BBBBB..",
    ".BBBBBBB.",
    "BBBBBBBBB",
    ".BBBBBBB.",
    "..BBBBB..",
    "...BBB...",
    "....B....",
};

static const PixelIcon ICON_CLAUDE = { PET_GRID,    PET_ROWS, PET_COLS };
static const PixelIcon ICON_CODEX  = { CODEX_GRID,  9, 9 };
static const PixelIcon ICON_GEMINI = { GEMINI_GRID, 9, 9 };
static const PixelIcon *g_current_icon = nullptr;   // currently-built emblem

static const PixelIcon *icon_for_tool(const char *tool) {
    if (tool && strstr(tool, "codex"))  return &ICON_CODEX;
    if (tool && strstr(tool, "gemini")) return &ICON_GEMINI;
    return &ICON_CLAUDE;   // claude_code + fallback
}

static lv_obj_t *build_pixel_icon(lv_obj_t *parent, const PixelIcon *ic, int cell) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, ic->cols * cell, ic->rows * cell);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    for (int r = 0; r < ic->rows; r++) {
        const char *row = ic->grid[r];
        int c = 0;
        while (c < ic->cols) {
            char ch = row[c];
            if (ch == '.') { c++; continue; }
            int start = c;
            while (c < ic->cols && row[c] == ch) c++;   // extend run
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

static lv_obj_t *build_claude_pet(lv_obj_t *parent, int cell) {
    g_current_icon = &ICON_CLAUDE;
    return build_pixel_icon(parent, &ICON_CLAUDE, cell);
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
    // Gentle opacity breathing on the mascot. Now safe to animate: the CO5300
    // invalidate-area rounder (display.cpp) keeps the small per-frame partial
    // redraws of the mascot's bounding box clean, so no "花花" accumulates.
    if (!pet) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pet);
    lv_anim_set_exec_cb(&a, pet_opa_cb);
    lv_anim_set_values(&a, 150, 255);
    lv_anim_set_duration(&a, 1400);
    lv_anim_set_playback_duration(&a, 1400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

// Swap the centre emblem to the active tool's icon (rebuild only when it
// actually changes — infrequent), then tint it to the tool color.
static void set_tool_icon(const char *tool, lv_color_t color) {
    const PixelIcon *ic = icon_for_tool(tool);
    if (ic != g_current_icon && ov_spark) {
        lv_anim_delete(ov_spark, nullptr);   // drop the old breath anim first
        lv_obj_delete(ov_spark);
        ov_spark = build_pixel_icon(tile_overview, ic, 4);
        lv_obj_align(ov_spark, LV_ALIGN_CENTER, 0, -116);
        start_spark_breath(ov_spark);
        g_current_icon = ic;
    }
    spark_set_color(ov_spark, color);
}

// Smoothly animate an arc to its new value. Clean now that dirty rects are
// aligned by the rounder — a moving ring no longer smears.
static void arc_anim_exec(void *obj, int32_t v) { lv_arc_set_value((lv_obj_t *)obj, v); }

static void arc_anim_to(lv_obj_t *arc, int32_t target) {
    if (!arc) return;
    int32_t cur = lv_arc_get_value(arc);
    if (cur == target) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_anim_exec);
    lv_anim_set_values(&a, cur, target);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// Calm gauge color. The arc LENGTH already conveys "how much is left", so the
// fill stays a quiet cool-white in the healthy range (no green glare — bright
// saturated greens bloom badly on this AMOLED). Color is reserved for WARNING:
// amber when low, red when nearly empty. Used by the system gauges.
static lv_color_t fuel_color(int rem) {
    if (rem < 0) rem = 0; else if (rem > 100) rem = 100;
    if (rem < 15) return lv_color_hex(0xCE5648);   // red — nearly empty
    if (rem < 30) return lv_color_hex(0xF0A024);   // amber — distinct from the calm resting tone
    return lv_color_hex(0xC4CCDA);                 // calm cool white — toned down to limit AMOLED bloom
}

// Overview ring color. Same calm/warn logic, but the two concentric rings get
// DISTINCT identities so they're never confused: the inner 5h ring (the hero,
// tighter limit) is a bright cool white; the outer 7d ring is a dim slate. Both
// turn amber/red together when low.
static lv_color_t ring_color(int rem, bool hero) {
    if (rem < 0) rem = 0; else if (rem > 100) rem = 100;
    if (rem < 15) return lv_color_hex(0xCE5648);   // red — nearly empty
    if (rem < 30) return lv_color_hex(0xF0A024);   // amber — running low
    return hero ? lv_color_hex(0xC4CCDA)           // inner 5h: cool white (toned to limit bloom)
                : lv_color_hex(0x7A8A9A);          // outer 7d: slate (brightened so it reads on black)
}

// Compact reset countdown: 5h window → "1h 44m"; 7d window → "4d 6h".
static void fmt_reset(uint32_t secs, char *buf, size_t n) {
    if (secs == 0) { snprintf(buf, n, "--"); return; }
    uint32_t d = secs / 86400, h = (secs % 86400) / 3600, m = (secs % 3600) / 60;
    if (d > 0)      snprintf(buf, n, "%lud %luh", (unsigned long)d, (unsigned long)h);
    else if (h > 0) snprintf(buf, n, "%luh %lum", (unsigned long)h, (unsigned long)m);
    else            snprintf(buf, n, "%lum", (unsigned long)m);
}

// ── Overview page ──

static void create_overview_tile() {
    // Outer arc (usage)
    ov_arc_main = lv_arc_create(tile_overview);
    lv_obj_set_size(ov_arc_main, 412, 412);          // r=206 OUTER ring = 7d, gap at bottom
    lv_arc_set_rotation(ov_arc_main, 135);
    lv_arc_set_bg_angles(ov_arc_main, 0, 270);
    lv_arc_set_range(ov_arc_main, 0, 100);
    lv_arc_set_value(ov_arc_main, 0);
    lv_obj_remove_flag(ov_arc_main, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov_arc_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_color(ov_arc_main, lv_color_hex(0x242730), LV_PART_MAIN);   // subtle track (reads as a gauge, not void)
    lv_obj_set_style_arc_color(ov_arc_main, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ov_arc_main, 8, LV_PART_MAIN);    // outer 7d: thin
    lv_obj_set_style_arc_width(ov_arc_main, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ov_arc_main, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ov_arc_main, true, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov_arc_main, LV_OPA_TRANSP, LV_PART_KNOB);   // hide slider knob dot
    lv_obj_center(ov_arc_main);

    // Inner ring = 5h window (the tighter, more urgent limit). Concentric,
    // slightly smaller; colored by severity in ui_update_status.
    ov_arc_5h = lv_arc_create(tile_overview);
    lv_obj_set_size(ov_arc_5h, 366, 366);            // r=183 INNER ring = 5h, hugs the outer 7d ring (~14px gap)
    lv_arc_set_rotation(ov_arc_5h, 135);
    lv_arc_set_bg_angles(ov_arc_5h, 0, 270);
    lv_arc_set_range(ov_arc_5h, 0, 100);
    lv_arc_set_value(ov_arc_5h, 0);
    lv_obj_remove_flag(ov_arc_5h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov_arc_5h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_color(ov_arc_5h, lv_color_hex(0x242730), LV_PART_MAIN);   // subtle track
    lv_obj_set_style_arc_color(ov_arc_5h, CLR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ov_arc_5h, 10, LV_PART_MAIN);   // inner 5h: slightly bolder than 7d
    lv_obj_set_style_arc_width(ov_arc_5h, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ov_arc_5h, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ov_arc_5h, true, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov_arc_5h, LV_OPA_TRANSP, LV_PART_KNOB);   // hide slider knob dot
    lv_obj_center(ov_arc_5h);

    // Top readouts sit in each ring's TOP GAP (where there is no arc), so the
    // energy bar can never cover the number and the text is on clean black.
    // "7d" (outer) above "5h" (inner), mirroring the ring order. Format:
    // "<rem>% (<reset> / <window>)", e.g. "57% (1h 44m / 5h)" — recolor makes
    // the % bright and the reset/window detail muted.
    // HERO = 5h remaining, big, centred (within the clear inner zone).
    // HERO = 5h remaining %, big, centred. White when healthy, amber/red when low.
    ov_rd_5h = lv_label_create(tile_overview);
    lv_label_set_recolor(ov_rd_5h, true);
    lv_label_set_text(ov_rd_5h, "");
    lv_obj_set_style_text_font(ov_rd_5h, &lv_font_montserrat_48, 0);   // focal point
    lv_obj_set_style_text_align(ov_rd_5h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ov_rd_5h, lv_color_hex(0xE8EAED), 0);   // near-white, not hot-white (less glare)
    lv_obj_align(ov_rd_5h, LV_ALIGN_CENTER, 0, -16);

    // ov_rd_7d is no longer shown in the centre (7d% moved onto its ring tag to
    // declutter). Kept allocated/empty so older code paths stay null-safe.
    ov_rd_7d = lv_label_create(tile_overview);
    lv_label_set_text(ov_rd_7d, "");
    lv_obj_add_flag(ov_rd_7d, LV_OBJ_FLAG_HIDDEN);

    // 7d label sits AT THE TOP of the outer ring (12 o'clock) with a black backing
    // that notches the arc, so it reads as a complication belonging to the 7d ring
    // — not floating between the bands. The 5h window needs no side tag (it is the
    // hero number + "5h left" sub-line + bright inner ring). Created after the arcs
    // so the backing draws on top and cleanly cuts the stroke.
    ov_tag_7d = lv_label_create(tile_overview);
    lv_label_set_recolor(ov_tag_7d, true);
    lv_label_set_text(ov_tag_7d, "7d");
    lv_obj_set_style_text_font(ov_tag_7d, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(ov_tag_7d, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ov_tag_7d, lv_color_hex(0x7A8A9A), 0);
    lv_obj_set_style_bg_color(ov_tag_7d, lv_color_hex(0x000000), 0);   // notch out the arc behind it
    lv_obj_set_style_bg_opa(ov_tag_7d, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(ov_tag_7d, 7, 0);
    lv_obj_set_style_pad_ver(ov_tag_7d, 2, 0);
    lv_obj_align(ov_tag_7d, LV_ALIGN_CENTER, 0, -202);   // on the outer-ring stroke centerline: (412-8)/2

    ov_tag_5h = lv_label_create(tile_overview);   // retained for null-safety; unused
    lv_label_set_text(ov_tag_5h, "");
    lv_obj_add_flag(ov_tag_5h, LV_OBJ_FLAG_HIDDEN);

    // Claude Code pixel mascot — centered, static (no animation → no flush ghosts).
    ov_spark = build_claude_pet(tile_overview, 4);   // small identity emblem, top of the dial
    lv_obj_align(ov_spark, LV_ALIGN_CENTER, 0, -120);
    start_spark_breath(ov_spark);

    // Tool name (ASCII → large Montserrat). Muted gray so it stays calm — the
    // colored mascot + dot row carry the brand identity.
    ov_lbl_tool = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_tool, "Idle");
    lv_obj_set_style_text_color(ov_lbl_tool, lv_color_hex(0xB3B8C0), 0);   // secondary pillar
    lv_obj_set_style_text_font(ov_lbl_tool, &lv_font_montserrat_20, 0);    // mid-tier (48 hero → 20 name → 16 details)
    lv_obj_align(ov_lbl_tool, LV_ALIGN_CENTER, 0, -92);   // identity, top of dial

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

    // Sub-line under the hero: "5h left" (muted) + reset countdown (secondary).
    ov_lbl_model = lv_label_create(tile_overview);
    lv_label_set_recolor(ov_lbl_model, true);
    lv_label_set_text(ov_lbl_model, "");
    lv_obj_set_style_text_color(ov_lbl_model, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ov_lbl_model, &lv_font_montserrat_16, 0);
    lv_obj_align(ov_lbl_model, LV_ALIGN_CENTER, 0, 34);

    // Task-state badge (+ running session count)
    ov_lbl_status = lv_label_create(tile_overview);
    lv_label_set_text(ov_lbl_status, "");
    lv_obj_set_style_text_color(ov_lbl_status, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ov_lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_align(ov_lbl_status, LV_ALIGN_CENTER, 0, 72);   // task badge

    // Mini-tool dot row: one colored dot per active AI tool
    mini_tools_row = lv_obj_create(tile_overview);
    lv_obj_set_size(mini_tools_row, 200, 20);
    lv_obj_align(mini_tools_row, LV_ALIGN_CENTER, 0, 112);   // bottom gap, below the badge
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
    lv_obj_set_style_arc_color(td_arc_usage, lv_color_hex(0x242730), LV_PART_MAIN);   // match calm track
    lv_obj_set_style_arc_color(td_arc_usage, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(td_arc_usage, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(td_arc_usage, 8, LV_PART_INDICATOR);
    lv_obj_align(td_arc_usage, LV_ALIGN_CENTER, 0, -20);

    td_lbl_usage_pct = lv_label_create(tile_tool_detail);
    lv_label_set_text(td_lbl_usage_pct, "0%");
    lv_obj_set_style_text_color(td_lbl_usage_pct, lv_color_hex(0xE8EAED), 0);
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
    lv_obj_set_style_arc_width(arc, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x242730), LV_PART_MAIN);   // match overview track
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);   // hide slider knob dot
    lv_obj_align(arc, LV_ALIGN_CENTER, x, y);
    return arc;
}

// One gauge of the computer-stats triangle: ring + centred % + name below.
static void make_sys_gauge(lv_obj_t **arc, lv_obj_t **val, const char *name, int x, int y) {
    *arc = create_gauge_arc(tile_system, x, y, 108, CLR_ACCENT);   // r≈54
    *val = lv_label_create(tile_system);
    lv_label_set_text(*val, "--");
    lv_obj_set_style_text_color(*val, lv_color_hex(0xE8EAED), 0);   // near-white, matches overview hero
    lv_obj_set_style_text_font(*val, &lv_font_montserrat_20, 0);
    lv_obj_align(*val, LV_ALIGN_CENTER, x, y);
    lv_obj_t *nm = lv_label_create(tile_system);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_color(nm, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_16, 0);
    lv_obj_align(nm, LV_ALIGN_CENTER, x, y + 64);   // below the ring
}

static void create_system_tile() {
    lv_obj_t *title = lv_label_create(tile_system);
    lv_label_set_text(title, "SYSTEM");
    lv_obj_set_style_text_color(title, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    // Connection mode (USB / WiFi / offline), just under the title.
    sys_lbl_link = lv_label_create(tile_system);
    lv_label_set_text(sys_lbl_link, "");
    lv_obj_set_style_text_color(sys_lbl_link, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sys_lbl_link, &lv_font_montserrat_16, 0);
    lv_obj_align(sys_lbl_link, LV_ALIGN_TOP_MID, 0, 88);

    // Three gauges in a triangle: CPU upper-left, GPU upper-right, MEM lower-centre.
    make_sys_gauge(&sys_arc_cpu, &sys_lbl_cpu, "CPU", -98, -48);
    make_sys_gauge(&sys_arc_gpu, &sys_lbl_gpu, "GPU",  98, -48);
    make_sys_gauge(&sys_arc_mem, &sys_lbl_mem, "MEM",   0,  72);

    // Footer: temperature + disk.
    sys_lbl_net = lv_label_create(tile_system);
    lv_label_set_text(sys_lbl_net, "");
    lv_obj_set_style_text_color(sys_lbl_net, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sys_lbl_net, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(sys_lbl_net, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sys_lbl_net, LV_ALIGN_CENTER, 0, 184);
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

        // 5h window % + the inner-ring color (bright cool white, warns when low).
        int p5 = tool["p5"] | 0;
        lv_color_t c5 = ring_color(100 - p5, true);

        // Display name mapping
        const char *displayName = tool_display_name(activeTool);

        // Name is a calm secondary pillar; the colored mascot + dot row carry brand.
        lv_label_set_text(ov_lbl_tool, displayName);
        lv_obj_set_style_text_color(ov_lbl_tool, lv_color_hex(0xB3B8C0), 0);

        // Emblem: swap to the active tool's icon + recolor, ensure visible.
        set_tool_icon(activeTool, current_tool_color);
        if (ov_spark) lv_obj_remove_flag(ov_spark, LV_OBJ_FLAG_HIDDEN);

        // Task-state badge: Working / Waiting / Idle + how many CLI sessions run.
        const char *taskState = tool["task"] | "";
        const char *status = tool["status"] | "unknown";
        int tasks = tool["tasks"] | 0;
        const char *sym; const char *word; lv_color_t badgeCol;
        // Calm, low-glare badge colors (soft green / muted gold / gray).
        const lv_color_t COL_WORK = lv_color_hex(0x5FA878);
        const lv_color_t COL_WAIT = lv_color_hex(0xC8A86A);
        if (strcmp(taskState, "working") == 0)      { sym = LV_SYMBOL_PLAY;  word = "Working"; badgeCol = COL_WORK; }
        else if (strcmp(taskState, "waiting") == 0) { sym = LV_SYMBOL_OK;    word = "Waiting"; badgeCol = COL_WAIT; }
        else if (strcmp(taskState, "idle") == 0)    { sym = LV_SYMBOL_PAUSE; word = "Idle";    badgeCol = CLR_TEXT_MUTED; }
        else {
            bool act = (strcmp(status, "active") == 0);
            sym = act ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
            word = act ? "Active" : "Idle";
            badgeCol = act ? COL_WORK : CLR_TEXT_MUTED;
        }
        if (tasks > 1)
            lv_label_set_text_fmt(ov_lbl_status, "%s %s    %d running", sym, word, tasks);
        else
            lv_label_set_text_fmt(ov_lbl_status, "%s %s", sym, word);
        lv_obj_set_style_text_color(ov_lbl_status, badgeCol, 0);

        // Rings show REMAINING quota (fuel gauge). HERO = 5h remaining (big,
        // centred); SECONDARY = 7d remaining + reset (small, below). The
        // "5h left · resets …" sub-line repurposes ov_lbl_model.
        int usagePct = tool["usage_pct"] | 0;   // 7d used
        int rem7 = 100 - usagePct;              // 7d remaining
        int rem5 = 100 - p5;                    // 5h remaining
        int hasQuota = tool["hq"] | 0;          // 0 → no real 5h/7d source
        int stale    = tool["st"] | 0;          // 1 → cache stale (>15 min)

        uint32_t r5secs = tool["r5"] | 0;   // seconds until 5h reset
        uint32_t r7secs = tool["r7"] | 0;   // seconds until 7d reset

        if (!hasQuota) {
            // No per-account quota source → honest "--", faint gray rings.
            lv_label_set_text(ov_rd_5h, "--");
            lv_label_set_text(ov_lbl_model, "no quota data");
            lv_obj_set_style_text_color(ov_lbl_model, CLR_TEXT_SECONDARY, 0);
            lv_label_set_text(ov_tag_7d, "#55585F 7d#  --");
            lv_obj_set_style_text_color(ov_rd_5h, CLR_TEXT_MUTED, 0);
            lv_obj_set_style_text_color(ov_tag_7d, CLR_TEXT_MUTED, 0);
            lv_obj_set_style_arc_opa(ov_arc_main, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(ov_arc_5h, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(ov_arc_main, CLR_BORDER, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(ov_arc_5h, CLR_BORDER, LV_PART_INDICATOR);
            arc_anim_to(ov_arc_main, 0);
            arc_anim_to(ov_arc_5h, 0);
        } else {
            char cd5[16], cd7[16];
            fmt_reset(r5secs, cd5, sizeof(cd5));
            fmt_reset(r7secs, cd7, sizeof(cd7));
            // HERO: 5h remaining %, big. Near-white when healthy (brighter than
            // the ring so the number reads as the focal point), amber/red when low.
            lv_color_t heroCol = stale ? CLR_TEXT_MUTED
                                       : (rem5 < 30 ? ring_color(rem5, true) : lv_color_hex(0xE8EAED));
            lv_label_set_text_fmt(ov_rd_5h, "%d%%%s", rem5, stale ? " *" : "");
            lv_obj_set_style_text_color(ov_rd_5h, heroCol, 0);

            // One-shot low-quota toast: fire once when 5h drops to ≤10%, re-arm
            // only after it recovers above 20% (hysteresis → no spam every update).
            static bool warned_low_5h = false;
            if (!stale && rem5 <= 10 && !warned_low_5h) {
                notif_show("5h quota low", NotifType::WARNING, 4000);
                warned_low_5h = true;
            } else if (rem5 > 20) {
                warned_low_5h = false;
            }

            // Sub-line: one calm line — what window + when it resets.
            lv_label_set_text_fmt(ov_lbl_model, "5h left    resets %s", cd5);
            lv_obj_set_style_text_color(ov_lbl_model, CLR_TEXT_SECONDARY, 0);
            // 7d label notched into the top of the outer ring: faint "7d" + value.
            lv_label_set_text_fmt(ov_tag_7d, "#55585F 7d#  %d%%", rem7);
            lv_obj_set_style_text_color(ov_tag_7d, ring_color(rem7, false), 0);
            (void)cd7;
            lv_opa_t bandOpa = stale ? LV_OPA_50 : LV_OPA_COVER;
            lv_obj_set_style_arc_opa(ov_arc_main, bandOpa, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(ov_arc_5h, bandOpa, LV_PART_INDICATOR);
            arc_anim_to(ov_arc_main, rem7);
            lv_obj_set_style_arc_color(ov_arc_main, ring_color(rem7, false), LV_PART_INDICATOR);
            arc_anim_to(ov_arc_5h, rem5);
            lv_obj_set_style_arc_color(ov_arc_5h, c5, LV_PART_INDICATOR);
        }

        // -- Update detail page --
        if (td_lbl_title) {
            lv_label_set_text(td_lbl_title, displayName);
            lv_obj_set_style_text_color(td_lbl_title, current_tool_color, 0);
        }
        if (td_arc_usage) {
            arc_anim_to(td_arc_usage, usagePct);
            // Calm fuel color (cool-white, warns red as usage climbs) — no bloomy
            // brand-orange ring; consistent with the overview/system gauges.
            lv_obj_set_style_arc_color(td_arc_usage, fuel_color(100 - usagePct), LV_PART_INDICATOR);
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
        lv_obj_set_style_text_color(ov_lbl_tool, lv_color_hex(0xB3B8C0), 0);
        lv_label_set_text(ov_lbl_status, "No active tools");
        lv_obj_set_style_text_color(ov_lbl_status, CLR_TEXT_MUTED, 0);
        lv_label_set_text(ov_lbl_model, "");
        if (ov_tag_7d) { lv_label_set_text(ov_tag_7d, "7d"); lv_obj_set_style_text_color(ov_tag_7d, CLR_TEXT_MUTED, 0); }
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
        int cpu  = sys["cpu_pct"]  | 0;
        int mem  = sys["mem_pct"]  | 0;
        int gpu  = sys["gpu_pct"]  | -1;   // -1 = N/A
        int temp = sys["temp_c"]   | -1;   // -1 = N/A
        int disk = sys["disk_pct"] | 0;

        // Usage gauges: green (low) → red (high) via the fuel gradient on (100-usage).
        arc_anim_to(sys_arc_cpu, cpu);
        lv_label_set_text_fmt(sys_lbl_cpu, "%d%%", cpu);
        lv_obj_set_style_arc_color(sys_arc_cpu, fuel_color(100 - cpu), LV_PART_INDICATOR);

        arc_anim_to(sys_arc_mem, mem);
        lv_label_set_text_fmt(sys_lbl_mem, "%d%%", mem);
        lv_obj_set_style_arc_color(sys_arc_mem, fuel_color(100 - mem), LV_PART_INDICATOR);

        if (gpu >= 0) {
            arc_anim_to(sys_arc_gpu, gpu);
            lv_label_set_text_fmt(sys_lbl_gpu, "%d%%", gpu);
            lv_obj_set_style_arc_color(sys_arc_gpu, fuel_color(100 - gpu), LV_PART_INDICATOR);
        } else {
            arc_anim_to(sys_arc_gpu, 0);
            lv_label_set_text(sys_lbl_gpu, "--");
            lv_obj_set_style_arc_color(sys_arc_gpu, CLR_BORDER, LV_PART_INDICATOR);
        }

        // Footer: temperature (— if unavailable, e.g. macOS without a sensor tool) + disk.
        if (temp >= 0) lv_label_set_text_fmt(sys_lbl_net, "temp %dC      disk %d%%", temp, disk);
        else           lv_label_set_text_fmt(sys_lbl_net, "temp --      disk %d%%", disk);
    }

    // The CO5300 invalidate-area rounder (display.cpp) aligns every dirty rect
    // to the panel's even/odd window requirement, so LVGL's own per-widget
    // partial redraws are clean — no full-screen invalidate workaround needed.
}

// ── Utility-screen navigation ──
// Long-press on any utility screen returns home, so the user can never get stuck.
static void util_back_cb(lv_event_t *e) { ui_open_dashboard(); }

static lv_obj_t *make_util_screen(lv_obj_t *(*builder)(lv_obj_t *)) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    builder(scr);
    lv_obj_add_event_cb(scr, util_back_cb, LV_EVENT_LONG_PRESSED, nullptr);
    return scr;
}

void ui_open_settings() {
    if (!scr_settings) scr_settings = make_util_screen(settings_page_create);
    settings_page_refresh();
    lv_screen_load(scr_settings);
    power_register_activity();
}

void ui_open_diagnostics() {
    if (!scr_diag) scr_diag = make_util_screen(diagnostic_page_create);
    diagnostic_page_refresh();
    lv_screen_load(scr_diag);
    power_register_activity();
}

void ui_open_ota() {
    if (!scr_ota) scr_ota = make_util_screen(ota_page_create);
    ota_page_refresh();
    lv_screen_load(scr_ota);
    power_register_activity();
}

void ui_open_dashboard() {
    if (scr_dashboard) lv_screen_load(scr_dashboard);
    power_register_activity();
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

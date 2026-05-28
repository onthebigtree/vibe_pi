#pragma once

#include <lvgl.h>
#include "hal/board.h"

// ── Color Palette (Minimal Dark Theme) ──
#define CLR_BG            lv_color_hex(0x000000)
#define CLR_SURFACE       lv_color_hex(0x111111)
#define CLR_SURFACE_ALT   lv_color_hex(0x1A1A1A)
#define CLR_BORDER        lv_color_hex(0x2A2A2A)

#define CLR_TEXT_PRIMARY   lv_color_hex(0xFFFFFF)
#define CLR_TEXT_SECONDARY lv_color_hex(0x999999)
#define CLR_TEXT_MUTED     lv_color_hex(0x555555)

#define CLR_ACCENT         lv_color_hex(0x64B5F6)
#define CLR_ACCENT_DIM     lv_color_hex(0x1A3A5A)
#define CLR_SUCCESS        lv_color_hex(0x4CAF50)
#define CLR_WARNING        lv_color_hex(0xFFA726)
#define CLR_ERROR          lv_color_hex(0xEF5350)

#define CLR_CLAUDE         lv_color_hex(0xD97757)
#define CLR_CODEX          lv_color_hex(0x10A37F)
#define CLR_GEMINI         lv_color_hex(0x4285F4)

// ── Spacing (proportional) ──
#define SP_XS  (pct_w(1))
#define SP_S   (pct_w(2))
#define SP_M   (pct_w(3))
#define SP_L   (pct_w(5))
#define SP_XL  (pct_w(7))

// ── Custom CJK fonts (GB2312-1 subset, ~3900 chars) ──
// 28px CJK omitted — too large for 3.3MB partition. TITLE falls back to 20px.
LV_FONT_DECLARE(font_zh_14);
LV_FONT_DECLARE(font_zh_20);

// ── Fonts (size-adaptive, CJK-capable) ──
#define FONT_TITLE    (&font_zh_20)
#define FONT_LARGE    (&font_zh_20)
#define FONT_BODY     (&font_zh_20)
#define FONT_SMALL    (&font_zh_14)

// ── Arc dimensions (proportional to screen) ──
#define ARC_OUTER_SIZE   (pct_w(86))
#define ARC_OUTER_WIDTH  (scr_w() > 300 ? 6 : 4)
#define ARC_INNER_SIZE   (pct_w(73))
#define ARC_INNER_WIDTH  (scr_w() > 300 ? 4 : 3)
#define ARC_GAUGE_SIZE   (pct_w(30))

void theme_init();
void theme_set_dark(bool dark);
bool theme_is_dark();
lv_color_t theme_bg();
lv_color_t theme_text();
lv_color_t theme_text_secondary();
lv_color_t theme_surface();
lv_color_t theme_tool_color(const char *tool_name);

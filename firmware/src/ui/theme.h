#pragma once

#include <lvgl.h>

// ── Color Palette (Minimal Dark Theme) ──
#define CLR_BG            lv_color_hex(0x000000)
#define CLR_SURFACE       lv_color_hex(0x111111)
#define CLR_SURFACE_ALT   lv_color_hex(0x1A1A1A)
#define CLR_BORDER        lv_color_hex(0x2A2A2A)

#define CLR_TEXT_PRIMARY   lv_color_hex(0xFFFFFF)
#define CLR_TEXT_SECONDARY lv_color_hex(0x999999)
#define CLR_TEXT_MUTED     lv_color_hex(0x555555)

#define CLR_ACCENT         lv_color_hex(0x64B5F6)  // Blue
#define CLR_ACCENT_DIM     lv_color_hex(0x1A3A5A)
#define CLR_SUCCESS        lv_color_hex(0x4CAF50)
#define CLR_WARNING        lv_color_hex(0xFFA726)
#define CLR_ERROR          lv_color_hex(0xEF5350)

#define CLR_CLAUDE         lv_color_hex(0xD97757)  // Claude orange
#define CLR_CODEX          lv_color_hex(0x10A37F)  // OpenAI green
#define CLR_GEMINI         lv_color_hex(0x4285F4)  // Google blue

// ── Spacing ──
#define SP_XS  4
#define SP_S   8
#define SP_M   16
#define SP_L   24
#define SP_XL  32

// ── Fonts ──
#define FONT_TITLE    &lv_font_montserrat_28
#define FONT_LARGE    &lv_font_montserrat_22
#define FONT_BODY     &lv_font_montserrat_16
#define FONT_SMALL    &lv_font_montserrat_14

// ── Arc dimensions ──
#define ARC_OUTER_SIZE   400
#define ARC_OUTER_WIDTH  6
#define ARC_INNER_SIZE   340
#define ARC_INNER_WIDTH  4

void theme_init();
lv_color_t theme_tool_color(const char *tool_name);

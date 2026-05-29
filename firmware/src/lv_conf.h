#pragma once

#define LV_COLOR_DEPTH 16
#define LV_USE_LOG 0
#define LV_USE_FLOAT 0

// Fonts
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1   // hero token count on the overview

// Widgets
#define LV_USE_LABEL     1
#define LV_USE_ARC       1
#define LV_USE_SPINNER   1
#define LV_USE_BTN       1
#define LV_USE_IMG       1
#define LV_USE_TILEVIEW  1
#define LV_USE_TEXTAREA  1
#define LV_USE_KEYBOARD  1
#define LV_USE_BAR       1

// Memory — board-aware
#ifdef BOARD_HAS_PSRAM
    #define LV_MEM_SIZE (128 * 1024)
#else
    #define LV_MEM_SIZE (48 * 1024)
#endif
#define LV_MEM_ADR 0
#define LV_MEM_CUSTOM 0

// Tick
#define LV_TICK_PERIOD_MS 5

// Animations
#define LV_USE_ANIMATION 1

// Layout
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

// Performance
#define LV_DRAW_SW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_IMG_CACHE_DEF_SIZE 0

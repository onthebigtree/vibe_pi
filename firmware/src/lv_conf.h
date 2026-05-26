#pragma once

#define LV_COLOR_DEPTH 16
#define LV_USE_LOG 0
#define LV_USE_FLOAT 0

// Fonts
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1

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

// Memory (128KB from PSRAM)
#define LV_MEM_SIZE (128 * 1024)
#define LV_MEM_ADR 0
#define LV_MEM_CUSTOM 0

// Tick
#define LV_TICK_PERIOD_MS 5

// Animations
#define LV_USE_ANIMATION 1

// Flex layout
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

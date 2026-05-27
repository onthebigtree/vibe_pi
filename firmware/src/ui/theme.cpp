#include "theme.h"
#include <string.h>

static bool dark_mode = true;

void theme_init() {
    dark_mode = true;
}

void theme_set_dark(bool dark) {
    dark_mode = dark;
}

bool theme_is_dark() {
    return dark_mode;
}

lv_color_t theme_bg() {
    return dark_mode ? lv_color_hex(0x000000) : lv_color_hex(0xF5F5F5);
}

lv_color_t theme_text() {
    return dark_mode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x1A1A1A);
}

lv_color_t theme_text_secondary() {
    return dark_mode ? lv_color_hex(0x999999) : lv_color_hex(0x666666);
}

lv_color_t theme_surface() {
    return dark_mode ? lv_color_hex(0x111111) : lv_color_hex(0xFFFFFF);
}

lv_color_t theme_tool_color(const char *tool_name) {
    if (!tool_name) return CLR_ACCENT;
    if (strstr(tool_name, "claude"))  return CLR_CLAUDE;
    if (strstr(tool_name, "codex"))   return CLR_CODEX;
    if (strstr(tool_name, "gemini"))  return CLR_GEMINI;
    if (strstr(tool_name, "cursor"))  return lv_color_hex(0x00B4D8);
    if (strstr(tool_name, "windsurf")) return lv_color_hex(0x6C5CE7);
    return CLR_ACCENT;
}

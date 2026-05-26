#include "theme.h"
#include <string.h>

void theme_init() {
    // LVGL theme customization can be extended here
}

lv_color_t theme_tool_color(const char *tool_name) {
    if (!tool_name) return CLR_ACCENT;
    if (strstr(tool_name, "claude"))  return CLR_CLAUDE;
    if (strstr(tool_name, "codex"))   return CLR_CODEX;
    if (strstr(tool_name, "gemini"))  return CLR_GEMINI;
    return CLR_ACCENT;
}

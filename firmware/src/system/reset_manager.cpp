#include "reset_manager.h"
#include "settings_manager.h"
#include "health_manager.h"
#include <esp_system.h>

const char *reset_level_name(ResetLevel level) {
    switch (level) {
        case ResetLevel::SOFT_RESTART:  return "Soft Restart";
        case ResetLevel::DISPLAY_RESET: return "Display Reset";
        case ResetLevel::NETWORK_RESET: return "Network Reset";
        case ResetLevel::FACTORY_RESET: return "Factory Reset";
    }
    return "Unknown";
}

void reset_execute(ResetLevel level, bool reboot) {
    Serial.printf("[Reset] Executing L%d: %s\n", (int)level, reset_level_name(level));

    switch (level) {
        case ResetLevel::SOFT_RESTART:
            break;

        case ResetLevel::DISPLAY_RESET:
            settings_reset_display();
            break;

        case ResetLevel::NETWORK_RESET:
            settings_reset_network();
            break;

        case ResetLevel::FACTORY_RESET:
            health_clear_crash_count();
            settings_reset_factory();
            break;
    }

    if (reboot) {
        Serial.println("[Reset] Rebooting...");
        delay(500);
        esp_restart();
    }
}

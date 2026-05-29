#include "health_manager.h"
#include "config.h"
#include "settings_manager.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

static bool safeMode = false;
static char errorLog[ERROR_LOG_MAX_ENTRIES][48];
static uint8_t errorLogHead = 0;
static uint8_t errorLogCount = 0;

// RAM mirror of the crash NVS keys. health_get_data() runs every 30s on the
// main loop; reading these from RAM avoids an NVS flash transaction (and the
// stall it can cause) on the hot path. Kept in sync on record/clear.
static uint32_t cachedCrashCount = 0;
static char cachedLastCrash[48] = "";

void health_init() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    uint32_t crashCount = prefs.getUInt(NVS_CRASH_COUNT, 0);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        crashCount++;
        prefs.putUInt(NVS_CRASH_COUNT, crashCount);

        const char *reasonStr = "unknown";
        switch (reason) {
            case ESP_RST_PANIC:    reasonStr = "panic"; break;
            case ESP_RST_INT_WDT:  reasonStr = "int_wdt"; break;
            case ESP_RST_TASK_WDT: reasonStr = "task_wdt"; break;
            case ESP_RST_WDT:      reasonStr = "wdt"; break;
            default: break;
        }
        prefs.putString(NVS_LAST_CRASH, reasonStr);
        Serial.printf("[Health] Crash detected: %s (count: %u)\n", reasonStr, crashCount);
    }

    if (crashCount >= MAX_CRASH_COUNT_SAFE_MODE) {
        Serial.printf("[Health] Crash count %u >= %d — clearing and continuing\n",
                      crashCount, MAX_CRASH_COUNT_SAFE_MODE);
        prefs.putUInt(NVS_CRASH_COUNT, 0);
        prefs.putString(NVS_LAST_CRASH, "");
        // Don't set safeMode — allow normal boot after clearing
    }

    // Prime the RAM cache from the (possibly just-cleared) NVS state.
    cachedCrashCount = prefs.getUInt(NVS_CRASH_COUNT, 0);
    strlcpy(cachedLastCrash, prefs.getString(NVS_LAST_CRASH, "").c_str(), sizeof(cachedLastCrash));

    prefs.end();
}

SelfTestResult health_run_self_test() {
    Serial.println("[Health] Running POST self-test...");

    // PSRAM check
    if (!psramFound()) {
        Serial.println("[Health] FAIL: PSRAM not found");
        health_record_error("PSRAM_FAIL");
        return SelfTestResult::PSRAM_FAIL;
    }
    Serial.printf("[Health] PSRAM: %u KB free\n", ESP.getFreePsram() / 1024);

    // Flash check
    uint32_t flashSize = ESP.getFlashChipSize();
    if (flashSize < 1024 * 1024) {
        health_record_error("FLASH_FAIL");
        return SelfTestResult::FLASH_FAIL;
    }
    Serial.printf("[Health] Flash: %u MB\n", flashSize / (1024 * 1024));

    // WiFi module check — just verify MAC is readable, don't change mode
    uint8_t mac[6];
    WiFi.macAddress(mac);
    if (mac[0] == 0 && mac[1] == 0 && mac[2] == 0) {
        health_record_error("WIFI_FAIL");
        return SelfTestResult::WIFI_FAIL;
    }
    Serial.println("[Health] WiFi: OK");

    // Display and touch are checked during their init — stubs for now
    Serial.println("[Health] Display: OK (stub)");
    Serial.println("[Health] Touch: OK (stub)");

    Serial.println("[Health] POST: ALL PASS");
    return SelfTestResult::PASS;
}

void health_record_error(const char *error) {
    strlcpy(errorLog[errorLogHead], error, sizeof(errorLog[0]));
    errorLogHead = (errorLogHead + 1) % ERROR_LOG_MAX_ENTRIES;
    if (errorLogCount < ERROR_LOG_MAX_ENTRIES) errorLogCount++;
    Serial.printf("[Health] Error logged: %s\n", error);
}

void health_record_crash(const char *reason) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    uint32_t cnt = prefs.getUInt(NVS_CRASH_COUNT, 0) + 1;
    prefs.putUInt(NVS_CRASH_COUNT, cnt);
    prefs.putString(NVS_LAST_CRASH, reason);
    prefs.end();
    cachedCrashCount = cnt;
    strlcpy(cachedLastCrash, reason, sizeof(cachedLastCrash));
}

void health_clear_crash_count() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUInt(NVS_CRASH_COUNT, 0);
    prefs.putString(NVS_LAST_CRASH, "");
    prefs.end();
    cachedCrashCount = 0;
    cachedLastCrash[0] = '\0';
    safeMode = false;
}

bool health_is_safe_mode() { return safeMode; }

void health_enter_safe_mode() {
    safeMode = true;
    Serial.println("[Health] Entering safe mode");
}

HealthData health_get_data() {
    HealthData d;
    d.uptime_sec = millis() / 1000;
    d.free_heap = ESP.getFreeHeap();
    d.wifi_rssi = WiFi.RSSI();
    d.temperature_c = temperatureRead();
    d.safe_mode = safeMode;
    d.error_count = errorLogCount;

    // Read from the RAM mirror — no NVS access on the 30s report hot path.
    d.crash_count = cachedCrashCount;
    strlcpy(d.last_crash_reason, cachedLastCrash, sizeof(d.last_crash_reason));

    return d;
}

void health_build_report(JsonDocument &doc, const char *device_id) {
    HealthData d = health_get_data();

    doc["v"] = PROTOCOL_VERSION;
    doc["type"] = "health_report";
    doc["ts"] = (unsigned long)(millis() / 1000);

    JsonObject p = doc["payload"].to<JsonObject>();
    p["device_id"]         = device_id;
    p["uptime_sec"]        = d.uptime_sec;
    p["free_heap"]         = d.free_heap;
    p["wifi_rssi"]         = d.wifi_rssi;
    p["temperature_c"]     = d.temperature_c;
    p["crash_count"]       = d.crash_count;
    p["last_crash_reason"] = d.last_crash_reason;
    p["safe_mode"]         = d.safe_mode;
    // Battery / power IC info
    extern class AXP2101 g_axp;
    extern uint16_t axp_voltage();
    extern uint8_t axp_percent();
    extern bool axp_charging();
    extern bool axp_vbus();
    p["battery_mv"]        = axp_voltage();
    p["battery_pct"]       = axp_percent();
    p["charging"]          = axp_charging();
    p["vbus"]              = axp_vbus();

    JsonArray errors = p["errors"].to<JsonArray>();
    uint8_t start = (errorLogCount >= ERROR_LOG_MAX_ENTRIES)
                    ? errorLogHead : 0;
    for (uint8_t i = 0; i < errorLogCount; i++) {
        uint8_t idx = (start + i) % ERROR_LOG_MAX_ENTRIES;
        errors.add(errorLog[idx]);
    }
}

uint32_t health_get_crash_count() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint32_t cnt = prefs.getUInt(NVS_CRASH_COUNT, 0);
    prefs.end();
    return cnt;
}

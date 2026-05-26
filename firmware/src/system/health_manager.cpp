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
        safeMode = true;
        Serial.println("[Health] SAFE MODE ACTIVATED — too many crashes");
    }

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

    // WiFi module check
    if (WiFi.mode(WIFI_STA) == false) {
        health_record_error("WIFI_FAIL");
        return SelfTestResult::WIFI_FAIL;
    }
    WiFi.mode(WIFI_OFF);
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
}

void health_clear_crash_count() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUInt(NVS_CRASH_COUNT, 0);
    prefs.putString(NVS_LAST_CRASH, "");
    prefs.end();
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

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    d.crash_count = prefs.getUInt(NVS_CRASH_COUNT, 0);
    strlcpy(d.last_crash_reason, prefs.getString(NVS_LAST_CRASH, "").c_str(), sizeof(d.last_crash_reason));
    prefs.end();

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

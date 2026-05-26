#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class SelfTestResult : uint8_t {
    PASS = 0,
    DISPLAY_FAIL  = 1,
    TOUCH_FAIL    = 2,
    WIFI_FAIL     = 3,
    FLASH_FAIL    = 4,
    IMU_FAIL      = 5,
    PSRAM_FAIL    = 6,
};

struct HealthData {
    uint32_t uptime_sec;
    uint32_t free_heap;
    int      wifi_rssi;
    float    temperature_c;
    uint32_t crash_count;
    char     last_crash_reason[64];
    bool     safe_mode;
    uint8_t  error_count;
};

void              health_init();
SelfTestResult    health_run_self_test();
void              health_record_error(const char *error);
void              health_record_crash(const char *reason);
void              health_clear_crash_count();
bool              health_is_safe_mode();
void              health_enter_safe_mode();
HealthData        health_get_data();
void              health_build_report(JsonDocument &doc, const char *device_id);
uint32_t          health_get_crash_count();

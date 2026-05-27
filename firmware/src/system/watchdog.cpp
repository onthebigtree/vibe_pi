#include "watchdog.h"
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>

#define MEM_LOW_THRESHOLD (20 * 1024)

static uint32_t minFreeHeap = UINT32_MAX;

void watchdog_init(uint32_t timeout_ms) {
    esp_task_wdt_init(timeout_ms / 1000, true);
    esp_task_wdt_add(nullptr);
    Serial.printf("[WDT] Initialized: %lu ms\n", timeout_ms);
}

void watchdog_feed() {
    esp_task_wdt_reset();

    uint32_t freeNow = esp_get_free_heap_size();
    if (freeNow < minFreeHeap) {
        minFreeHeap = freeNow;
    }
}

uint32_t mem_get_free_heap() {
    return esp_get_free_heap_size();
}

uint32_t mem_get_min_free_heap() {
    return minFreeHeap;
}

bool mem_is_low() {
    return esp_get_free_heap_size() < MEM_LOW_THRESHOLD;
}

void mem_log_stats() {
    Serial.printf("[MEM] Free: %lu KB, Min: %lu KB, PSRAM free: %lu KB\n",
                  esp_get_free_heap_size() / 1024,
                  minFreeHeap / 1024,
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
}

#pragma once

#include <Arduino.h>

void     watchdog_init(uint32_t timeout_ms);
void     watchdog_feed();
uint32_t mem_get_free_heap();
uint32_t mem_get_min_free_heap();
bool     mem_is_low();         // below 20KB threshold
void     mem_log_stats();

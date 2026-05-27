#pragma once

#include <lvgl.h>
#include <stdint.h>

struct DisplayConfig {
    uint16_t width;
    uint16_t height;
    uint16_t radius;         // 0 for square/rectangular
    bool     is_round;
    uint8_t  color_depth;    // 16 or 24
    uint32_t buf_pixel_count; // recommended pixel buffer size
    bool     supports_dimming;
};

class DisplayHAL {
public:
    virtual ~DisplayHAL() = default;
    virtual bool init() = 0;
    virtual void flush(const lv_area_t *area, uint8_t *px_map) = 0;
    virtual void set_brightness(uint8_t pct) = 0;   // 0-100
    virtual void sleep() = 0;
    virtual void wake() = 0;
    virtual const DisplayConfig &config() const = 0;
};

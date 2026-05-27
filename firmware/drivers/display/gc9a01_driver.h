#pragma once

#include "hal/display_hal.h"

class GC9A01Driver : public DisplayHAL {
public:
    GC9A01Driver(uint8_t mosi, uint8_t sck, uint8_t cs,
                 uint8_t dc, uint8_t rst, uint8_t bl);

    bool init() override;
    void flush(const lv_area_t *area, uint8_t *px_map) override;
    void set_brightness(uint8_t pct) override;
    void sleep() override;
    void wake() override;
    const DisplayConfig &config() const override { return _cfg; }

private:
    uint8_t _mosi, _sck, _cs, _dc, _rst, _bl;
    DisplayConfig _cfg;
    void write_cmd(uint8_t cmd);
    void write_data(uint8_t data);
    void write_data_buf(const uint8_t *data, size_t len);
    void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
};

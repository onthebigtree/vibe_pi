#pragma once

#include "hal/display_hal.h"
#include <esp_lcd_panel_io.h>

class CO5300Driver : public DisplayHAL {
public:
    CO5300Driver(uint8_t cs, uint8_t sck, uint8_t d0, uint8_t d1,
                 uint8_t d2, uint8_t d3, int8_t rst, int8_t te);

    bool init() override;
    void flush(const lv_area_t *area, uint8_t *px_map) override;
    void set_brightness(uint8_t pct) override;
    void sleep() override;
    void wake() override;
    const DisplayConfig &config() const override { return _cfg; }

private:
    uint8_t _cs, _sck, _d0, _d1, _d2, _d3;
    int8_t  _rst, _te;
    DisplayConfig _cfg;

    void hw_reset();
    void send_cmd(uint8_t cmd);
    void send_cmd_data(uint8_t cmd, const uint8_t *data, size_t len);
    void send_cmd_byte(uint8_t cmd, uint8_t data);
    void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void write_pixels(const uint8_t *data, size_t len);
};

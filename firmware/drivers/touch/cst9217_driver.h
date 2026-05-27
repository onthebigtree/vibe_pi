#pragma once

#include "hal/touch_hal.h"

class CST9217Driver : public TouchHAL {
public:
    CST9217Driver(uint8_t sda, uint8_t scl, uint8_t intr, uint8_t rst,
                  uint16_t screen_w, uint16_t screen_h);
    bool init() override;
    TouchPoint read() override;

private:
    uint8_t _sda, _scl, _int, _rst;
    uint16_t _w, _h;
    static constexpr uint8_t I2C_ADDR = 0x5A;
};

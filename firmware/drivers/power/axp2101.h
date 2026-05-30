#pragma once

#include <Arduino.h>

// Minimal AXP2101 driver — just what we need for battery indicator.
// Register map (AXP2101 datasheet / XPowersLib):
//   0x00 PMU_STATUS1  — bit5 = VBUS good
//   0x01 PMU_STATUS2  — bits[6:5] battery current direction (01 = charging)
//   0x30 ADC_CHAN_EN  — bit0 = VBAT voltage ADC enable (MUST be set to read VBAT)
//   0x34/0x35         — VBAT ADC data, 14-bit, value is millivolts
class AXP2101 {
public:
    // sda/scl: I2C pins (shared with touch I2C). addr defaults 0x34.
    bool begin(int sda, int scl, uint8_t addr = 0x34);

    bool      present() const { return _present; }
    uint16_t  battery_voltage_mv();   // 0 if absent
    uint8_t   battery_percent();      // estimated from voltage (3.3V=0%, 4.2V=100%)
    bool      is_charging();
    bool      vbus_present();

private:
    bool _present = false;
    uint8_t _addr = 0x34;
    int _sda = -1, _scl = -1;
    bool read_reg(uint8_t reg, uint8_t *val);
    bool write_reg(uint8_t reg, uint8_t val);
    bool read_reg16_be(uint8_t reg_hi, uint8_t reg_lo, uint16_t *val);
};

extern AXP2101 g_axp;

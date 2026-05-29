#include "axp2101.h"
#include <Wire.h>

AXP2101 g_axp;

bool AXP2101::begin(int sda, int scl, uint8_t addr) {
    _sda = sda; _scl = scl; _addr = addr;
    Wire.begin(sda, scl);
    // This Wire.begin() runs after the touch driver already configured the bus,
    // and re-init resets the clock to the 100kHz default — restore 400kHz so
    // touch/IMU reads stay fast. setTimeOut bounds any stall on a wedged bus.
    Wire.setClock(400000);
    Wire.setTimeOut(50);
    // Probe: read chip ID register (0x03 on AXP2101)
    uint8_t id = 0;
    if (!read_reg(0x03, &id)) {
        _present = false;
        Serial.println("[AXP2101] not detected");
        return false;
    }
    _present = true;
    Serial.printf("[AXP2101] OK chip_id=0x%02X\n", id);
    return true;
}

bool AXP2101::read_reg(uint8_t reg, uint8_t *val) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(_addr, (uint8_t)1) != 1) return false;
    *val = Wire.read();
    return true;
}

bool AXP2101::read_reg16_be(uint8_t reg_hi, uint8_t reg_lo, uint16_t *val) {
    uint8_t hi, lo;
    if (!read_reg(reg_hi, &hi)) return false;
    if (!read_reg(reg_lo, &lo)) return false;
    *val = ((uint16_t)(hi & 0x3F) << 8) | lo;
    return true;
}

uint16_t AXP2101::battery_voltage_mv() {
    if (!_present) return 0;
    uint16_t raw;
    if (!read_reg16_be(0x34, 0x35, &raw)) return 0;
    return raw;  // already in mV on AXP2101
}

uint8_t AXP2101::battery_percent() {
    uint16_t mv = battery_voltage_mv();
    if (mv == 0) return 0;
    // Linear estimate: 3.3V → 0%, 4.2V → 100%. Clamp.
    if (mv >= 4200) return 100;
    if (mv <= 3300) return 0;
    return (uint8_t)((mv - 3300) * 100 / 900);
}

bool AXP2101::is_charging() {
    if (!_present) return false;
    uint8_t st;
    if (!read_reg(0x01, &st)) return false;  // PMU status
    return (st & 0x04) != 0;  // bit 2 = charging
}

bool AXP2101::vbus_present() {
    if (!_present) return false;
    uint8_t st;
    if (!read_reg(0x00, &st)) return false;
    return (st & 0x20) != 0;  // bit 5 = VBUS good
}

// Plain C-style accessors for use in other translation units (avoids weird
// extern class linkage in health_manager).
uint16_t axp_voltage() { return g_axp.battery_voltage_mv(); }
uint8_t  axp_percent() { return g_axp.battery_percent(); }
bool     axp_charging() { return g_axp.is_charging(); }
bool     axp_vbus()    { return g_axp.vbus_present(); }

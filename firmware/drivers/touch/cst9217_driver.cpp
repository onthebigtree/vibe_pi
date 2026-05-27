#include "cst9217_driver.h"
#include <Wire.h>
#include <Arduino.h>

CST9217Driver::CST9217Driver(uint8_t sda, uint8_t scl, uint8_t intr, uint8_t rst,
                             uint16_t screen_w, uint16_t screen_h)
    : _sda(sda), _scl(scl), _int(intr), _rst(rst), _w(screen_w), _h(screen_h) {}

bool CST9217Driver::init() {
    Wire.begin(_sda, _scl);
    Wire.setClock(400000);

    if (_rst != 0xFF) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
        delay(10);
        digitalWrite(_rst, HIGH);
        delay(50);
    }
    if (_int != 0xFF) {
        pinMode(_int, INPUT);
    }

    // Verify presence
    Wire.beginTransmission(I2C_ADDR);
    bool ok = (Wire.endTransmission() == 0);
    Serial.printf("[CST9217] Init %s\n", ok ? "OK" : "FAIL");
    return ok;
}

TouchPoint CST9217Driver::read() {
    TouchPoint tp = {0, 0, false};

    Wire.beginTransmission(I2C_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return tp;

    Wire.requestFrom(I2C_ADDR, (uint8_t)7);
    if (Wire.available() < 7) return tp;

    uint8_t buf[7];
    for (int i = 0; i < 7; i++) buf[i] = Wire.read();

    uint8_t touch_count = buf[2] & 0x0F;
    if (touch_count > 0) {
        tp.x = ((buf[3] & 0x0F) << 8) | buf[4];
        tp.y = ((buf[5] & 0x0F) << 8) | buf[6];
        tp.pressed = true;

        // Clamp to screen
        if (tp.x >= _w) tp.x = _w - 1;
        if (tp.y >= _h) tp.y = _h - 1;
    }
    return tp;
}

#include "cst9217_driver.h"
#include <Wire.h>
#include <Arduino.h>

#define CST9217_REG_DATA  0xD000

CST9217Driver::CST9217Driver(uint8_t sda, uint8_t scl, uint8_t intr, uint8_t rst,
                             uint16_t screen_w, uint16_t screen_h)
    : _sda(sda), _scl(scl), _int(intr), _rst(rst), _w(screen_w), _h(screen_h) {}

static int read_reg16(uint8_t addr, uint16_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return -1;
    Wire.requestFrom(addr, (uint8_t)len);
    int n = 0;
    while (Wire.available() && n < (int)len) buf[n++] = Wire.read();
    return n;
}

static bool write_reg16(uint8_t addr, uint16_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool CST9217Driver::init() {
    Wire.begin(_sda, _scl);
    Wire.setClock(400000);

    if (_rst != 0xFF) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
        delay(20);
        digitalWrite(_rst, HIGH);
        delay(100);
    }
    if (_int != 0xFF) {
        pinMode(_int, INPUT_PULLUP);
    }

    write_reg16(I2C_ADDR, 0xD101, 0x01);
    delay(10);

    uint8_t id_buf[4] = {};
    read_reg16(I2C_ADDR, 0xD204, id_buf, 4);
    Serial.printf("[CST9217] Init OK (SDA=%d SCL=%d INT=%d RST=%d)\n",
                  _sda, _scl, _int, _rst);
    return true;
}

TouchPoint CST9217Driver::read() {
    TouchPoint tp = {0, 0, false};

    uint8_t buf[12];
    int n = read_reg16(I2C_ADDR, CST9217_REG_DATA, buf, 7);
    if (n < 7) return tp;

    // buf[0]: gesture/event (0xFF = idle)
    if (buf[0] == 0xFF) return tp;

    // buf[5]: touch count, buf[6]: 0xAB = valid
    uint8_t count = buf[5];
    if (count == 0 || buf[6] != 0xAB) return tp;

    // SensorLib CST92xx coordinate decoding:
    // X = (buf[1] << 4) | (buf[3] >> 4)
    // Y = (buf[2] << 4) | (buf[3] & 0x0F)
    uint16_t x = ((uint16_t)buf[1] << 4) | (buf[3] >> 4);
    uint16_t y = ((uint16_t)buf[2] << 4) | (buf[3] & 0x0F);

    tp.pressed = true;
    tp.x = (x < _w) ? x : _w - 1;
    tp.y = (y < _h) ? y : _h - 1;

    return tp;
}

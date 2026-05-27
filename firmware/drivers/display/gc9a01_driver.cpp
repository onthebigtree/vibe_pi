#include "gc9a01_driver.h"
#include <Arduino.h>
#include <SPI.h>

static SPIClass *gc_spi = nullptr;

GC9A01Driver::GC9A01Driver(uint8_t mosi, uint8_t sck, uint8_t cs,
                           uint8_t dc, uint8_t rst, uint8_t bl)
    : _mosi(mosi), _sck(sck), _cs(cs), _dc(dc), _rst(rst), _bl(bl) {
    _cfg = {
        .width = 240, .height = 240, .radius = 120,
        .is_round = true, .color_depth = 16,
        .buf_pixel_count = 240 * 20,
        .supports_dimming = true,
    };
}

bool GC9A01Driver::init() {
    pinMode(_cs, OUTPUT);
    pinMode(_dc, OUTPUT);
    pinMode(_rst, OUTPUT);
    pinMode(_bl, OUTPUT);
    digitalWrite(_cs, HIGH);
    digitalWrite(_bl, LOW);

    gc_spi = new SPIClass(HSPI);
    gc_spi->begin(_sck, -1, _mosi, _cs);
    gc_spi->setFrequency(40000000);

    // Hardware reset
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(120);

    // GC9A01 init sequence
    write_cmd(0xEF);
    write_cmd(0xEB); write_data(0x14);
    write_cmd(0xFE); // Inter Register Enable1
    write_cmd(0xEF); // Inter Register Enable2
    write_cmd(0xEB); write_data(0x14);
    write_cmd(0x84); write_data(0x40);
    write_cmd(0x85); write_data(0xFF);
    write_cmd(0x86); write_data(0xFF);
    write_cmd(0x87); write_data(0xFF);
    write_cmd(0x88); write_data(0x0A);
    write_cmd(0x89); write_data(0x21);
    write_cmd(0x8A); write_data(0x00);
    write_cmd(0x8B); write_data(0x80);
    write_cmd(0x8C); write_data(0x01);
    write_cmd(0x8D); write_data(0x01);
    write_cmd(0x8E); write_data(0xFF);
    write_cmd(0x8F); write_data(0xFF);
    write_cmd(0xB6); write_data(0x00); write_data(0x00); // Display Function Control
    write_cmd(0x3A); write_data(0x55); // RGB565
    write_cmd(0x90); write_data(0x08); write_data(0x08); write_data(0x08); write_data(0x08);
    write_cmd(0xBD); write_data(0x06);
    write_cmd(0xBC); write_data(0x00);
    write_cmd(0xFF); write_data(0x60); write_data(0x01); write_data(0x04);
    write_cmd(0xC3); write_data(0x13); // Voltage regulator
    write_cmd(0xC4); write_data(0x13);
    write_cmd(0xC9); write_data(0x22);
    write_cmd(0xBE); write_data(0x11);
    write_cmd(0xE1); write_data(0x10); write_data(0x0E);
    write_cmd(0xDF); write_data(0x21); write_data(0x0C); write_data(0x02);
    write_cmd(0xF0); write_data(0x45); write_data(0x09); write_data(0x08); write_data(0x08); write_data(0x26); write_data(0x2A);
    write_cmd(0xF1); write_data(0x43); write_data(0x70); write_data(0x72); write_data(0x36); write_data(0x37); write_data(0x6F);
    write_cmd(0xF2); write_data(0x45); write_data(0x09); write_data(0x08); write_data(0x08); write_data(0x26); write_data(0x2A);
    write_cmd(0xF3); write_data(0x43); write_data(0x70); write_data(0x72); write_data(0x36); write_data(0x37); write_data(0x6F);
    write_cmd(0xED); write_data(0x1B); write_data(0x0B);
    write_cmd(0xAE); write_data(0x77);
    write_cmd(0xCD); write_data(0x63);
    write_cmd(0x70); write_data(0x07); write_data(0x07); write_data(0x04); write_data(0x0E); write_data(0x0F); write_data(0x09); write_data(0x07); write_data(0x08); write_data(0x03);
    write_cmd(0xE8); write_data(0x34);
    write_cmd(0x62); write_data(0x18); write_data(0x0D); write_data(0x71); write_data(0xED); write_data(0x70); write_data(0x70); write_data(0x18); write_data(0x0F); write_data(0x71); write_data(0xEF); write_data(0x70); write_data(0x70);
    write_cmd(0x63); write_data(0x18); write_data(0x11); write_data(0x71); write_data(0xF1); write_data(0x70); write_data(0x70); write_data(0x18); write_data(0x13); write_data(0x71); write_data(0xF3); write_data(0x70); write_data(0x70);
    write_cmd(0x64); write_data(0x28); write_data(0x29); write_data(0xF1); write_data(0x01); write_data(0xF1); write_data(0x00); write_data(0x07);
    write_cmd(0x66); write_data(0x3C); write_data(0x00); write_data(0xCD); write_data(0x67); write_data(0x45); write_data(0x45); write_data(0x10); write_data(0x00); write_data(0x00); write_data(0x00);
    write_cmd(0x67); write_data(0x00); write_data(0x3C); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x01); write_data(0x54); write_data(0x10); write_data(0x32); write_data(0x98);
    write_cmd(0x74); write_data(0x10); write_data(0x85); write_data(0x80); write_data(0x00); write_data(0x00); write_data(0x4E); write_data(0x00);
    write_cmd(0x98); write_data(0x3E); write_data(0x07);
    write_cmd(0x35); // Tearing effect line ON
    write_cmd(0x21); // Display Inversion ON
    write_cmd(0x11); // Sleep Out
    delay(120);
    write_cmd(0x29); // Display ON
    delay(20);

    analogWrite(_bl, 200);
    Serial.println("[GC9A01] Init complete");
    return true;
}

void GC9A01Driver::flush(const lv_area_t *area, uint8_t *px_map) {
    set_window(area->x1, area->y1, area->x2, area->y2);

    uint32_t size = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) * 2;
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    gc_spi->writeBytes(px_map, size);
    digitalWrite(_cs, HIGH);
}

void GC9A01Driver::set_brightness(uint8_t pct) {
    analogWrite(_bl, (uint32_t)pct * 255 / 100);
}

void GC9A01Driver::sleep() {
    analogWrite(_bl, 0);
    write_cmd(0x28); // Display OFF
    write_cmd(0x10); // Sleep IN
}

void GC9A01Driver::wake() {
    write_cmd(0x11); // Sleep OUT
    delay(120);
    write_cmd(0x29); // Display ON
    analogWrite(_bl, 200);
}

void GC9A01Driver::write_cmd(uint8_t cmd) {
    digitalWrite(_dc, LOW);
    digitalWrite(_cs, LOW);
    gc_spi->transfer(cmd);
    digitalWrite(_cs, HIGH);
}

void GC9A01Driver::write_data(uint8_t data) {
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    gc_spi->transfer(data);
    digitalWrite(_cs, HIGH);
}

void GC9A01Driver::write_data_buf(const uint8_t *data, size_t len) {
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    gc_spi->writeBytes(data, len);
    digitalWrite(_cs, HIGH);
}

void GC9A01Driver::set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    write_cmd(0x2A);
    write_data(x0 >> 8); write_data(x0 & 0xFF);
    write_data(x1 >> 8); write_data(x1 & 0xFF);
    write_cmd(0x2B);
    write_data(y0 >> 8); write_data(y0 & 0xFF);
    write_data(y1 >> 8); write_data(y1 & 0xFF);
    write_cmd(0x2C);
}

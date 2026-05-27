#include "co5300_driver.h"
#include <Arduino.h>
#include <SPI.h>

CO5300Driver::CO5300Driver(uint8_t cs, uint8_t sck, uint8_t d0, uint8_t d1,
                           uint8_t d2, uint8_t d3, uint8_t rst, uint8_t te)
    : _cs(cs), _sck(sck), _d0(d0), _d1(d1), _d2(d2), _d3(d3), _rst(rst), _te(te) {
    _cfg = {
        .width = 466, .height = 466, .radius = 233,
        .is_round = true, .color_depth = 16,
        .buf_pixel_count = 466 * 60,
        .supports_dimming = true,
    };
}

bool CO5300Driver::init() {
    pinMode(_rst, OUTPUT);
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    // Hardware reset
    digitalWrite(_rst, LOW);
    delay(20);
    digitalWrite(_rst, HIGH);
    delay(120);

    // TODO: CO5300 QSPI init sequence
    // Reference: Waveshare ESP32-S3 1.75" AMOLED example code
    // Steps: sleep out → delay 120ms → display on → set pixel format →
    //        set column/row address → memory access control → QSPI enable

    Serial.println("[CO5300] Init complete (stub — fill from Waveshare example)");
    return true;
}

void CO5300Driver::flush(const lv_area_t *area, uint8_t *px_map) {
    set_window(area->x1, area->y1, area->x2, area->y2);

    // TODO: Transfer px_map via QSPI DMA
    // Size: (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) * 2 bytes (RGB565)
}

void CO5300Driver::set_brightness(uint8_t pct) {
    // TODO: CO5300 brightness register write via QSPI command
    // Command 0x51, data = pct * 255 / 100
}

void CO5300Driver::sleep() {
    // TODO: Send display off + sleep in commands
}

void CO5300Driver::wake() {
    // TODO: Send sleep out + display on commands
}

void CO5300Driver::qspi_write_cmd(uint8_t cmd) {
    // TODO: QSPI command phase
}

void CO5300Driver::qspi_write_data(const uint8_t *data, size_t len) {
    // TODO: QSPI data phase
}

void CO5300Driver::set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // TODO: Set column address (0x2A) + row address (0x2B) + memory write (0x2C)
}

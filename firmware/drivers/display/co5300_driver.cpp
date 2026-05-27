#include "co5300_driver.h"
#include <Arduino.h>
#include <driver/spi_master.h>

#define QSPI_HOST     SPI2_HOST
#define QSPI_FREQ     40000000
#define MAX_PIXELS     1024
#define COL_OFFSET     6
#define ROW_OFFSET     0

static spi_device_handle_t _spi = nullptr;
static spi_transaction_ext_t _tran_ext;
static spi_transaction_t *_tran;
static uint8_t *_dma_buf = nullptr;

static uint32_t _cs_mask;
static volatile uint32_t *_cs_set;
static volatile uint32_t *_cs_clr;

static inline void CS_HIGH() { *_cs_set = _cs_mask; }
static inline void CS_LOW()  { *_cs_clr = _cs_mask; }
static inline void POLL_START() { spi_device_polling_start(_spi, _tran, portMAX_DELAY); }
static inline void POLL_END()   { spi_device_polling_end(_spi, portMAX_DELAY); }

// ── Exactly match Waveshare writeCommand ──
static void writeCommand(uint8_t c) {
    CS_LOW();
    _tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tran_ext.base.cmd = 0x02;
    _tran_ext.base.addr = ((uint32_t)c) << 8;
    _tran_ext.base.tx_buffer = NULL;
    _tran_ext.base.length = 0;
    POLL_START(); POLL_END();
    CS_HIGH();
}

// ── Exactly match Waveshare writeC8D8 ──
static void writeC8D8(uint8_t c, uint8_t d) {
    CS_LOW();
    _tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tran_ext.base.cmd = 0x02;
    _tran_ext.base.addr = ((uint32_t)c) << 8;
    _tran_ext.base.tx_data[0] = d;
    _tran_ext.base.length = 8;
    POLL_START(); POLL_END();
    CS_HIGH();
}

// ── Exactly match Waveshare writeC8D16D16 ──
static void writeC8D16D16(uint8_t c, uint16_t d1, uint16_t d2) {
    CS_LOW();
    _tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tran_ext.base.cmd = 0x02;
    _tran_ext.base.addr = ((uint32_t)c) << 8;
    _tran_ext.base.tx_data[0] = d1 >> 8;
    _tran_ext.base.tx_data[1] = d1 & 0xFF;
    _tran_ext.base.tx_data[2] = d2 >> 8;
    _tran_ext.base.tx_data[3] = d2 & 0xFF;
    _tran_ext.base.length = 32;
    POLL_START(); POLL_END();
    CS_HIGH();
}

CO5300Driver::CO5300Driver(uint8_t cs, uint8_t sck, uint8_t d0, uint8_t d1,
                           uint8_t d2, uint8_t d3, int8_t rst, int8_t te)
    : _cs(cs), _sck(sck), _d0(d0), _d1(d1), _d2(d2), _d3(d3), _rst(rst), _te(te) {
    _cfg = {
        .width = 466, .height = 466, .radius = 233,
        .is_round = true, .color_depth = 16,
        .buf_pixel_count = 466 * 40,
        .supports_dimming = true,
    };
}

void CO5300Driver::hw_reset() {
    if (_rst >= 0) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, HIGH);
        delay(10);
        digitalWrite(_rst, LOW);
        delay(200);
        digitalWrite(_rst, HIGH);
        delay(200);
    }
}

bool CO5300Driver::init() {
    hw_reset();

    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    _cs_mask = digitalPinToBitMask(_cs);
    if (_cs >= 32) {
        _cs_set = (volatile uint32_t *)GPIO_OUT1_W1TS_REG;
        _cs_clr = (volatile uint32_t *)GPIO_OUT1_W1TC_REG;
    } else {
        _cs_set = (volatile uint32_t *)GPIO_OUT_W1TS_REG;
        _cs_clr = (volatile uint32_t *)GPIO_OUT_W1TC_REG;
    }

    // SPI bus — exactly match Waveshare: mosi=D0, miso=D1, quadwp=D2, quadhd=D3
    spi_bus_config_t bus_cfg = {
        .mosi_io_num  = _d0,
        .miso_io_num  = _d1,
        .sclk_io_num  = _sck,
        .quadwp_io_num = _d2,
        .quadhd_io_num = _d3,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = (MAX_PIXELS * 16) + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .intr_flags = 0,
    };

    esp_err_t ret = spi_bus_initialize(QSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        Serial.printf("[CO5300] SPI bus init failed: %d\n", ret);
        return false;
    }

    // Device config — exactly match Waveshare
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 8,
        .address_bits = 24,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = QSPI_FREQ,
        .input_delay_ns = 0,
        .spics_io_num = -1,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
        .pre_cb = nullptr,
        .post_cb = nullptr,
    };

    ret = spi_bus_add_device(QSPI_HOST, &dev_cfg, &_spi);
    if (ret != ESP_OK) {
        Serial.printf("[CO5300] SPI device add failed: %d\n", ret);
        return false;
    }

    spi_device_acquire_bus(_spi, portMAX_DELAY);

    memset(&_tran_ext, 0, sizeof(_tran_ext));
    _tran = (spi_transaction_t *)&_tran_ext;

    _dma_buf = (uint8_t *)heap_caps_aligned_alloc(16, MAX_PIXELS * 2, MALLOC_CAP_DMA);
    if (!_dma_buf) {
        Serial.println("[CO5300] DMA alloc failed");
        return false;
    }

    // ── Init sequence — exactly match Waveshare co5300_init_operations[] ──
    writeCommand(0x11);             // Sleep Out
    delay(120);
    writeC8D8(0xFE, 0x00);         // Command page 0
    writeC8D8(0xC4, 0x80);         // SPI mode: enable QSPI
    writeC8D8(0x3A, 0x55);         // Pixel format: RGB565
    writeC8D8(0x53, 0x20);         // Brightness ctrl enable
    writeC8D8(0x63, 0xFF);         // HBM brightness max
    writeCommand(0x29);             // Display ON
    writeC8D8(0x51, 0xD0);         // Normal brightness 208/255
    writeC8D8(0x58, 0x00);         // Contrast enhancement off
    delay(10);
    writeCommand(0x20);             // Inversion Off

    Serial.println("[CO5300] Init complete");
    return true;
}

void CO5300Driver::flush(const lv_area_t *area, uint8_t *px_map) {
    uint16_t x = area->x1 + COL_OFFSET;
    uint16_t y = area->y1 + ROW_OFFSET;
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;
    uint32_t total_px = w * h;

    // Set address window — match Waveshare writeAddrWindow exactly
    writeC8D16D16(0x2A, x, x + w - 1);   // Column address set
    writeC8D16D16(0x2B, y, y + h - 1);   // Row address set
    writeCommand(0x2C);                    // Memory Write Start

    // Write pixel data via QIO — match Waveshare writePixels
    bool first = true;
    uint16_t *src = (uint16_t *)px_map;
    uint32_t remaining = total_px;

    CS_LOW();
    while (remaining > 0) {
        uint32_t chunk = (remaining > MAX_PIXELS) ? MAX_PIXELS : remaining;

        if (first) {
            _tran_ext.base.flags = SPI_TRANS_MODE_QIO;
            _tran_ext.base.cmd = 0x32;
            _tran_ext.base.addr = 0x003C00;
            first = false;
        } else {
            _tran_ext.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                                   SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            _tran_ext.command_bits = 0;
            _tran_ext.address_bits = 0;
            _tran_ext.dummy_bits = 0;
        }

        // Byte-swap RGB565 LE→BE (match Waveshare MSB_32_16_16_SET)
        uint16_t *dst = (uint16_t *)_dma_buf;
        for (uint32_t i = 0; i < chunk; i++) {
            dst[i] = __builtin_bswap16(src[i]);
        }

        _tran_ext.base.tx_buffer = _dma_buf;
        _tran_ext.base.length = chunk << 4;   // chunk * 16 bits

        POLL_START();
        POLL_END();

        src += chunk;
        remaining -= chunk;
    }
    CS_HIGH();
}

void CO5300Driver::set_brightness(uint8_t pct) {
    uint8_t val = (uint32_t)pct * 255 / 100;
    writeC8D8(0x51, val);
}

void CO5300Driver::sleep() {
    writeCommand(0x28);
    delay(120);
    writeCommand(0x10);
    delay(120);
}

void CO5300Driver::wake() {
    writeCommand(0x29);
    delay(120);
    writeCommand(0x11);
    delay(120);
}

// These are no longer used directly but kept for HAL interface compatibility
void CO5300Driver::send_cmd(uint8_t cmd) { writeCommand(cmd); }
void CO5300Driver::send_cmd_byte(uint8_t cmd, uint8_t data) { writeC8D8(cmd, data); }
void CO5300Driver::send_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {}
void CO5300Driver::set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {}
void CO5300Driver::write_pixels(const uint8_t *data, size_t len) {}

#include "co5300_driver.h"
#include <Arduino.h>
#include <driver/spi_master.h>

#define QSPI_HOST     SPI2_HOST
#define QSPI_FREQ     40000000
#define MAX_TRANSFER   (1024 * 2)
#define COL_OFFSET     6

static spi_device_handle_t _spi = nullptr;
static spi_transaction_ext_t _tran_ext;
static spi_transaction_t *_tran;
static uint8_t *_dma_buf = nullptr;
static uint8_t *_dma_buf2 = nullptr;

static uint32_t _cs_mask;
static volatile uint32_t *_cs_set;
static volatile uint32_t *_cs_clr;

static void cs_high() { *_cs_set = _cs_mask; }
static void cs_low()  { *_cs_clr = _cs_mask; }

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

    // Setup fast CS toggle via direct register access
    _cs_mask = digitalPinToBitMask(_cs);
    if (_cs >= 32) {
        _cs_set = (volatile uint32_t *)GPIO_OUT1_W1TS_REG;
        _cs_clr = (volatile uint32_t *)GPIO_OUT1_W1TC_REG;
    } else {
        _cs_set = (volatile uint32_t *)GPIO_OUT_W1TS_REG;
        _cs_clr = (volatile uint32_t *)GPIO_OUT_W1TC_REG;
    }

    // Init QSPI bus
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num  = _d0;
    bus_cfg.miso_io_num  = _d1;
    bus_cfg.sclk_io_num  = _sck;
    bus_cfg.quadwp_io_num = _d2;
    bus_cfg.quadhd_io_num = _d3;
    bus_cfg.max_transfer_sz = MAX_TRANSFER + 8;
    bus_cfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;

    esp_err_t ret = spi_bus_initialize(QSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        Serial.printf("[CO5300] SPI bus init failed: %d\n", ret);
        return false;
    }

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.command_bits = 8;
    dev_cfg.address_bits = 24;
    dev_cfg.mode = 0;
    dev_cfg.clock_speed_hz = QSPI_FREQ;
    dev_cfg.spics_io_num = -1;
    dev_cfg.flags = SPI_DEVICE_HALFDUPLEX;
    dev_cfg.queue_size = 1;

    ret = spi_bus_add_device(QSPI_HOST, &dev_cfg, &_spi);
    if (ret != ESP_OK) {
        Serial.printf("[CO5300] SPI device add failed: %d\n", ret);
        return false;
    }

    spi_device_acquire_bus(_spi, portMAX_DELAY);

    memset(&_tran_ext, 0, sizeof(_tran_ext));
    _tran = (spi_transaction_t *)&_tran_ext;

    // DMA-aligned buffers for pixel transfer
    _dma_buf = (uint8_t *)heap_caps_aligned_alloc(16, MAX_TRANSFER, MALLOC_CAP_DMA);
    _dma_buf2 = (uint8_t *)heap_caps_aligned_alloc(16, MAX_TRANSFER, MALLOC_CAP_DMA);
    if (!_dma_buf || !_dma_buf2) {
        Serial.println("[CO5300] DMA buffer alloc failed");
        return false;
    }

    // ── CO5300 Init Sequence (from Waveshare official Arduino_CO5300) ──

    // Sleep Out
    send_cmd(0x11);
    delay(120);

    // Select command page 0
    send_cmd_byte(0xFE, 0x00);

    // Enable QSPI mode
    send_cmd_byte(0xC4, 0x80);

    // Pixel format: RGB565
    send_cmd_byte(0x3A, 0x55);

    // Brightness control enable
    send_cmd_byte(0x53, 0x20);

    // HBM brightness max
    send_cmd_byte(0x63, 0xFF);

    // Display ON
    send_cmd(0x29);

    // Normal brightness
    send_cmd_byte(0x51, 0xD0);

    // Contrast enhancement off
    send_cmd_byte(0x58, 0x00);

    delay(10);

    // Inversion off
    send_cmd(0x20);

    Serial.println("[CO5300] Init complete");
    return true;
}

void CO5300Driver::send_cmd(uint8_t cmd) {
    cs_low();
    _tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tran_ext.base.cmd = 0x02;
    _tran_ext.base.addr = ((uint32_t)cmd) << 8;
    _tran_ext.base.tx_buffer = NULL;
    _tran_ext.base.length = 0;
    spi_device_polling_start(_spi, _tran, portMAX_DELAY);
    spi_device_polling_end(_spi, portMAX_DELAY);
    cs_high();
}

void CO5300Driver::send_cmd_byte(uint8_t cmd, uint8_t data) {
    cs_low();
    _tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tran_ext.base.cmd = 0x02;
    _tran_ext.base.addr = ((uint32_t)cmd) << 8;
    _tran_ext.base.tx_data[0] = data;
    _tran_ext.base.length = 8;
    spi_device_polling_start(_spi, _tran, portMAX_DELAY);
    spi_device_polling_end(_spi, portMAX_DELAY);
    cs_high();
}

void CO5300Driver::send_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    cs_low();
    _tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tran_ext.base.cmd = 0x02;
    _tran_ext.base.addr = ((uint32_t)cmd) << 8;
    _tran_ext.base.tx_buffer = data;
    _tran_ext.base.length = len * 8;
    spi_device_polling_start(_spi, _tran, portMAX_DELAY);
    spi_device_polling_end(_spi, portMAX_DELAY);
    cs_high();
}

void CO5300Driver::set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Apply column offset
    x0 += COL_OFFSET;
    x1 += COL_OFFSET;

    // Column address set
    uint8_t col[4] = {(uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
                      (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)};
    send_cmd_data(0x2A, col, 4);

    // Row address set
    uint8_t row[4] = {(uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
                      (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)};
    send_cmd_data(0x2B, row, 4);
}

void CO5300Driver::write_pixels(const uint8_t *data, size_t len) {
    bool first = true;
    cs_low();

    while (len > 0) {
        size_t chunk = (len > MAX_TRANSFER) ? MAX_TRANSFER : len;

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

        // Byte-swap RGB565 LE→BE for CO5300
        uint16_t *src = (uint16_t *)data;
        uint16_t *dst = (uint16_t *)_dma_buf;
        size_t px_count = chunk / 2;
        for (size_t i = 0; i < px_count; i++) {
            dst[i] = __builtin_bswap16(src[i]);
        }
        _tran_ext.base.tx_buffer = _dma_buf;
        _tran_ext.base.length = chunk * 8;

        spi_device_polling_start(_spi, _tran, portMAX_DELAY);
        spi_device_polling_end(_spi, portMAX_DELAY);

        data += chunk;
        len -= chunk;
    }

    cs_high();
}

void CO5300Driver::flush(const lv_area_t *area, uint8_t *px_map) {
    set_window(area->x1, area->y1, area->x2, area->y2);

    uint32_t size = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) * 2;
    write_pixels(px_map, size);
}

void CO5300Driver::set_brightness(uint8_t pct) {
    uint8_t val = (uint32_t)pct * 255 / 100;
    send_cmd_byte(0x51, val);
}

void CO5300Driver::sleep() {
    send_cmd(0x28);
    delay(120);
    send_cmd(0x10);
    delay(120);
}

void CO5300Driver::wake() {
    send_cmd(0x29);
    delay(120);
    send_cmd(0x11);
    delay(120);
}

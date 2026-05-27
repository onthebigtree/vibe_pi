#pragma once

#include "hal/board.h"

// ── Display: GC9A01 SPI 240x240 Round ──
#define BSP_DISPLAY_WIDTH    240
#define BSP_DISPLAY_HEIGHT   240
#define BSP_DISPLAY_ROUND    true
#define BSP_DISPLAY_RADIUS   120
#define BSP_COLOR_DEPTH      16

#define BSP_SPI_MOSI   35
#define BSP_SPI_SCK    36
#define BSP_SPI_CS     34
#define BSP_SPI_DC     7
#define BSP_DISP_RST   8
#define BSP_DISP_BL    9

// ── Touch: CST816S I2C ──
#define BSP_TOUCH_SDA  10
#define BSP_TOUCH_SCL  11
#define BSP_TOUCH_INT  12
#define BSP_TOUCH_RST  13

// ── Peripherals ──
#define BSP_BUTTON_PIN 0

static constexpr BoardInfo BSP_BOARD_INFO = {
    .board_id   = "gc9a01-round-240-generic",
    .board_name = "ESP32-S3 1.28\" Round LCD (GC9A01)",
    .mcu        = "esp32s3",
    .caps = {
        .has_touch = true,
        .has_imu   = false,
        .has_mic   = false,
        .has_rtc   = false,
        .has_psram = true,
        .has_button = true,
        .button_pin = 0,
        .button_active_low = true,
    },
};

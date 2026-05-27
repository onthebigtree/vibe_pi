#pragma once

#include "hal/board.h"

// ── Display: CO5300 QSPI ──
#define BSP_DISPLAY_WIDTH    466
#define BSP_DISPLAY_HEIGHT   466
#define BSP_DISPLAY_ROUND    true
#define BSP_DISPLAY_RADIUS   233
#define BSP_COLOR_DEPTH      16

#define BSP_QSPI_CS    12
#define BSP_QSPI_SCK   47
#define BSP_QSPI_D0    18
#define BSP_QSPI_D1    7
#define BSP_QSPI_D2    48
#define BSP_QSPI_D3    5
#define BSP_DISP_RST   17
#define BSP_DISP_TE    9

// ── Touch: CST9217 I2C ──
#define BSP_TOUCH_SDA  39
#define BSP_TOUCH_SCL  40
#define BSP_TOUCH_INT  13
#define BSP_TOUCH_RST  14

// ── Peripherals ──
#define BSP_BUTTON_PIN       0
#define BSP_IMU_ADDR         0x6B   // QMI8658

// ── Board descriptor ──
static constexpr BoardInfo BSP_BOARD_INFO = {
    .board_id   = "waveshare-esp32s3-amoled-175",
    .board_name = "Waveshare 1.75\" Round AMOLED",
    .mcu        = "esp32s3",
    .caps = {
        .has_touch = true,
        .has_imu   = true,
        .has_mic   = true,
        .has_rtc   = true,
        .has_psram = true,
        .has_button = true,
        .button_pin = 0,
        .button_active_low = true,
    },
};

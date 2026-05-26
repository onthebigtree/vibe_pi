#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <SPI.h>

// ── LVGL display buffers (in PSRAM for 466px wide display) ──
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_display_t *disp = nullptr;

// TODO: Implement CO5300 QSPI initialization
// Reference: Waveshare ESP32-S3 1.75" AMOLED example code
// The CO5300 uses QSPI interface for high-speed pixel data transfer.
// Init sequence: reset → sleep out → display on → set column/row → write memory

static void disp_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    // TODO: Send pixel data to CO5300 via QSPI
    //
    // Pseudocode:
    //   1. Set column address (area->x1 to area->x2)
    //   2. Set row address (area->y1 to area->y2)
    //   3. Write memory start
    //   4. Transfer px_map via QSPI DMA
    //   5. Signal flush ready

    lv_display_flush_ready(display);
}

// TODO: Implement CST9217 touch input
// Reference: Waveshare CST9217 driver
// The CST9217 communicates via I2C, reporting touch coordinates.

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    // TODO: Read touch data from CST9217 via I2C
    //
    // Pseudocode:
    //   1. Read touch registers via Wire
    //   2. If touch detected: data->state = LV_INDEV_STATE_PRESSED
    //   3. Set data->point.x / data->point.y
    //   4. Else: data->state = LV_INDEV_STATE_RELEASED

    data->state = LV_INDEV_STATE_RELEASED;
}

void display_init() {
    lv_init();

    // Allocate display buffers in PSRAM for the large 466px display
    size_t buf_size = SCREEN_WIDTH * 60;
    buf1 = (lv_color_t *)ps_malloc(buf_size * sizeof(lv_color_t));
    buf2 = (lv_color_t *)ps_malloc(buf_size * sizeof(lv_color_t));

    if (!buf1 || !buf2) {
        Serial.println("[Display] FATAL: PSRAM alloc failed — falling back to smaller buffers");
        buf_size = SCREEN_WIDTH * 20;
        buf1 = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
        buf2 = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
    }

    disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_size * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // TODO: Call CO5300 hardware init here
    // co5300_init();

    // Register touch input device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    // TODO: Call CST9217 hardware init here
    // cst9217_init();

    Serial.printf("[Display] LVGL initialized (%dx%d, buf=%zu px)\n",
                  SCREEN_WIDTH, SCREEN_HEIGHT, buf_size);
}

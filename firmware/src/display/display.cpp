#include "display.h"
#include "hal/board.h"
#include <Arduino.h>

static DisplayHAL *g_display = nullptr;
static TouchHAL   *g_touch   = nullptr;
static lv_display_t *lv_disp = nullptr;
static lv_indev_t   *lv_indev = nullptr;

static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

static void disp_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
    if (g_display) g_display->flush(area, px_map);
    lv_display_flush_ready(d);
}

static uint32_t _touch_dbg_counter = 0;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!g_touch) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    TouchPoint tp = g_touch->read();
    data->point.x = tp.x;
    data->point.y = tp.y;
    data->state = tp.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (tp.pressed && (++_touch_dbg_counter % 10 == 0)) {
        Serial.printf("[LVGL_TOUCH] x=%d y=%d\n", tp.x, tp.y);
    }
}

static void lv_tick_cb(void) {
    lv_tick_inc(5);
}

void display_init() {
    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)millis);

    const BoardInfo &board = board_get_info();

    // Create display driver via HAL
    g_display = board_create_display();
    if (!g_display || !g_display->init()) {
        Serial.println("[Display] FATAL: Display init failed");
        return;
    }

    const DisplayConfig &cfg = g_display->config();

    // Allocate buffers — PSRAM if available, fallback to heap
    size_t buf_px = cfg.buf_pixel_count;
    if (board.caps.has_psram && psramFound()) {
        buf1 = (lv_color_t *)ps_malloc(buf_px * sizeof(lv_color_t));
        buf2 = (lv_color_t *)ps_malloc(buf_px * sizeof(lv_color_t));
    }
    if (!buf1 || !buf2) {
        buf_px = cfg.width * 10; // conservative fallback
        buf1 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
        buf2 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
    }

    lv_disp = lv_display_create(cfg.width, cfg.height);
    lv_display_set_flush_cb(lv_disp, disp_flush_cb);
    lv_display_set_buffers(lv_disp, buf1, buf2, buf_px * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Create touch input if available
    g_touch = board_create_touch();
    if (g_touch && g_touch->init()) {
        lv_indev = lv_indev_create();
        lv_indev_set_type(lv_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(lv_indev, touch_read_cb);
        lv_indev_set_display(lv_indev, lv_disp);
        Serial.println("[Display] Touch enabled");
    }

    Serial.printf("[Display] %s: %dx%d, buf=%zu px\n",
                  board.board_name, cfg.width, cfg.height, buf_px);
}

void display_set_brightness(uint8_t level) {
    if (g_display) g_display->set_brightness(level);
}

void display_poll_touch_debug() {
    if (!g_touch) return;
    TouchPoint tp = g_touch->read();
    if (tp.pressed) {
        Serial.printf("[RAW_TOUCH] x=%d y=%d\n", tp.x, tp.y);
    }
}

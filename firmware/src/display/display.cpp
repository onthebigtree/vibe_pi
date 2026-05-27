#include "display.h"
#include "hal/board.h"
#include <Arduino.h>

static DisplayHAL *g_display = nullptr;
static TouchHAL   *g_touch   = nullptr;
static lv_display_t *lv_disp = nullptr;
static lv_indev_t   *lv_indev = nullptr;

static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

static uint32_t _touch_call_count = 0;
static TouchPoint _cached_tp = {0, 0, false};

static void disp_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
    if (g_display) g_display->flush(area, px_map);
    lv_display_flush_ready(d);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    _touch_call_count++;
    data->point.x = _cached_tp.x;
    data->point.y = _cached_tp.y;
    data->state = _cached_tp.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint32_t my_tick_get(void) {
    return (uint32_t)millis();
}

void display_init() {
    lv_init();
    lv_tick_set_cb(my_tick_get);

    const BoardInfo &board = board_get_info();

    g_display = board_create_display();
    if (!g_display || !g_display->init()) {
        Serial.println("[Display] FATAL: Display init failed");
        return;
    }

    const DisplayConfig &cfg = g_display->config();

    // Use large buffers in PSRAM for smooth rendering (1/4 screen each, double-buffered)
    size_t buf_px = cfg.width * cfg.height / 4;
    if (board.caps.has_psram && psramFound()) {
        buf1 = (lv_color_t *)ps_malloc(buf_px * sizeof(lv_color_t));
        buf2 = (lv_color_t *)ps_malloc(buf_px * sizeof(lv_color_t));
        Serial.printf("[Display] PSRAM buffers: 2x %zu px (%zu KB each)\n",
                      buf_px, buf_px * sizeof(lv_color_t) / 1024);
    }
    if (!buf1 || !buf2) {
        buf_px = cfg.width * 20;
        buf1 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
        buf2 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
    }

    lv_disp = lv_display_create(cfg.width, cfg.height);
    lv_display_set_flush_cb(lv_disp, disp_flush_cb);
    lv_display_set_buffers(lv_disp, buf1, buf2, buf_px * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    g_touch = board_create_touch();
    if (g_touch && g_touch->init()) {
        lv_indev = lv_indev_create();
        lv_indev_set_type(lv_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(lv_indev, touch_read_cb);
        lv_indev_set_display(lv_indev, lv_disp);
        Serial.println("[Display] Touch registered with LVGL");
    }

    Serial.printf("[Display] %s: %dx%d, buf=%zu px\n",
                  board.board_name, cfg.width, cfg.height, buf_px);
}

void display_set_brightness(uint8_t level) {
    if (g_display) g_display->set_brightness(level);
}

void display_update_touch() {
    if (g_touch) {
        _cached_tp = g_touch->read();
    }
}

uint32_t display_get_touch_cb_count() {
    return _touch_call_count;
}

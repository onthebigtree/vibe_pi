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
static uint32_t _touch_press_count = 0;
static TouchPoint _cached_tp = {0, 0, false};

static uint32_t _flush_count = 0;
static void disp_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
    _flush_count++;
    if (g_display) g_display->flush(area, px_map);
    lv_display_flush_ready(d);
}
uint32_t display_get_flush_count() { return _flush_count; }

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

    // DMA-aligned buffers in internal RAM (not PSRAM — PSRAM causes QSPI DMA deadlock)
    size_t buf_px = cfg.width * 40;
    buf1 = (lv_color_t *)heap_caps_aligned_alloc(16, buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = (lv_color_t *)heap_caps_aligned_alloc(16, buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        buf_px = cfg.width * 10;
        free(buf1); free(buf2);
        buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
        buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    }
    Serial.printf("[Display] DMA buffers: 2x %zu px (%zu KB each)\n",
                  buf_px, buf_px * sizeof(lv_color_t) / 1024);

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
        // Lower the swipe distance threshold from the 50px default to 35px so
        // page/tool gestures feel more responsive on the 466px round screen.
        // Not lower than this: the top-half tap-to-cycle target shares the same
        // area, and an over-sensitive gesture would trigger accidental cycling.
        lv_indev_set_gesture_min_distance(lv_indev, 35);
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
        TouchPoint tp = g_touch->read();
        if (tp.pressed && !_cached_tp.pressed) {
            _touch_press_count++;
            Serial.printf("[TOUCH] press #%u at (%d, %d)\n", _touch_press_count, tp.x, tp.y);
        }
        _cached_tp = tp;
    }
}

uint32_t display_get_touch_cb_count() {
    return _touch_call_count;
}

uint32_t display_get_touch_press_count() {
    return _touch_press_count;
}

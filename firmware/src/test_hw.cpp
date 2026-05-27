// Hardware self-test — compile with -DHWTEST to use this instead of main.cpp
#ifdef HWTEST

#include <Arduino.h>
#include <Wire.h>
#include "hal/board.h"
#include "boards/waveshare_175_amoled/board_def.h"

static DisplayHAL *disp = nullptr;
static TouchHAL *touch = nullptr;

static void fill_color(uint16_t color, const char *name) {
    const DisplayConfig &cfg = disp->config();
    uint16_t w = cfg.width;

    size_t strip_px = w * 10;
    uint16_t *buf = (uint16_t *)ps_malloc(strip_px * 2);
    if (!buf) buf = (uint16_t *)malloc(strip_px * 2);
    if (!buf) { Serial.println("[TEST] alloc fail"); return; }

    uint16_t be_color = __builtin_bswap16(color);
    for (size_t i = 0; i < strip_px; i++) buf[i] = be_color;

    Serial.printf("[TEST] Filling %s (0x%04X)...\n", name, color);
    unsigned long t0 = millis();

    for (uint16_t y = 0; y < cfg.height; y += 10) {
        uint16_t y1 = min((uint16_t)(y + 9), (uint16_t)(cfg.height - 1));
        lv_area_t area = { .x1 = 0, .y1 = (int32_t)y, .x2 = (int32_t)(w - 1), .y2 = (int32_t)y1 };
        disp->flush(&area, (uint8_t *)buf);
    }

    Serial.printf("[TEST] %s done in %lu ms\n", name, millis() - t0);
    free(buf);
}

static void draw_crosshair() {
    const DisplayConfig &cfg = disp->config();
    uint16_t w = cfg.width, h = cfg.height;
    uint16_t cx = w / 2, cy = h / 2;

    // Red horizontal line at vertical center
    uint16_t *hline = (uint16_t *)malloc(w * 2);
    uint16_t red = __builtin_bswap16(0xF800);
    for (int i = 0; i < w; i++) hline[i] = red;
    for (int dy = -1; dy <= 0; dy++) {
        lv_area_t a = {0, (int32_t)(cy + dy), (int32_t)(w - 1), (int32_t)(cy + dy)};
        disp->flush(&a, (uint8_t *)hline);
    }
    free(hline);

    // Green vertical line at horizontal center
    uint16_t green = __builtin_bswap16(0x07E0);
    uint16_t px[2] = {green, green};
    for (uint16_t y = 0; y < h; y++) {
        lv_area_t a = {(int32_t)(cx - 1), (int32_t)y, (int32_t)cx, (int32_t)y};
        disp->flush(&a, (uint8_t *)px);
    }

    // White 10x10 corner markers
    uint16_t block[100];
    uint16_t white = __builtin_bswap16(0xFFFF);
    for (int i = 0; i < 100; i++) block[i] = white;

    lv_area_t tl = {0, 0, 9, 9};
    disp->flush(&tl, (uint8_t *)block);
    lv_area_t tr = {(int32_t)(w - 10), 0, (int32_t)(w - 1), 9};
    disp->flush(&tr, (uint8_t *)block);
    lv_area_t bl = {0, (int32_t)(h - 10), 9, (int32_t)(h - 1)};
    disp->flush(&bl, (uint8_t *)block);
    lv_area_t br = {(int32_t)(w - 10), (int32_t)(h - 10), (int32_t)(w - 1), (int32_t)(h - 1)};
    disp->flush(&br, (uint8_t *)block);

    // Blue circle outline at ~radius (draw dots at edge)
    uint16_t blue = __builtin_bswap16(0x001F);
    uint16_t dot[4] = {blue, blue, blue, blue};
    int r = min(w, h) / 2 - 2;
    for (int angle = 0; angle < 360; angle += 2) {
        float rad = angle * 3.14159f / 180.0f;
        int px_x = cx + (int)(r * cosf(rad));
        int px_y = cy + (int)(r * sinf(rad));
        if (px_x >= 0 && px_x < w - 1 && px_y >= 0 && px_y < h - 1) {
            lv_area_t a = {px_x, px_y, px_x + 1, px_y + 1};
            disp->flush(&a, (uint8_t *)dot);
        }
    }

    Serial.printf("[TEST] Crosshair at (%d,%d), radius circle at r=%d\n", cx, cy, r);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[HWTEST] === Hardware Self Test ===");

    const BoardInfo &board = board_get_info();
    Serial.printf("[HWTEST] Board: %s (%s)\n", board.board_name, board.board_id);

    disp = board_create_display();
    if (!disp) { Serial.println("[HWTEST] FAIL: no display"); return; }
    bool ok = disp->init();
    Serial.printf("[HWTEST] Display init: %s\n", ok ? "OK" : "FAIL");
    if (!ok) return;

    const DisplayConfig &cfg = disp->config();
    Serial.printf("[HWTEST] Display: %dx%d, round=%d\n", cfg.width, cfg.height, cfg.is_round);

    touch = board_create_touch();
    if (touch) {
        bool tok = touch->init();
        Serial.printf("[HWTEST] Touch init: %s\n", tok ? "OK" : "FAIL");
    }

    disp->set_brightness(80);

    // Black background
    fill_color(0x0000, "BLACK");
    delay(200);

    // Draw alignment test pattern
    draw_crosshair();

    Serial.println("[HWTEST] === Alignment test ===");
    Serial.println("[HWTEST] You should see:");
    Serial.println("[HWTEST]   - Red horizontal line at screen center");
    Serial.println("[HWTEST]   - Green vertical line at screen center");
    Serial.println("[HWTEST]   - Blue circle near edge (should align with round bezel)");
    Serial.println("[HWTEST]   - White squares at 4 corners (hidden by bezel on round)");
    Serial.println("[HWTEST] Now polling touch...");
}

void loop() {
    if (touch) {
        TouchPoint tp = touch->read();
        if (tp.pressed) {
            Serial.printf("[TOUCH] x=%d y=%d\n", tp.x, tp.y);
        }
    }
    delay(30);
}

#endif // HWTEST

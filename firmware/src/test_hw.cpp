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

    // Fill in 10-line strips
    size_t strip_px = w * 10;
    uint16_t *buf = (uint16_t *)ps_malloc(strip_px * 2);
    if (!buf) buf = (uint16_t *)malloc(strip_px * 2);
    if (!buf) { Serial.println("[TEST] alloc fail"); return; }

    // Byte-swap for BE display
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

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[HWTEST] === Hardware Self Test ===");

    const BoardInfo &board = board_get_info();
    Serial.printf("[HWTEST] Board: %s (%s)\n", board.board_name, board.board_id);

    // Init display via HAL
    disp = board_create_display();
    if (!disp) { Serial.println("[HWTEST] FAIL: board_create_display returned null"); return; }

    bool ok = disp->init();
    Serial.printf("[HWTEST] Display init: %s\n", ok ? "OK" : "FAIL");
    if (!ok) return;

    const DisplayConfig &cfg = disp->config();
    Serial.printf("[HWTEST] Display: %dx%d, round=%d, depth=%d\n",
                  cfg.width, cfg.height, cfg.is_round, cfg.color_depth);

    // Init touch via HAL
    touch = board_create_touch();
    if (touch) {
        bool tok = touch->init();
        Serial.printf("[HWTEST] Touch init: %s\n", tok ? "OK" : "FAIL");
    } else {
        Serial.println("[HWTEST] No touch on this board");
    }

    // Brightness test
    Serial.println("[HWTEST] Brightness ramp: 0 → 100");
    for (int b = 0; b <= 100; b += 20) {
        disp->set_brightness(b);
        delay(200);
    }

    // Color fill tests
    fill_color(0xF800, "RED");
    delay(1000);
    fill_color(0x07E0, "GREEN");
    delay(1000);
    fill_color(0x001F, "BLUE");
    delay(1000);
    fill_color(0xFFFF, "WHITE");
    delay(1000);
    fill_color(0x0000, "BLACK");

    Serial.println("[HWTEST] === Color test complete ===");
    Serial.println("[HWTEST] Now polling touch... (touch the screen)");
}

void loop() {
    if (touch) {
        TouchPoint tp = touch->read();
        if (tp.pressed) {
            bool valid = (tp.x >= 0 && tp.x < 466 && tp.y >= 0 && tp.y < 466);
            Serial.printf("[TOUCH] x=%d y=%d %s\n", tp.x, tp.y, valid ? "OK" : "OUT_OF_RANGE");
        }
    }
    delay(30);
}

#endif // HWTEST

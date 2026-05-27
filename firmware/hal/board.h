#pragma once

#include "display_hal.h"
#include "touch_hal.h"
#include <stdint.h>

struct BoardCapabilities {
    bool    has_touch;
    bool    has_imu;
    bool    has_mic;
    bool    has_rtc;
    bool    has_psram;
    bool    has_button;
    uint8_t button_pin;
    bool    button_active_low;
};

struct BoardInfo {
    const char      *board_id;
    const char      *board_name;
    const char      *mcu;
    BoardCapabilities caps;
};

// Each BSP implements these
const BoardInfo   &board_get_info();
DisplayHAL        *board_create_display();
TouchHAL          *board_create_touch();     // nullptr if no touch

// Runtime accessors set after board_create_display()
const DisplayConfig &board_display_config();

// Proportional layout helpers
inline uint16_t scr_w()    { return board_display_config().width; }
inline uint16_t scr_h()    { return board_display_config().height; }
inline uint16_t scr_r()    { return board_display_config().radius; }
inline bool     scr_round(){ return board_display_config().is_round; }
inline int      pct_w(int pct) { return (scr_w() * pct) / 100; }
inline int      pct_h(int pct) { return (scr_h() * pct) / 100; }

#pragma once

#include <stdint.h>

struct TouchPoint {
    int16_t x;
    int16_t y;
    bool    pressed;
};

class TouchHAL {
public:
    virtual ~TouchHAL() = default;
    virtual bool init() = 0;
    virtual TouchPoint read() = 0;
};

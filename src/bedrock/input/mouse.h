#pragma once

#include <cstdint>

class MouseDevice {
public:
    void feed(char actionButtonId, std::int8_t buttonData, short x, short y, short dx, short dy,
              bool forceMotionlessPointer);
};

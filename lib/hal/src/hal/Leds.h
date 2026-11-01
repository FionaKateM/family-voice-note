#pragma once

#include <cstdint>

#include "hal/Colour.h"

namespace fvb {

// The 10 RGB LEDs on the Core2 base.
class Leds {
public:
    virtual ~Leds() = default;

    virtual uint8_t count() const = 0;
    virtual void setPixel(uint8_t index, Colour colour) = 0;
    virtual void setAll(Colour colour) = 0;
    // Sends the colours set since the last show() to the LEDs.
    virtual void show() = 0;
};

}  // namespace fvb

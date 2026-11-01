#pragma once

#include <cstdint>

namespace fvb {

struct Colour {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const Colour& other) const { return r == other.r && g == other.g && b == other.b; }
    bool operator!=(const Colour& other) const { return !(*this == other); }
};

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

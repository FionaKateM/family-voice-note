#pragma once

#include <cstdint>
#include <string>

namespace fvb {

// The 320x240 screen. Deliberately basic for now: the real screens (ticket 1.11
// and 2.5) will decide whether this grows or LVGL takes over drawing.
class Display {
public:
    virtual ~Display() = default;

    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;

    // Turns the backlight off for quiet hours. Drawing still works while off.
    virtual void setOn(bool on) = 0;
    virtual void setBrightness(uint8_t percent) = 0;

    virtual void clear() = 0;
    virtual void drawText(int16_t x, int16_t y, const std::string& text) = 0;

    // Sends everything drawn since the last present() to the screen.
    virtual void present() = 0;
};

}  // namespace fvb

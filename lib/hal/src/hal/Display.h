#pragma once

#include <cstdint>
#include <string>

#include "hal/Colour.h"

namespace fvb {

// The 320x240 screen. (0, 0) is the top left corner.
//
// Deliberately basic: enough shapes and text for the rough screens in ticket
// 1.11. Ticket 2.5 decides whether this grows (photos, nicer fonts) or LVGL takes
// over drawing.
class Display {
public:
    virtual ~Display() = default;

    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;

    // Turns the backlight off for quiet hours. Drawing still works while off.
    virtual void setOn(bool on) = 0;
    virtual void setBrightness(uint8_t percent) = 0;

    // Fills the whole screen with black.
    virtual void clear() = 0;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Colour colour) = 0;
    virtual void fillCircle(int16_t centreX, int16_t centreY, int16_t radius, Colour colour) = 0;
    virtual void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                              Colour colour) = 0;

    // Text with its top left corner at (x, y). Size 1 is small print, 2 is
    // normal, and bigger sizes are for single letters or numbers.
    virtual void drawText(int16_t x, int16_t y, const std::string& text, Colour colour = Colour{255, 255, 255},
                          uint8_t size = 2) = 0;
    // How wide drawText would make this text, in pixels. Used for centring.
    virtual int16_t textWidth(const std::string& text, uint8_t size = 2) const = 0;
    virtual int16_t textHeight(uint8_t size = 2) const = 0;

    // Sends everything drawn since the last present() to the screen.
    virtual void present() = 0;
};

}  // namespace fvb

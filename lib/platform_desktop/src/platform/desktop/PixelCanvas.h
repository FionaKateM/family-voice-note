#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/Display.h"

namespace fvb {

// A Display that draws into memory: one 0xRRGGBB value per pixel. The desktop
// window shows its last presented frame. Kept apart from the window so the
// drawing can be tested without opening one.
class PixelCanvas : public Display {
public:
    PixelCanvas(uint16_t width = 320, uint16_t height = 240);

    uint16_t width() const override { return width_; }
    uint16_t height() const override { return height_; }

    void setOn(bool on) override { on_ = on; ++frame_; }
    void setBrightness(uint8_t percent) override { brightness_ = percent > 100 ? 100 : percent; ++frame_; }

    void clear() override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Colour colour) override;
    void fillCircle(int16_t centreX, int16_t centreY, int16_t radius, Colour colour) override;
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, Colour colour) override;
    void drawText(int16_t x, int16_t y, const std::string& text, Colour colour = Colour{255, 255, 255},
                  uint8_t size = 2) override;
    int16_t textWidth(const std::string& text, uint8_t size = 2) const override;
    int16_t textHeight(uint8_t size = 2) const override;
    void present() override;

    // ---- For the window and tests ----
    bool isOn() const { return on_; }
    uint8_t brightness() const { return brightness_; }
    // Goes up each time there's something new to show.
    uint64_t frameNumber() const { return frame_; }
    // The frame last presented, row by row from the top left.
    const std::vector<uint32_t>& shown() const { return shown_; }
    uint32_t shownPixel(int x, int y) const { return shown_[static_cast<size_t>(y) * width_ + x]; }
    // What's been drawn since the last present().
    uint32_t drawnPixel(int x, int y) const { return pixels_[static_cast<size_t>(y) * width_ + x]; }

private:
    void setPixel(int x, int y, uint32_t rgb);
    void hline(int x0, int x1, int y, uint32_t rgb);

    uint16_t width_;
    uint16_t height_;
    bool on_ = true;
    uint8_t brightness_ = 100;
    uint64_t frame_ = 0;
    std::vector<uint32_t> pixels_;
    std::vector<uint32_t> shown_;
};

inline uint32_t toRgb(Colour c) { return (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b; }

}  // namespace fvb

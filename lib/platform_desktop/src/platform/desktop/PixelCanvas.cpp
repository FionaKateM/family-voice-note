#include "platform/desktop/PixelCanvas.h"

#include <algorithm>
#include <cmath>

#include "platform/desktop/PixelFont.h"

namespace fvb {

namespace {

// Splits UTF-8 text into characters. Anything outside plain ASCII comes back as
// 0, which is drawn as a box (the font only has ASCII).
std::vector<char> characters(const std::string& text) {
    std::vector<char> out;
    for (unsigned char c : text) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c >= 0xC0) {
            out.push_back(0);  // first byte of a longer character
        }
        // 0x80 to 0xBF continue a character already counted
    }
    return out;
}

}  // namespace

PixelCanvas::PixelCanvas(uint16_t width, uint16_t height)
    : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height, 0),
      shown_(static_cast<size_t>(width) * height, 0) {}

void PixelCanvas::setPixel(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    pixels_[static_cast<size_t>(y) * width_ + x] = rgb;
}

void PixelCanvas::hline(int x0, int x1, int y, uint32_t rgb) {
    if (y < 0 || y >= height_) return;
    x0 = std::max(x0, 0);
    x1 = std::min(x1, width_ - 1);
    for (int x = x0; x <= x1; ++x) pixels_[static_cast<size_t>(y) * width_ + x] = rgb;
}

void PixelCanvas::clear() { std::fill(pixels_.begin(), pixels_.end(), 0); }

void PixelCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Colour colour) {
    const uint32_t rgb = toRgb(colour);
    for (int row = y; row < y + h; ++row) hline(x, x + w - 1, row, rgb);
}

void PixelCanvas::fillCircle(int16_t cx, int16_t cy, int16_t radius, Colour colour) {
    if (radius < 0) return;
    const uint32_t rgb = toRgb(colour);
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = static_cast<int>(std::sqrt(static_cast<double>(radius) * radius - dy * dy) + 0.5);
        hline(cx - half, cx + half, cy + dy, rgb);
    }
}

void PixelCanvas::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, Colour colour) {
    const uint32_t rgb = toRgb(colour);
    // Tests each pixel's centre against the three edges. Slow in theory, but the
    // screen is small and triangles here are icons.
    auto edge = [](double ax, double ay, double bx, double by, double px, double py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    const double area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    const int minX = std::max(0, static_cast<int>(std::min({x0, x1, x2})));
    const int maxX = std::min(width_ - 1, static_cast<int>(std::max({x0, x1, x2})));
    const int minY = std::max(0, static_cast<int>(std::min({y0, y1, y2})));
    const int maxY = std::min(height_ - 1, static_cast<int>(std::max({y0, y1, y2})));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double a = edge(x1, y1, x2, y2, px, py);
            const double b = edge(x2, y2, x0, y0, px, py);
            const double c = edge(x0, y0, x1, y1, px, py);
            const bool inside = area > 0 ? (a >= 0 && b >= 0 && c >= 0) : (a <= 0 && b <= 0 && c <= 0);
            if (inside) setPixel(x, y, rgb);
        }
    }
}

void PixelCanvas::drawText(int16_t x, int16_t y, const std::string& text, Colour colour, uint8_t size) {
    if (size == 0) size = 1;
    const uint32_t rgb = toRgb(colour);
    int left = x;
    for (char c : characters(text)) {
        if (c >= pixelfont::kFirst && c <= pixelfont::kLast) {
            const uint8_t* rows = pixelfont::kGlyphs[c - pixelfont::kFirst];
            for (int row = 0; row < pixelfont::kHeight; ++row) {
                for (int col = 0; col < pixelfont::kWidth; ++col) {
                    if (!(rows[row] & (1 << (pixelfont::kWidth - 1 - col)))) continue;
                    for (int sy = 0; sy < size; ++sy) {
                        for (int sx = 0; sx < size; ++sx) setPixel(left + col * size + sx, y + row * size + sy, rgb);
                    }
                }
            }
        } else if (c != ' ') {
            // A character the font doesn't have: an outlined box.
            const int w = (pixelfont::kWidth - 2) * size;
            const int h = (pixelfont::kHeight - 4) * size;
            const int top = y + 2 * size;
            for (int i = 0; i < w; ++i) {
                setPixel(left + size + i, top, rgb);
                setPixel(left + size + i, top + h - 1, rgb);
            }
            for (int i = 0; i < h; ++i) {
                setPixel(left + size, top + i, rgb);
                setPixel(left + size + w - 1, top + i, rgb);
            }
        }
        left += pixelfont::kWidth * size;
    }
}

int16_t PixelCanvas::textWidth(const std::string& text, uint8_t size) const {
    if (size == 0) size = 1;
    return static_cast<int16_t>(characters(text).size() * pixelfont::kWidth * size);
}

int16_t PixelCanvas::textHeight(uint8_t size) const {
    if (size == 0) size = 1;
    return static_cast<int16_t>(pixelfont::kHeight * size);
}

void PixelCanvas::present() {
    shown_ = pixels_;
    ++frame_;
}

}  // namespace fvb

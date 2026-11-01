#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "hal/Input.h"
#include "platform/desktop/DesktopBasics.h"
#include "platform/desktop/PixelCanvas.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace fvb {

// The laptop's stand-in for the Core2's screen, buttons and LEDs: a window
// showing the 320x240 screen, with a strip underneath for the 10 LEDs and the
// three buttons.
//
//   Keys 1, 2, 3 (or the left arrow, down arrow and right arrow) press the
//   left, middle and right buttons. Clicking a button in the strip does too.
//   Clicking the screen is a tap. Dragging across it is a swipe.
//
// Uses SDL2. Everything here must be called from the main thread (a macOS rule).
class DesktopWindow {
public:
    // Extra space under the screen for the LEDs and buttons.
    static constexpr int kStripHeight = 64;

    DesktopWindow(PixelCanvas& screen, const DesktopLeds& leds) : screen_(screen), leds_(leds) {}
    ~DesktopWindow();
    DesktopWindow(const DesktopWindow&) = delete;
    DesktopWindow& operator=(const DesktopWindow&) = delete;

    // Opens the window, `scale` times bigger than the real screen. Returns false
    // with a reason if it can't (e.g. no display).
    bool open(int scale, std::string& error);

    // Reads the keyboard and mouse, and redraws if anything changed. Call every loop.
    void update();
    bool pollEvent(InputEvent& event);

private:
    void handleKey(int key, bool down);
    void handleMouseDown(int x, int y);
    void handleMouseUp(int x, int y);
    void pressButton(int index);
    void render();
    void drawStrip();

    PixelCanvas& screen_;
    const DesktopLeds& leds_;
    PixelCanvas strip_{320, kStripHeight};

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;

    std::deque<InputEvent> events_;
    bool mouseDown_ = false;
    int downX_ = 0;
    int downY_ = 0;
    bool buttonHeld_[3] = {false, false, false};
    bool mouseButton_[3] = {false, false, false};

    uint64_t lastFrame_ = ~0ULL;
    Colour lastLeds_[10];
    bool needsRender_ = true;
};

// The window's keyboard and mouse as the box's Input.
class DesktopWindowInput : public Input {
public:
    explicit DesktopWindowInput(DesktopWindow& window) : window_(window) {}
    void update() override { window_.update(); }
    bool pollEvent(InputEvent& event) override { return window_.pollEvent(event); }

private:
    DesktopWindow& window_;
};

}  // namespace fvb

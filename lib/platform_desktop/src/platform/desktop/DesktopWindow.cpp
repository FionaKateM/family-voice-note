#include "platform/desktop/DesktopWindow.h"

#include <SDL.h>

#include <cstdlib>

#include "platform/desktop/StopRequest.h"

namespace fvb {

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kButtonCentres[3] = {53, 160, 267};  // match app/screens/ScreenLayout.h
constexpr int kButtonWidth = 84;
constexpr int kButtonTop = 30;
constexpr int kButtonHeight = 28;
// A drag shorter than this counts as a tap.
constexpr int kSwipeDistance = 25;

}  // namespace

DesktopWindow::~DesktopWindow() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (window_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool DesktopWindow::open(int scale, std::string& error) {
    // Leave Ctrl+C to StopRequest, which already knows how to stop the box tidily.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        error = SDL_GetError();
        return false;
    }
    const int totalHeight = kScreenHeight + kStripHeight;
    window_ = SDL_CreateWindow("Family Voice Box", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               kScreenWidth * scale, totalHeight * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, 0);
    if (!renderer_) {
        error = SDL_GetError();
        return false;
    }
    // Keeps the screen's shape whatever size the window is, and gives mouse
    // positions in screen pixels.
    SDL_RenderSetLogicalSize(renderer_, kScreenWidth, totalHeight);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, kScreenWidth,
                                 totalHeight);
    if (!texture_) {
        error = SDL_GetError();
        return false;
    }
    needsRender_ = true;
    render();
    return true;
}

void DesktopWindow::update() {
    if (!window_) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                requestStop();  // closing the window stops the box, like Ctrl+C
                break;
            case SDL_KEYDOWN:
                if (!e.key.repeat) handleKey(e.key.keysym.sym, true);
                break;
            case SDL_KEYUP:
                handleKey(e.key.keysym.sym, false);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) handleMouseDown(e.button.x, e.button.y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) handleMouseUp(e.button.x, e.button.y);
                break;
            case SDL_WINDOWEVENT:
                needsRender_ = true;
                break;
            default:
                break;
        }
    }
    render();
}

bool DesktopWindow::pollEvent(InputEvent& event) {
    if (events_.empty()) return false;
    event = events_.front();
    events_.pop_front();
    return true;
}

void DesktopWindow::pressButton(int index) {
    InputEvent e;
    e.type = InputEvent::Type::ButtonPress;
    e.button = static_cast<Button>(index);
    events_.push_back(e);
}

void DesktopWindow::handleKey(int key, bool down) {
    int index = -1;
    if (key == SDLK_1 || key == SDLK_KP_1 || key == SDLK_LEFT) index = 0;
    if (key == SDLK_2 || key == SDLK_KP_2 || key == SDLK_DOWN) index = 1;
    if (key == SDLK_3 || key == SDLK_KP_3 || key == SDLK_RIGHT) index = 2;
    if (index < 0) return;
    // The Core2's buttons act when touched, so the press counts straight away.
    if (down) pressButton(index);
    buttonHeld_[index] = down;
    needsRender_ = true;
}

void DesktopWindow::handleMouseDown(int x, int y) {
    if (y >= kScreenHeight) {
        // In the strip: maybe one of the buttons.
        const int stripY = y - kScreenHeight;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(x - kButtonCentres[i]) <= kButtonWidth / 2 && stripY >= kButtonTop &&
                stripY < kButtonTop + kButtonHeight) {
                pressButton(i);
                mouseButton_[i] = true;
                needsRender_ = true;
            }
        }
        return;
    }
    mouseDown_ = true;
    downX_ = x;
    downY_ = y;
}

void DesktopWindow::handleMouseUp(int x, int y) {
    for (bool& held : mouseButton_) {
        if (held) needsRender_ = true;
        held = false;
    }
    if (!mouseDown_) return;
    mouseDown_ = false;

    const int dx = x - downX_;
    const int dy = y - downY_;
    InputEvent e;
    if (std::abs(dx) < kSwipeDistance && std::abs(dy) < kSwipeDistance) {
        e.type = InputEvent::Type::Tap;
        e.x = static_cast<int16_t>(downX_);
        e.y = static_cast<int16_t>(downY_);
    } else {
        e.type = InputEvent::Type::Swipe;
        if (std::abs(dx) >= std::abs(dy)) {
            e.swipe = dx < 0 ? SwipeDirection::Left : SwipeDirection::Right;
        } else {
            e.swipe = dy < 0 ? SwipeDirection::Up : SwipeDirection::Down;
        }
    }
    events_.push_back(e);
}

void DesktopWindow::drawStrip() {
    strip_.clear();
    strip_.fillRect(0, 0, 320, kStripHeight, Colour{40, 40, 44});

    // The LEDs: the Core2 base has five down each side; here they're in a row.
    for (int i = 0; i < 10; ++i) {
        const Colour c = leds_.shown(static_cast<uint8_t>(i));
        const int16_t x = static_cast<int16_t>(25 + i * 30);
        strip_.fillCircle(x, 14, 7, Colour{70, 70, 76});
        if (c != Colour{}) strip_.fillCircle(x, 14, 6, c);
    }

    static const char* labels[3] = {"1", "2", "3"};
    for (int i = 0; i < 3; ++i) {
        const bool held = buttonHeld_[i] || mouseButton_[i];
        const int16_t left = static_cast<int16_t>(kButtonCentres[i] - kButtonWidth / 2);
        strip_.fillRect(left, kButtonTop, kButtonWidth, kButtonHeight, held ? Colour{200, 200, 210} : Colour{90, 90, 100});
        const Colour text = held ? Colour{30, 30, 30} : Colour{230, 230, 235};
        strip_.drawText(static_cast<int16_t>(kButtonCentres[i] - strip_.textWidth(labels[i], 1) / 2),
                        static_cast<int16_t>(kButtonTop + (kButtonHeight - strip_.textHeight(1)) / 2), labels[i], text, 1);
    }
    strip_.present();
}

void DesktopWindow::render() {
    if (!window_) return;
    bool ledsChanged = false;
    for (uint8_t i = 0; i < 10; ++i) {
        if (leds_.shown(i) != lastLeds_[i]) {
            lastLeds_[i] = leds_.shown(i);
            ledsChanged = true;
        }
    }
    if (!needsRender_ && !ledsChanged && screen_.frameNumber() == lastFrame_) return;
    needsRender_ = false;
    lastFrame_ = screen_.frameNumber();

    drawStrip();

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) != 0) return;
    const int percent = screen_.isOn() ? screen_.brightness() : 0;
    for (int y = 0; y < kScreenHeight + kStripHeight; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + y * pitch);
        for (int x = 0; x < kScreenWidth; ++x) {
            if (y >= kScreenHeight) {
                row[x] = strip_.shownPixel(x, y - kScreenHeight);
                continue;
            }
            uint32_t rgb = screen_.shownPixel(x, y);
            if (percent < 100) {
                // Backlight: dim, or black when off.
                const uint32_t r = ((rgb >> 16) & 0xff) * percent / 100;
                const uint32_t g = ((rgb >> 8) & 0xff) * percent / 100;
                const uint32_t b = (rgb & 0xff) * percent / 100;
                rgb = (r << 16) | (g << 8) | b;
            }
            row[x] = rgb;
        }
    }
    SDL_UnlockTexture(texture_);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

}  // namespace fvb

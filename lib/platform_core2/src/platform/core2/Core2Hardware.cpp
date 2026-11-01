#include "platform/core2/Core2Hardware.h"

#include <M5Unified.h>
#include <sys/time.h>

namespace fvb {

// ---- Clock ----

uint64_t Core2Clock::millisSinceBoot() const { return millis(); }

bool Core2Clock::isTimeSet() const {
    // Anything before 2024 means the clock hasn't been set from the internet yet.
    return unixTime() > 1704067200;
}

int64_t Core2Clock::unixTime() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec;
}

void Core2Clock::sleepMs(uint32_t ms) { delay(ms); }

void Core2Clock::startInternetTime() {
    // Offsets of 0: the system clock stays in UTC.
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

// ---- Logger ----

void Core2SerialLogger::log(Level level, const std::string& message) {
    static const char* names[] = {"debug", "info", "warning", "error"};
    Serial.printf("[%s] %s\n", names[static_cast<int>(level)], message.c_str());
}

// ---- Power ----

int8_t Core2Power::batteryPercent() const { return static_cast<int8_t>(M5.Power.getBatteryLevel()); }

bool Core2Power::isCharging() const { return M5.Power.isCharging() == m5::Power_Class::is_charging; }

bool Core2Power::isPluggedIn() const { return M5.Power.getVBUSVoltage() > 4000; }

// ---- Display ----

uint16_t Core2Display::width() const { return M5.Display.width(); }
uint16_t Core2Display::height() const { return M5.Display.height(); }

void Core2Display::setOn(bool on) { M5.Display.setBrightness(on ? brightness_ * 255 / 100 : 0); }

void Core2Display::setBrightness(uint8_t percent) {
    brightness_ = percent > 100 ? 100 : percent;
    M5.Display.setBrightness(brightness_ * 255 / 100);
}

void Core2Display::clear() { M5.Display.fillScreen(TFT_BLACK); }

static uint16_t toColour565(Colour c) { return M5.Display.color565(c.r, c.g, c.b); }

void Core2Display::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Colour colour) {
    M5.Display.fillRect(x, y, w, h, toColour565(colour));
}

void Core2Display::fillCircle(int16_t centreX, int16_t centreY, int16_t radius, Colour colour) {
    M5.Display.fillCircle(centreX, centreY, radius, toColour565(colour));
}

void Core2Display::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                                Colour colour) {
    M5.Display.fillTriangle(x0, y0, x1, y1, x2, y2, toColour565(colour));
}

void Core2Display::drawText(int16_t x, int16_t y, const std::string& text, Colour colour, uint8_t size) {
    M5.Display.setTextColor(toColour565(colour));  // no background, so text can sit on shapes
    M5.Display.setTextSize(size);
    M5.Display.drawString(text.c_str(), x, y);
}

int16_t Core2Display::textWidth(const std::string& text, uint8_t size) const {
    M5.Display.setTextSize(size);
    return static_cast<int16_t>(M5.Display.textWidth(text.c_str()));
}

int16_t Core2Display::textHeight(uint8_t size) const {
    M5.Display.setTextSize(size);
    return static_cast<int16_t>(M5.Display.fontHeight());
}

void Core2Display::present() {
    // Drawing goes straight to the screen for now, so screens flicker as they're
    // redrawn. Ticket 2.5 should draw to a buffer (an M5Canvas sprite) first.
}

// ---- Input ----

void Core2Input::push(const InputEvent& event) {
    if (count_ == kQueueSize) return;  // drop if nobody is reading
    queue_[(head_ + count_) % kQueueSize] = event;
    ++count_;
}

void Core2Input::update() {
    M5.update();

    InputEvent e;
    e.type = InputEvent::Type::ButtonPress;
    if (M5.BtnA.wasPressed()) { e.button = Button::Left; push(e); }
    if (M5.BtnB.wasPressed()) { e.button = Button::Middle; push(e); }
    if (M5.BtnC.wasPressed()) { e.button = Button::Right; push(e); }

    // Taps and swipes on the screen come in ticket 2.1.
}

bool Core2Input::pollEvent(InputEvent& event) {
    if (count_ == 0) return false;
    event = queue_[head_];
    head_ = (head_ + 1) % kQueueSize;
    --count_;
    return true;
}

}  // namespace fvb

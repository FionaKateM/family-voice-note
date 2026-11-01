#pragma once

// Desktop versions of the simpler interfaces.
//
// Clock and Logger are real. The rest are placeholders that print to the
// terminal; later tickets replace them:
//   Network  -> ticket 1.3 (real HTTPS to Telegram)
//   Display, Input, Audio -> ticket 1.11 (320x240 window, keyboard, mic and speakers)

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "hal/Hardware.h"

namespace fvb {

class DesktopClock : public Clock {
public:
    uint64_t millisSinceBoot() const override {
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now() - start_).count());
    }
    // The laptop's clock is already set from the internet.
    bool isTimeSet() const override { return true; }
    int64_t unixTime() const override {
        using namespace std::chrono;
        return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    }
    void sleepMs(uint32_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

class DesktopLogger : public Logger {
public:
    explicit DesktopLogger(Level minimum = Level::Info) : minimum_(minimum) {}
    void log(Level level, const std::string& message) override {
        if (level < minimum_) return;
        static const char* names[] = {"debug", "info", "warning", "error"};
        std::fprintf(level >= Level::Warning ? stderr : stdout, "[%s] %s\n",
                     names[static_cast<int>(level)], message.c_str());
        std::fflush(stdout);
    }

private:
    Level minimum_;
};

// Prints each screen to the terminal until the window arrives in ticket 1.11.
class DesktopTerminalDisplay : public Display {
public:
    uint16_t width() const override { return 320; }
    uint16_t height() const override { return 240; }
    void setOn(bool on) override { on_ = on; }
    void setBrightness(uint8_t) override {}
    void clear() override { lines_.clear(); }
    void drawText(int16_t, int16_t, const std::string& text) override { lines_.push_back(text); }
    void present() override {
        if (!on_) return;
        std::printf("+-- screen ---------------------------\n");
        for (const auto& line : lines_) std::printf("| %s\n", line.c_str());
        std::printf("+-------------------------------------\n");
        std::fflush(stdout);
    }

private:
    bool on_ = true;
    std::vector<std::string> lines_;
};

class DesktopPlaceholderAudio : public Audio {
public:
    bool play(const std::string&) override { return false; }
    void stopPlayback() override {}
    bool isPlaying() const override { return false; }
    void setVolume(uint8_t) override {}
    bool startRecording(const std::string&) override { return false; }
    uint32_t stopRecording() override { return 0; }
    bool isRecording() const override { return false; }
    uint8_t inputLevel() const override { return 0; }
};

class DesktopPlaceholderNetwork : public Network {
public:
    bool isConnected() const override { return false; }
    int8_t signalStrength() const override { return 0; }
    HttpResponse request(const HttpRequest&) override {
        return HttpResponse{0, "", "desktop network not built yet (ticket 1.3)"};
    }
};

// Keeps the last colours so a later screen window can draw them.
class DesktopLeds : public Leds {
public:
    uint8_t count() const override { return 10; }
    void setPixel(uint8_t index, Colour colour) override { if (index < 10) pending_[index] = colour; }
    void setAll(Colour colour) override { for (auto& c : pending_) c = colour; }
    void show() override { for (int i = 0; i < 10; ++i) shown_[i] = pending_[i]; }
    Colour shown(uint8_t index) const { return index < 10 ? shown_[index] : Colour{}; }

private:
    Colour pending_[10];
    Colour shown_[10];
};

// A laptop is always "plugged in". Battery level can be faked for testing alerts later.
class DesktopPower : public Power {
public:
    int8_t batteryPercent() const override { return battery; }
    bool isCharging() const override { return false; }
    bool isPluggedIn() const override { return true; }

    int8_t battery = 100;
};

class DesktopPlaceholderInput : public Input {
public:
    void update() override {}
    bool pollEvent(InputEvent&) override { return false; }
};

}  // namespace fvb

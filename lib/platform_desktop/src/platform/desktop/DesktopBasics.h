#pragma once

// Desktop versions of the simpler interfaces.
//
// The screen, buttons and LEDs are shown in a window (DesktopWindow.h), sound is
// in DesktopAudio.h and the network in DesktopNetwork.h.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

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

// Keeps the last colours so the window can draw them.
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

}  // namespace fvb

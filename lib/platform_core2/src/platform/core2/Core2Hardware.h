#pragma once

// Core2 versions of the hardware interfaces.
//
// At this stage (ticket 1.1) these only need to compile. Clock, Logger, Power,
// Display and Input use M5Unified and should roughly work. Storage, settings,
// network, audio and LEDs are placeholders that ticket 2.1 (bring-up) replaces.

#include "hal/Hardware.h"

namespace fvb {

class Core2Clock : public Clock {
public:
    uint64_t millisSinceBoot() const override;
    bool isTimeSet() const override;
    int64_t unixTime() const override;
    void sleepMs(uint32_t ms) override;

    // Starts setting the clock from internet time servers (NTP), and keeps it
    // right from then on (the ESP32 checks again every hour). The clock is kept
    // in UTC; local time is worked out by app/time, never by the C library.
    // Call once Wi-Fi is connected (ticket 2.4). Safe to call again on reconnect.
    void startInternetTime();
};

class Core2SerialLogger : public Logger {
public:
    void log(Level level, const std::string& message) override;
};

class Core2Power : public Power {
public:
    int8_t batteryPercent() const override;
    bool isCharging() const override;
    bool isPluggedIn() const override;
};

class Core2Display : public Display {
public:
    uint16_t width() const override;
    uint16_t height() const override;
    void setOn(bool on) override;
    void setBrightness(uint8_t percent) override;
    void clear() override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Colour colour) override;
    void fillCircle(int16_t centreX, int16_t centreY, int16_t radius, Colour colour) override;
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, Colour colour) override;
    void drawText(int16_t x, int16_t y, const std::string& text, Colour colour = Colour{255, 255, 255},
                  uint8_t size = 2) override;
    int16_t textWidth(const std::string& text, uint8_t size = 2) const override;
    int16_t textHeight(uint8_t size = 2) const override;
    void present() override;

private:
    uint8_t brightness_ = 70;
};

class Core2Input : public Input {
public:
    void update() override;
    bool pollEvent(InputEvent& event) override;

private:
    static constexpr int kQueueSize = 8;
    void push(const InputEvent& event);
    InputEvent queue_[kQueueSize];
    int head_ = 0;
    int count_ = 0;
};

// ---- Placeholders until ticket 2.1 ----

class Core2PlaceholderFileStorage : public FileStorage {
public:
    bool isMounted() const override { return false; }
    bool exists(const std::string&) const override { return false; }
    bool readFile(const std::string&, std::string&) const override { return false; }
    bool writeFile(const std::string&, const std::string&) override { return false; }
    bool appendFile(const std::string&, const std::string&) override { return false; }
    bool removeFile(const std::string&) override { return false; }
    bool renameFile(const std::string&, const std::string&) override { return false; }
    int64_t fileSize(const std::string&) const override { return -1; }
    bool makeDir(const std::string&) override { return false; }
    bool removeDir(const std::string&) override { return false; }
    std::vector<std::string> listDir(const std::string&) const override { return {}; }
    uint64_t totalBytes() const override { return 0; }
    uint64_t usedBytes() const override { return 0; }
};

class Core2PlaceholderSettingsStore : public SettingsStore {
public:
    bool has(const std::string&) const override { return false; }
    std::string getString(const std::string&, const std::string& fallback = "") const override { return fallback; }
    bool setString(const std::string&, const std::string&) override { return false; }
    int64_t getInt(const std::string&, int64_t fallback = 0) const override { return fallback; }
    bool setInt(const std::string&, int64_t) override { return false; }
    bool remove(const std::string&) override { return false; }
};

class Core2PlaceholderNetwork : public Network {
public:
    bool isConnected() const override { return false; }
    int8_t signalStrength() const override { return 0; }
    HttpResponse request(const HttpRequest&) override { return HttpResponse{0, "", "not built yet"}; }
};

class Core2PlaceholderAudio : public Audio {
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

class Core2PlaceholderLeds : public Leds {
public:
    uint8_t count() const override { return 10; }
    void setPixel(uint8_t, Colour) override {}
    void setAll(Colour) override {}
    void show() override {}
};

}  // namespace fvb

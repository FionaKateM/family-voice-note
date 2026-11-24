#pragma once

// Core2 versions of the hardware interfaces.
//
// Everything except audio and installing updates is real as of ticket 2.1
// (bring-up). Audio comes in ticket 2.2 and installing updates in 2.7; until
// then those are placeholders.

#include "hal/FirmwareInstaller.h"
#include "hal/Hardware.h"

#include <string>
#include <vector>

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
    // Taps and swipes below this line are on the three touch buttons, which
    // M5Unified reports as button presses instead.
    static constexpr int16_t kScreenBottom = 240;
    void push(const InputEvent& event);
    InputEvent queue_[kQueueSize];
    int head_ = 0;
    int count_ = 0;
};

// ---- SD card ----

// The microSD card, formatted FAT32. Shares its connection (the SPI bus) with
// the screen; that's fine while everything runs on one task. Ticket 2.3 adds
// locking when Telegram moves onto its own task.
class Core2FileStorage : public FileStorage {
public:
    // Call once after M5.begin(). Returns false if there's no card, or it can't be read.
    bool begin();

    bool isMounted() const override { return mounted_; }
    bool exists(const std::string& path) const override;
    bool readFile(const std::string& path, std::string& contents) const override;
    bool writeFile(const std::string& path, const std::string& contents) override;
    bool appendFile(const std::string& path, const std::string& contents) override;
    bool removeFile(const std::string& path) override;
    bool renameFile(const std::string& from, const std::string& to) override;
    int64_t fileSize(const std::string& path) const override;
    bool makeDir(const std::string& path) override;
    bool removeDir(const std::string& path) override;
    std::vector<std::string> listDir(const std::string& path) const override;
    uint64_t totalBytes() const override;
    uint64_t usedBytes() const override;

private:
    bool write(const std::string& path, const std::string& contents, bool append);
    bool removeTree(const std::string& path);
    bool mounted_ = false;
};

// ---- Internal memory (NVS) ----

class Core2SettingsStore : public SettingsStore {
public:
    // Call once in setup(). Internal memory isn't ready while global objects are
    // being made, so this can't happen in the constructor.
    bool begin();

    bool has(const std::string& key) const override;
    std::string getString(const std::string& key, const std::string& fallback = "") const override;
    bool setString(const std::string& key, const std::string& value) override;
    int64_t getInt(const std::string& key, int64_t fallback = 0) const override;
    bool setInt(const std::string& key, int64_t value) override;
    bool remove(const std::string& key) override;

private:
    bool ready_ = false;
};

// ---- Wi-Fi and HTTPS ----

// Joins a Wi-Fi network, waiting up to timeoutMs. Remembering several networks
// and the setup hotspot come in ticket 2.4.
bool core2JoinWifi(const std::string& name, const std::string& password, uint32_t timeoutMs);

// HTTPS requests over Wi-Fi. Each Core2Network keeps its own connection open
// between requests, so the box can have one for the long poll and one for
// sending, as the desktop build does. Servers are checked against the root
// certificates in RootCertificates.h, and plain HTTP is refused.
class Core2Network : public Network {
public:
    Core2Network();
    ~Core2Network() override;

    bool isConnected() const override;
    int8_t signalStrength() const override;
    std::string wifiName() const override;
    HttpResponse request(const HttpRequest& request) override;

private:
    struct Connection;  // the TLS client and HTTP client, kept out of this header
    Connection* connection_;
};

// ---- LEDs ----

// The 10 RGB LEDs on the M5GO Bottom2 base (the one with the side light bars).
class Core2Leds : public Leds {
public:
    void begin();
    uint8_t count() const override { return 10; }
    void setPixel(uint8_t index, Colour colour) override;
    void setAll(Colour colour) override;
    void show() override;
};

// Which Core2 this is, for /status. The two hardware versions use different
// power chips, which M5Unified tells apart. Call after M5.begin().
std::string core2Description();

// ---- Placeholders ----

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


// Installing over the air comes in ticket 2.7 (using the ESP32's two app
// slots and its automatic rollback). Until then it refuses, but restarting
// works.
class Core2PlaceholderFirmwareInstaller : public FirmwareInstaller {
public:
    bool begin(size_t, const std::string&, std::string& error) override {
        error = "installing updates on the device isn't built yet";
        return false;
    }
    bool write(const uint8_t*, size_t) override { return false; }
    bool finish(std::string& error) override {
        error = "installing updates on the device isn't built yet";
        return false;
    }
    void abort() override {}
    void restart() override;
    void markGood() override {}
};

}  // namespace fvb

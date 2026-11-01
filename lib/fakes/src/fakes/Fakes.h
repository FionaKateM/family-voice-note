#pragma once

// In-memory versions of every hardware interface, for tests.
// They record what was asked of them so tests can check it.

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "hal/Hardware.h"

namespace fvb {
namespace fakes {

class FakeAudio : public Audio {
public:
    bool play(const std::string& path) override { playing = path; return true; }
    void stopPlayback() override { playing.clear(); }
    bool isPlaying() const override { return !playing.empty(); }
    void setVolume(uint8_t percent) override { volume = percent; }
    bool startRecording(const std::string& path) override { recording = path; return true; }
    uint32_t stopRecording() override { recording.clear(); return nextRecordingMs; }
    bool isRecording() const override { return !recording.empty(); }
    uint8_t inputLevel() const override { return level; }

    std::string playing;
    std::string recording;
    uint8_t volume = 0;
    uint8_t level = 0;
    uint32_t nextRecordingMs = 0;
};

class FakeDisplay : public Display {
public:
    uint16_t width() const override { return 320; }
    uint16_t height() const override { return 240; }
    void setOn(bool value) override { on = value; }
    void setBrightness(uint8_t percent) override { brightness = percent; }
    void clear() override { pending.clear(); }
    void drawText(int16_t, int16_t, const std::string& text) override { pending.push_back(text); }
    void present() override { shown = pending; ++presentCount; }

    bool shows(const std::string& text) const {
        return std::any_of(shown.begin(), shown.end(),
                           [&](const std::string& s) { return s.find(text) != std::string::npos; });
    }

    bool on = false;
    uint8_t brightness = 100;
    std::vector<std::string> pending;
    std::vector<std::string> shown;
    int presentCount = 0;
};

class FakeFileStorage : public FileStorage {
public:
    bool isMounted() const override { return mounted; }
    bool exists(const std::string& path) const override { return files.count(path) > 0; }
    bool readFile(const std::string& path, std::string& contents) const override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        contents = it->second;
        return true;
    }
    bool writeFile(const std::string& path, const std::string& contents) override {
        if (readOnly) return false;
        files[path] = contents;
        return true;
    }
    bool appendFile(const std::string& path, const std::string& contents) override {
        if (readOnly) return false;
        files[path] += contents;
        return true;
    }
    bool removeFile(const std::string& path) override { return !readOnly && files.erase(path) > 0; }
    bool renameFile(const std::string& from, const std::string& to) override {
        if (readOnly) return false;
        auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second;
        files.erase(from);
        return true;
    }
    int64_t fileSize(const std::string& path) const override {
        auto it = files.find(path);
        return it == files.end() ? -1 : static_cast<int64_t>(it->second.size());
    }
    // Folders aren't modelled: a folder "exists" if any file is inside it.
    bool makeDir(const std::string&) override { return true; }
    bool removeDir(const std::string& path) override {
        const std::string prefix = path.back() == '/' ? path : path + "/";
        for (auto it = files.begin(); it != files.end();) {
            it = it->first.rfind(prefix, 0) == 0 ? files.erase(it) : std::next(it);
        }
        return true;
    }
    std::vector<std::string> listDir(const std::string& path) const override {
        const std::string prefix = path.back() == '/' ? path : path + "/";
        std::vector<std::string> names;
        for (const auto& entry : files) {
            if (entry.first.rfind(prefix, 0) != 0) continue;
            std::string rest = entry.first.substr(prefix.size());
            std::string name = rest.substr(0, rest.find('/'));
            if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        }
        return names;
    }
    uint64_t totalBytes() const override { return capacity; }
    uint64_t usedBytes() const override {
        uint64_t used = 0;
        for (const auto& entry : files) used += entry.second.size();
        return used;
    }

    bool mounted = true;
    bool readOnly = false;  // set to make every change fail, like a locked or broken card
    uint64_t capacity = 16ULL * 1024 * 1024 * 1024;
    std::map<std::string, std::string> files;
};

class FakeSettingsStore : public SettingsStore {
public:
    bool has(const std::string& key) const override { return values.count(key) > 0; }
    std::string getString(const std::string& key, const std::string& fallback) const override {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
    bool setString(const std::string& key, const std::string& value) override {
        if (!isValidKey(key) || !isValidValue(value) || failWrites) return false;
        values[key] = value;
        return true;
    }
    int64_t getInt(const std::string& key, int64_t fallback) const override {
        auto it = values.find(key);
        return it == values.end() ? fallback : std::stoll(it->second);
    }
    bool setInt(const std::string& key, int64_t value) override { return setString(key, std::to_string(value)); }
    bool remove(const std::string& key) override { return values.erase(key) > 0; }

    std::map<std::string, std::string> values;
    bool failWrites = false;  // set to pretend the internal memory is broken
};

class FakeNetwork : public Network {
public:
    bool isConnected() const override { return connected; }
    int8_t signalStrength() const override { return connected ? -60 : 0; }
    HttpResponse request(const HttpRequest& req) override {
        requests.push_back(req);
        if (responses.empty()) return HttpResponse{0, "", "no fake response queued"};
        HttpResponse r = responses.front();
        responses.pop_front();
        return r;
    }

    bool connected = true;
    std::vector<HttpRequest> requests;
    std::deque<HttpResponse> responses;
};

class FakeClock : public Clock {
public:
    uint64_t millisSinceBoot() const override { return bootMs; }
    bool isTimeSet() const override { return timeSet; }
    int64_t unixTime() const override { return unixSeconds; }
    void sleepMs(uint32_t ms) override { bootMs += ms; unixSeconds += ms / 1000; }

    uint64_t bootMs = 0;
    bool timeSet = false;
    int64_t unixSeconds = 0;
};

class FakeLeds : public Leds {
public:
    FakeLeds() : pending(10, Colour{1, 1, 1}), shown(10, Colour{1, 1, 1}) {}
    uint8_t count() const override { return 10; }
    void setPixel(uint8_t index, Colour colour) override { if (index < pending.size()) pending[index] = colour; }
    void setAll(Colour colour) override { std::fill(pending.begin(), pending.end(), colour); }
    void show() override { shown = pending; ++showCount; }

    bool allShown(Colour colour) const {
        return std::all_of(shown.begin(), shown.end(), [&](Colour c) { return c == colour; });
    }

    std::vector<Colour> pending;
    std::vector<Colour> shown;
    int showCount = 0;
};

class FakePower : public Power {
public:
    int8_t batteryPercent() const override { return battery; }
    bool isCharging() const override { return charging; }
    bool isPluggedIn() const override { return pluggedIn; }

    int8_t battery = 100;
    bool charging = false;
    bool pluggedIn = true;
};

class FakeInput : public Input {
public:
    void update() override { ++updateCount; }
    bool pollEvent(InputEvent& event) override {
        if (queue.empty()) return false;
        event = queue.front();
        queue.pop_front();
        return true;
    }
    void press(Button button) {
        InputEvent e;
        e.type = InputEvent::Type::ButtonPress;
        e.button = button;
        queue.push_back(e);
    }

    std::deque<InputEvent> queue;
    int updateCount = 0;
};

class FakeLogger : public Logger {
public:
    void log(Level level, const std::string& message) override { lines.emplace_back(level, message); }

    bool contains(const std::string& text) const {
        return std::any_of(lines.begin(), lines.end(),
                           [&](const auto& l) { return l.second.find(text) != std::string::npos; });
    }

    std::vector<std::pair<Level, std::string>> lines;
};

// All the fakes together, plus a Hardware that points at them.
struct FakeHardware {
    FakeAudio audio;
    FakeDisplay display;
    FakeFileStorage files;
    FakeSettingsStore settings;
    FakeNetwork network;
    FakeClock clock;
    FakeLeds leds;
    FakePower power;
    FakeInput input;
    FakeLogger log;

    Hardware hardware{audio, display, files, settings, network, clock, leds, power, input, log};
};

}  // namespace fakes
}  // namespace fvb

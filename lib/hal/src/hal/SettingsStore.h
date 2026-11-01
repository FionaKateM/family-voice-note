#pragma once

#include <cstdint>
#include <string>

namespace fvb {

// Small values kept in the ESP32's internal memory (NVS): settings, the bot token,
// people lists. Survives the SD card being removed or swapped.
//
// Keys are limited to 15 characters because that is NVS's limit. Implementations
// refuse longer keys so the desktop build catches them before the device does.
class SettingsStore {
public:
    static constexpr size_t kMaxKeyLength = 15;

    virtual ~SettingsStore() = default;

    virtual bool has(const std::string& key) const = 0;
    virtual std::string getString(const std::string& key, const std::string& fallback = "") const = 0;
    virtual bool setString(const std::string& key, const std::string& value) = 0;
    virtual int64_t getInt(const std::string& key, int64_t fallback = 0) const = 0;
    virtual bool setInt(const std::string& key, int64_t value) = 0;
    virtual bool remove(const std::string& key) = 0;

    static bool isValidKey(const std::string& key) {
        return !key.empty() && key.size() <= kMaxKeyLength;
    }
};

}  // namespace fvb

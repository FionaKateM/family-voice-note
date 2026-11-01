#pragma once

#include <filesystem>
#include <string>

#include "hal/SettingsStore.h"

namespace fvb {

// Stands in for the ESP32's internal memory. Each key is a small file in a folder.
// Keys may only use letters, numbers and "_" (so they're safe as file names),
// and are limited to 15 characters like the real thing.
class DesktopSettingsStore : public SettingsStore {
public:
    explicit DesktopSettingsStore(std::filesystem::path folder);

    bool has(const std::string& key) const override;
    std::string getString(const std::string& key, const std::string& fallback = "") const override;
    bool setString(const std::string& key, const std::string& value) override;
    int64_t getInt(const std::string& key, int64_t fallback = 0) const override;
    bool setInt(const std::string& key, int64_t value) override;
    bool remove(const std::string& key) override;

private:
    bool pathFor(const std::string& key, std::filesystem::path& out) const;

    std::filesystem::path folder_;
};

}  // namespace fvb

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/SettingsStore.h"

namespace fvb {

enum class MonitorMode : uint8_t { Off = 0, SentOnly = 1, ReceivedOnly = 2, All = 3 };

struct WifiNetwork {
    std::string ssid;
    std::string password;  // empty for an open network
};

// The box's settings, kept in internal memory (NVS). Each getter returns the
// default from section 12 of the requirements until an admin changes it.
//
// Setters refuse values that make no sense (a volume of 150, a quiet hour of 25:00)
// and return false, so the bot can tell the admin rather than store rubbish.
class DeviceSettings {
public:
    static constexpr uint8_t kDefaultVoiceVolume = 70;
    static constexpr uint8_t kDefaultAlertVolume = 50;
    static constexpr uint16_t kDefaultQuietStart = 19 * 60;  // minutes after midnight
    static constexpr uint16_t kDefaultQuietEnd = 7 * 60;
    static constexpr uint32_t kDefaultMaxRecordingSeconds = 360;
    static constexpr uint32_t kMinMaxRecordingSeconds = 10;
    static constexpr uint32_t kMaxMaxRecordingSeconds = 15 * 60;
    static constexpr uint32_t kDefaultKeepDays = 60;  // 0 means keep forever
    static constexpr uint32_t kMaxKeepDays = 3650;
    static constexpr size_t kMaxWifiNetworks = 5;

    explicit DeviceSettings(SettingsStore& store) : store_(store) {}

    // From the setup file.
    std::string childName() const;
    bool setChildName(const std::string& name);
    std::string botToken() const;
    bool hasBotToken() const;
    bool setBotToken(const std::string& token);

    // Device timezone as a name like "Europe/London". Empty until confirmed.
    // setTimezone refuses names that aren't in the built-in table (app/time/TimeZones.h).
    std::string timezone() const;
    bool setTimezone(const std::string& name);

    // Quiet hours in minutes after midnight. Start can be later than end (crosses midnight).
    uint16_t quietStart() const;
    uint16_t quietEnd() const;
    bool setQuietHours(uint16_t startMinutes, uint16_t endMinutes);

    uint8_t voiceVolume() const;  // 0 to 100
    bool setVoiceVolume(uint8_t percent);
    uint8_t alertVolume() const;  // 0 to 100
    bool setAlertVolume(uint8_t percent);

    uint32_t maxRecordingSeconds() const;
    bool setMaxRecordingSeconds(uint32_t seconds);

    uint32_t keepDays() const;
    bool setKeepDays(uint32_t days);

    MonitorMode monitorMode() const;
    bool setMonitorMode(MonitorMode mode);

    // Known Wi-Fi networks, most recently added first.
    std::vector<WifiNetwork> wifiNetworks() const;
    // Adds a network, or updates its password if it's already known, and moves it
    // to the front. The oldest is forgotten if there are already kMaxWifiNetworks.
    bool addWifiNetwork(const WifiNetwork& network);
    bool removeWifiNetwork(const std::string& ssid);

private:
    bool setBoundedInt(const char* key, int64_t value, int64_t min, int64_t max);
    bool saveWifi(const std::vector<WifiNetwork>& networks);

    SettingsStore& store_;
};

}  // namespace fvb

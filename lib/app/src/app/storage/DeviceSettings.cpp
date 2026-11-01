#include "app/storage/DeviceSettings.h"

#include <algorithm>

#include "app/storage/TextRecord.h"

namespace fvb {

namespace {
// NVS keys. 15 characters at most.
constexpr const char* kChildName = "child_name";
constexpr const char* kBotToken = "bot_token";
constexpr const char* kTimezone = "timezone";
constexpr const char* kQuietStart = "quiet_start";
constexpr const char* kQuietEnd = "quiet_end";
constexpr const char* kVoiceVolume = "voice_vol";
constexpr const char* kAlertVolume = "alert_vol";
constexpr const char* kMaxRecording = "max_rec_s";
constexpr const char* kKeepDays = "keep_days";
constexpr const char* kMonitorMode = "monitor_mode";
constexpr const char* kWifi = "wifi";

constexpr uint16_t kMinutesInDay = 24 * 60;
}  // namespace

std::string DeviceSettings::childName() const { return store_.getString(kChildName, "Child"); }

bool DeviceSettings::setChildName(const std::string& name) {
    const std::string clean = text::trim(name);
    if (clean.empty()) return false;
    return store_.setString(kChildName, clean);
}

std::string DeviceSettings::botToken() const { return store_.getString(kBotToken); }
bool DeviceSettings::hasBotToken() const { return !botToken().empty(); }

bool DeviceSettings::setBotToken(const std::string& token) {
    if (token.empty()) return false;
    return store_.setString(kBotToken, token);
}

std::string DeviceSettings::timezone() const { return store_.getString(kTimezone); }

bool DeviceSettings::setTimezone(const std::string& name) {
    if (name.empty()) return false;
    return store_.setString(kTimezone, name);
}

uint16_t DeviceSettings::quietStart() const {
    return static_cast<uint16_t>(store_.getInt(kQuietStart, kDefaultQuietStart));
}

uint16_t DeviceSettings::quietEnd() const {
    return static_cast<uint16_t>(store_.getInt(kQuietEnd, kDefaultQuietEnd));
}

bool DeviceSettings::setQuietHours(uint16_t startMinutes, uint16_t endMinutes) {
    if (startMinutes >= kMinutesInDay || endMinutes >= kMinutesInDay) return false;
    return store_.setInt(kQuietStart, startMinutes) && store_.setInt(kQuietEnd, endMinutes);
}

uint8_t DeviceSettings::voiceVolume() const {
    return static_cast<uint8_t>(store_.getInt(kVoiceVolume, kDefaultVoiceVolume));
}
bool DeviceSettings::setVoiceVolume(uint8_t percent) { return setBoundedInt(kVoiceVolume, percent, 0, 100); }

uint8_t DeviceSettings::alertVolume() const {
    return static_cast<uint8_t>(store_.getInt(kAlertVolume, kDefaultAlertVolume));
}
bool DeviceSettings::setAlertVolume(uint8_t percent) { return setBoundedInt(kAlertVolume, percent, 0, 100); }

uint32_t DeviceSettings::maxRecordingSeconds() const {
    return static_cast<uint32_t>(store_.getInt(kMaxRecording, kDefaultMaxRecordingSeconds));
}
bool DeviceSettings::setMaxRecordingSeconds(uint32_t seconds) {
    return setBoundedInt(kMaxRecording, seconds, kMinMaxRecordingSeconds, kMaxMaxRecordingSeconds);
}

uint32_t DeviceSettings::keepDays() const { return static_cast<uint32_t>(store_.getInt(kKeepDays, kDefaultKeepDays)); }
bool DeviceSettings::setKeepDays(uint32_t days) { return setBoundedInt(kKeepDays, days, 0, kMaxKeepDays); }

MonitorMode DeviceSettings::monitorMode() const {
    const int64_t value = store_.getInt(kMonitorMode, 0);
    if (value < 0 || value > 3) return MonitorMode::Off;
    return static_cast<MonitorMode>(value);
}

bool DeviceSettings::setMonitorMode(MonitorMode mode) {
    return setBoundedInt(kMonitorMode, static_cast<int64_t>(mode), 0, 3);
}

std::vector<WifiNetwork> DeviceSettings::wifiNetworks() const {
    std::vector<WifiNetwork> networks;
    for (const auto& line : text::splitLines(store_.getString(kWifi))) {
        const auto fields = text::splitFields(line);
        if (fields.size() < 2 || fields[0].empty()) continue;
        networks.push_back({fields[0], fields[1]});
    }
    return networks;
}

bool DeviceSettings::addWifiNetwork(const WifiNetwork& network) {
    if (network.ssid.empty()) return false;
    auto networks = wifiNetworks();
    networks.erase(std::remove_if(networks.begin(), networks.end(),
                                  [&](const WifiNetwork& n) { return n.ssid == network.ssid; }),
                   networks.end());
    networks.insert(networks.begin(), network);
    if (networks.size() > kMaxWifiNetworks) networks.resize(kMaxWifiNetworks);
    return saveWifi(networks);
}

bool DeviceSettings::removeWifiNetwork(const std::string& ssid) {
    auto networks = wifiNetworks();
    const auto before = networks.size();
    networks.erase(std::remove_if(networks.begin(), networks.end(),
                                  [&](const WifiNetwork& n) { return n.ssid == ssid; }),
                   networks.end());
    if (networks.size() == before) return false;
    return saveWifi(networks);
}

bool DeviceSettings::setBoundedInt(const char* key, int64_t value, int64_t min, int64_t max) {
    if (value < min || value > max) return false;
    return store_.setInt(key, value);
}

bool DeviceSettings::saveWifi(const std::vector<WifiNetwork>& networks) {
    std::string out;
    for (const auto& n : networks) out += text::joinFields({n.ssid, n.password}) + "\n";
    return store_.setString(kWifi, out);
}

}  // namespace fvb

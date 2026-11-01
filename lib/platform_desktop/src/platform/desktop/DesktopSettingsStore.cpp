#include "platform/desktop/DesktopSettingsStore.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace fvb {

DesktopSettingsStore::DesktopSettingsStore(fs::path folder) : folder_(std::move(folder)) {
    std::error_code ec;
    fs::create_directories(folder_, ec);
}

bool DesktopSettingsStore::pathFor(const std::string& key, fs::path& out) const {
    if (!isValidKey(key)) return false;
    for (char c : key) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    out = folder_ / key;
    return true;
}

bool DesktopSettingsStore::has(const std::string& key) const {
    fs::path p;
    std::error_code ec;
    return pathFor(key, p) && fs::is_regular_file(p, ec);
}

std::string DesktopSettingsStore::getString(const std::string& key, const std::string& fallback) const {
    fs::path p;
    if (!pathFor(key, p)) return fallback;
    std::ifstream in(p, std::ios::binary);
    if (!in) return fallback;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool DesktopSettingsStore::setString(const std::string& key, const std::string& value) {
    fs::path p;
    if (!pathFor(key, p)) return false;
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

int64_t DesktopSettingsStore::getInt(const std::string& key, int64_t fallback) const {
    if (!has(key)) return fallback;
    const std::string text = getString(key);
    try {
        size_t used = 0;
        const long long value = std::stoll(text, &used);
        return used == text.size() ? value : fallback;
    } catch (...) {
        return fallback;
    }
}

bool DesktopSettingsStore::setInt(const std::string& key, int64_t value) {
    return setString(key, std::to_string(value));
}

bool DesktopSettingsStore::remove(const std::string& key) {
    fs::path p;
    std::error_code ec;
    return pathFor(key, p) && fs::remove(p, ec);
}

}  // namespace fvb

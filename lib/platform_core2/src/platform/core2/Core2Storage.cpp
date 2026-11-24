// The SD card and the ESP32's internal memory (NVS) on the Core2.

#include <FS.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

#include "platform/core2/Core2Hardware.h"

namespace fvb {

namespace {

// The Core2's SD card slot is on the same SPI bus as the screen, with its own
// chip select pin.
constexpr int kSdChipSelect = 4;
constexpr uint32_t kSdSpeedHz = 25000000;

// Same rules as the desktop build: paths start with "/" and never contain "..".
bool validPath(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    size_t start = 1;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        if (path.compare(start, end - start, "..") == 0 && end - start == 2) return false;
        start = end + 1;
    }
    return true;
}

std::string parentOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return path.substr(0, slash);
}

// The last part of a path. File::name() already gives this on current
// versions of the ESP32 Arduino core, but older ones gave the whole path.
std::string baseName(const char* name) {
    std::string s = name ? name : "";
    const size_t slash = s.find_last_of('/');
    return slash == std::string::npos ? s : s.substr(slash + 1);
}

bool isDirectory(const std::string& path) {
    File f = SD.open(path.c_str());
    const bool dir = f && f.isDirectory();
    if (f) f.close();
    return dir;
}

// Settings in internal memory live under this name.
constexpr const char* kNvsNamespace = "fvb";
Preferences prefs;

}  // namespace

// ---- SD card ----

bool Core2FileStorage::begin() {
    mounted_ = SD.begin(kSdChipSelect, SPI, kSdSpeedHz);
    return mounted_;
}

bool Core2FileStorage::exists(const std::string& path) const {
    if (!mounted_ || !validPath(path)) return false;
    return SD.exists(path.c_str());
}

bool Core2FileStorage::readFile(const std::string& path, std::string& contents) const {
    if (!mounted_ || !validPath(path)) return false;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    if (f.isDirectory()) {
        f.close();
        return false;
    }
    const size_t size = f.size();
    contents.assign(size, '\0');
    size_t got = 0;
    while (got < size) {
        const size_t n = f.read(reinterpret_cast<uint8_t*>(&contents[got]), size - got);
        if (n == 0) break;
        got += n;
    }
    f.close();
    if (got != size) {
        contents.clear();
        return false;
    }
    return true;
}

bool Core2FileStorage::write(const std::string& path, const std::string& contents, bool append) {
    if (!mounted_ || !validPath(path) || path == "/") return false;
    if (!makeDir(parentOf(path))) return false;
    File f = SD.open(path.c_str(), append ? FILE_APPEND : FILE_WRITE);
    if (!f) return false;
    const size_t written = contents.empty() ? 0 : f.write(reinterpret_cast<const uint8_t*>(contents.data()), contents.size());
    f.close();
    return written == contents.size();
}

bool Core2FileStorage::writeFile(const std::string& path, const std::string& contents) {
    return write(path, contents, false);
}

bool Core2FileStorage::appendFile(const std::string& path, const std::string& contents) {
    return write(path, contents, true);
}

bool Core2FileStorage::removeFile(const std::string& path) {
    if (!mounted_ || !validPath(path) || isDirectory(path)) return false;
    return SD.remove(path.c_str());
}

bool Core2FileStorage::renameFile(const std::string& from, const std::string& to) {
    if (!mounted_ || !validPath(from) || !validPath(to)) return false;
    if (!SD.exists(from.c_str())) return false;
    if (!makeDir(parentOf(to))) return false;
    // FAT won't rename over an existing file, so remove it first.
    if (SD.exists(to.c_str()) && !isDirectory(to)) SD.remove(to.c_str());
    return SD.rename(from.c_str(), to.c_str());
}

int64_t Core2FileStorage::fileSize(const std::string& path) const {
    if (!mounted_ || !validPath(path)) return -1;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return -1;
    const int64_t size = f.isDirectory() ? -1 : static_cast<int64_t>(f.size());
    f.close();
    return size;
}

bool Core2FileStorage::makeDir(const std::string& path) {
    if (!mounted_ || !validPath(path)) return false;
    if (path == "/") return true;
    // Make each folder on the way down.
    size_t next = 1;
    while (true) {
        const size_t slash = path.find('/', next);
        const std::string part = slash == std::string::npos ? path : path.substr(0, slash);
        if (!part.empty() && part != "/") {
            if (SD.exists(part.c_str())) {
                if (!isDirectory(part)) return false;  // a file is in the way
            } else if (!SD.mkdir(part.c_str())) {
                return false;
            }
        }
        if (slash == std::string::npos || slash + 1 >= path.size()) break;
        next = slash + 1;
    }
    return true;
}

bool Core2FileStorage::removeTree(const std::string& path) {
    File dir = SD.open(path.c_str());
    if (!dir) return false;
    if (!dir.isDirectory()) {
        dir.close();
        return SD.remove(path.c_str());
    }
    // Collect the names first: removing while walking a FAT folder skips entries.
    std::vector<std::string> children;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        children.push_back(path + "/" + baseName(f.name()));
        f.close();
    }
    dir.close();
    bool ok = true;
    for (const auto& child : children) ok = removeTree(child) && ok;
    return SD.rmdir(path.c_str()) && ok;
}

bool Core2FileStorage::removeDir(const std::string& path) {
    if (!mounted_ || !validPath(path) || path == "/") return false;  // never remove the whole card
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    if (!isDirectory(p)) return false;
    return removeTree(p);
}

std::vector<std::string> Core2FileStorage::listDir(const std::string& path) const {
    std::vector<std::string> names;
    if (!mounted_ || !validPath(path)) return names;
    File dir = SD.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return names;
    }
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        names.push_back(baseName(f.name()));
        f.close();
    }
    dir.close();
    return names;
}

uint64_t Core2FileStorage::totalBytes() const { return mounted_ ? SD.totalBytes() : 0; }

uint64_t Core2FileStorage::usedBytes() const { return mounted_ ? SD.usedBytes() : 0; }

// ---- Internal memory ----

bool Core2SettingsStore::begin() {
    ready_ = prefs.begin(kNvsNamespace, false);
    return ready_;
}

bool Core2SettingsStore::has(const std::string& key) const {
    if (!ready_ || !isValidKey(key)) return false;
    return prefs.isKey(key.c_str());
}

std::string Core2SettingsStore::getString(const std::string& key, const std::string& fallback) const {
    if (!ready_ || !isValidKey(key) || !prefs.isKey(key.c_str())) return fallback;
    // The desktop build keeps numbers as text, so do the same here when a
    // number is read as a string.
    if (prefs.getType(key.c_str()) == PT_I64) return std::to_string(prefs.getLong64(key.c_str(), 0));
    if (prefs.getType(key.c_str()) != PT_STR) return fallback;
    const String value = prefs.getString(key.c_str(), "");
    return std::string(value.c_str(), value.length());
}

bool Core2SettingsStore::setString(const std::string& key, const std::string& value) {
    if (!ready_ || !isValidKey(key) || !isValidValue(value)) return false;
    // NVS won't change a key's type in place, so clear it if it held a number.
    if (prefs.isKey(key.c_str()) && prefs.getType(key.c_str()) != PT_STR) prefs.remove(key.c_str());
    // putString returns the length written, which is 0 for an empty string, so
    // an empty value counts as saved if the key now exists.
    const size_t written = prefs.putString(key.c_str(), value.c_str());
    return value.empty() ? prefs.isKey(key.c_str()) : written == value.size();
}

int64_t Core2SettingsStore::getInt(const std::string& key, int64_t fallback) const {
    if (!ready_ || !isValidKey(key) || !prefs.isKey(key.c_str())) return fallback;
    if (prefs.getType(key.c_str()) == PT_I64) return prefs.getLong64(key.c_str(), fallback);
    // A number saved as text: read it the way the desktop build does.
    const std::string text = getString(key);
    if (text.empty()) return fallback;
    char* end = nullptr;
    const long long value = strtoll(text.c_str(), &end, 10);
    return (end && *end == '\0') ? value : fallback;
}

bool Core2SettingsStore::setInt(const std::string& key, int64_t value) {
    if (!ready_ || !isValidKey(key)) return false;
    if (prefs.isKey(key.c_str()) && prefs.getType(key.c_str()) != PT_I64) prefs.remove(key.c_str());
    return prefs.putLong64(key.c_str(), value) == sizeof(int64_t);
}

bool Core2SettingsStore::remove(const std::string& key) {
    if (!ready_ || !isValidKey(key)) return false;
    if (!prefs.isKey(key.c_str())) return false;
    return prefs.remove(key.c_str());
}

}  // namespace fvb

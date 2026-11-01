#include "app/storage/Bin.h"

#include <algorithm>

#include "app/storage/Paths.h"
#include "app/storage/TextRecord.h"

namespace fvb {

namespace {

// Keeps letters and numbers only, so the name is safe on any SD card. Names in
// other alphabets become empty, which is fine: the contact number is still there.
std::string safeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
        if (out.size() >= 20) break;
    }
    return out;
}

// "20261101-143205-c3-Granny.mp3" (or "...-c3.mp3", or "...-c3-Granny-2.mp3" if
// two land in the same second).
bool parseName(const std::string& name, BinEntry& out) {
    if (name.size() < 19) return false;
    int64_t time;
    if (!parseCompactTime(name.substr(0, 15), time)) return false;
    if (name.compare(15, 2, "-c") != 0) return false;
    size_t pos = 17;
    size_t end = pos;
    while (end < name.size() && name[end] >= '0' && name[end] <= '9') ++end;
    uint64_t contact;
    if (!text::parseUint(name.substr(pos, end - pos), contact)) return false;
    out.fileName = name;
    out.time = time;
    out.contact = static_cast<uint32_t>(contact);
    return true;
}

}  // namespace

std::string BinEntry::path() const { return std::string(paths::kBinDir) + "/" + fileName; }

std::string Bin::add(const std::string& sourcePath, uint32_t contact, const std::string& contactName, int64_t now) {
    if (!files_.exists(sourcePath)) return "";

    std::string extension;
    const size_t dot = sourcePath.find_last_of('.');
    const size_t slash = sourcePath.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) extension = sourcePath.substr(dot);

    std::string base = formatCompactTime(now) + "-c" + std::to_string(contact);
    const std::string name = safeName(contactName);
    if (!name.empty()) base += "-" + name;

    std::string destination = std::string(paths::kBinDir) + "/" + base + extension;
    for (int n = 2; files_.exists(destination); ++n) {
        destination = std::string(paths::kBinDir) + "/" + base + "-" + std::to_string(n) + extension;
    }
    return files_.renameFile(sourcePath, destination) ? destination : "";
}

std::vector<BinEntry> Bin::entries() const {
    std::vector<BinEntry> result;
    for (const auto& name : files_.listDir(paths::kBinDir)) {
        BinEntry e;
        if (parseName(name, e)) result.push_back(e);
    }
    // Same-second names sort correctly too, because "-2" comes after the plain name.
    std::sort(result.begin(), result.end(), [](const BinEntry& a, const BinEntry& b) {
        return a.time != b.time ? a.time < b.time : a.fileName.size() != b.fileName.size()
                                                        ? a.fileName.size() < b.fileName.size()
                                                        : a.fileName < b.fileName;
    });
    return result;
}

bool Bin::newest(BinEntry& out) const {
    const auto list = entries();
    if (list.empty()) return false;
    out = list.back();
    return true;
}

size_t Bin::removeExpired(int64_t now) {
    size_t removed = 0;
    for (const auto& e : entries()) {
        // A date before 2024 means the clock wasn't set when it was binned. Its real
        // age is unknown, so keep it rather than risk deleting something recent.
        if (e.time < kEarliestRealTime) continue;
        if (now - e.time > kKeepSeconds && files_.removeFile(e.path())) ++removed;
    }
    return removed;
}

}  // namespace fvb

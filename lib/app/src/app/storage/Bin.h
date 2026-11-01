#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/FileStorage.h"

namespace fvb {

struct BinEntry {
    std::string fileName;  // e.g. "20261101-143205-c3-Granny.mp3"
    int64_t time = 0;      // when it was binned, seconds since 1970, UTC
    uint32_t contact = 0;  // contact number it was recorded for
    std::string path() const;
};

// Recordings the child deleted or recorded over. Kept in /bin, which the child
// never sees, named with the date, time and contact so a parent can find one.
class Bin {
public:
    static constexpr int64_t kKeepSeconds = 30LL * 24 * 60 * 60;
    static constexpr int64_t kEarliestRealTime = 1704067200;  // 1 January 2024

    explicit Bin(FileStorage& files) : files_(files) {}

    // Moves the recording at sourcePath into the bin. contactName is only used to
    // make the file name readable. Returns the bin entry's path, or "" on failure.
    std::string add(const std::string& sourcePath, uint32_t contact, const std::string& contactName, int64_t now);

    // Oldest first.
    std::vector<BinEntry> entries() const;
    // The most recently binned recording, for /trash getlast. False if the bin is empty.
    bool newest(BinEntry& out) const;
    // Permanently deletes anything binned more than 30 days before now. Returns how many.
    // Recordings binned before the clock was set are kept.
    size_t removeExpired(int64_t now);

private:
    FileStorage& files_;
};

}  // namespace fvb

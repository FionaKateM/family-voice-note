#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fvb {

// SHA-256, the checksum published with each release. Fed a piece at a time so
// a firmware download can be checked as it arrives, without holding the whole
// file in memory.
//
// Written out here (rather than using the ESP32's or the laptop's crypto
// library) so the same code runs and is tested on both builds. It's only used
// once per update, so speed doesn't matter.
class Sha256 {
public:
    Sha256() { reset(); }

    void reset();
    void add(const void* data, size_t length);
    void add(const std::string& data) { add(data.data(), data.size()); }
    // The checksum as 64 lower-case hex characters. Call once, after all the data.
    std::string finishHex();

    static std::string of(const std::string& data) {
        Sha256 s;
        s.add(data);
        return s.finishHex();
    }

private:
    void block(const uint8_t* p);

    uint32_t h_[8];
    uint8_t buffer_[64];
    size_t buffered_ = 0;
    uint64_t totalBytes_ = 0;
};

}  // namespace fvb

#pragma once

#include <cstdint>

namespace fvb {

struct Colour {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const Colour& other) const { return r == other.r && g == other.g && b == other.b; }
    bool operator!=(const Colour& other) const { return !(*this == other); }
};

}  // namespace fvb

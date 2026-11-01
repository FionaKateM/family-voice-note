#pragma once

#include <cstdint>

namespace fvb {

// Unpredictable numbers, used for invite codes. Must be good enough that nobody
// can guess a code: the ESP32's hardware generator on the device, the
// operating system's on a laptop.
//
// Not part of Hardware (yet) because only the bot needs it. It can move in
// there when the bot runs on the device (ticket 2.3).
class Random {
public:
    virtual ~Random() = default;
    virtual uint32_t next() = 0;
};

}  // namespace fvb

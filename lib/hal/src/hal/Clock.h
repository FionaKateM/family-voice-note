#pragma once

#include <cstdint>

namespace fvb {

class Clock {
public:
    virtual ~Clock() = default;

    // Time since the box started. Always available, never goes backwards.
    virtual uint64_t millisSinceBoot() const = 0;

    // Real time from the internet, as seconds since 1 January 1970 (UTC).
    // Only meaningful once isTimeSet() is true.
    virtual bool isTimeSet() const = 0;
    virtual int64_t unixTime() const = 0;

    virtual void sleepMs(uint32_t ms) = 0;
};

}  // namespace fvb

#pragma once

#include <cstdint>

namespace fvb {

class Power {
public:
    virtual ~Power() = default;

    // 0 to 100, or -1 if it can't be read.
    virtual int8_t batteryPercent() const = 0;
    virtual bool isCharging() const = 0;
    virtual bool isPluggedIn() const = 0;
};

}  // namespace fvb

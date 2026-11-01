#pragma once

#include <random>

#include "hal/Random.h"

namespace fvb {

// The operating system's random numbers (/dev/urandom on Linux and macOS).
class DesktopRandom : public Random {
public:
    uint32_t next() override { return device_(); }

private:
    std::random_device device_;
};

}  // namespace fvb

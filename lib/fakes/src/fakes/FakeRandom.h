#pragma once

#include <cstdint>

#include "hal/Random.h"

namespace fvb {
namespace fakes {

// The same numbers every run, so tests can predict invite codes.
class FakeRandom : public Random {
public:
    uint32_t next() override {
        value = value * 1664525u + 1013904223u;
        return value;
    }
    uint32_t value = 1;
};

}  // namespace fakes
}  // namespace fvb

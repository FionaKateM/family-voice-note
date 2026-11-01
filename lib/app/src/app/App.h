#pragma once

#include <cstdint>

#include "hal/Hardware.h"

namespace fvb {

// The box itself. Only talks to hardware through the interfaces in Hardware, so
// the same code runs on the Core2, on a laptop and in tests.
//
// At this stage (ticket 1.1) it only starts up and reads input. Later tickets add
// storage, Telegram, conversations and screens.
class App {
public:
    explicit App(Hardware& hardware) : hw_(hardware) {}

    // Call once at startup.
    void begin();
    // Call repeatedly. Does a small amount of work and returns quickly.
    void tick();

    uint64_t tickCount() const { return ticks_; }

private:
    void handleInput(const InputEvent& event);

    Hardware& hw_;
    uint64_t ticks_ = 0;
};

}  // namespace fvb

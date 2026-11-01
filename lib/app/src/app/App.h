#pragma once

#include <cstdint>

#include "app/storage/Storage.h"
#include "hal/Hardware.h"

namespace fvb {

// The box itself. Only talks to hardware through the interfaces in Hardware, so
// the same code runs on the Core2, on a laptop and in tests.
//
// At this stage it starts up, reads setup.txt and reads input. Later tickets add
// Telegram, conversations and screens.
class App {
public:
    explicit App(Hardware& hardware) : hw_(hardware), storage_(hardware.files, hardware.settings) {}

    // Call once at startup.
    void begin();
    // Call repeatedly. Does a small amount of work and returns quickly.
    void tick();

    uint64_t tickCount() const { return ticks_; }
    Storage& storage() { return storage_; }

private:
    void handleInput(const InputEvent& event);
    void readSetupFile();

    Hardware& hw_;
    Storage storage_;
    uint64_t ticks_ = 0;
};

}  // namespace fvb

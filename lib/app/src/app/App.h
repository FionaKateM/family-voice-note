#pragma once

#include <cstdint>

#include "app/screens/Screens.h"
#include "app/storage/Storage.h"
#include "hal/Hardware.h"

namespace fvb {

// The box itself. Only talks to hardware through the interfaces in Hardware, so
// the same code runs on the Core2, on a laptop and in tests.
//
// It starts up, reads setup.txt, and runs the screens the child uses. Telegram
// runs alongside it (see src/desktop_main.cpp).
class App {
public:
    explicit App(Hardware& hardware)
        : hw_(hardware), storage_(hardware.files, hardware.settings), screens_(hardware, storage_) {}

    // Call once at startup.
    void begin();
    // Call repeatedly. Does a small amount of work and returns quickly.
    void tick();

    uint64_t tickCount() const { return ticks_; }
    Storage& storage() { return storage_; }
    Screens& screens() { return screens_; }

private:
    void handleInput(const InputEvent& event);
    void readSetupFile();

    Hardware& hw_;
    Storage storage_;
    Screens screens_;
    uint64_t ticks_ = 0;
};

}  // namespace fvb

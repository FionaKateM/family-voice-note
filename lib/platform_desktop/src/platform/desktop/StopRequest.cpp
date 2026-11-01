#include "platform/desktop/StopRequest.h"

#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdlib>

namespace fvb {

namespace {

// Set from a signal handler, so it must be lock-free.
std::atomic<bool> stopFlag{false};
static_assert(std::atomic<bool>::is_always_lock_free, "stop flag must be safe to set from a signal handler");

void onSignal(int) {
    if (stopFlag.exchange(true)) {
        // Second Ctrl+C. Only async-signal-safe calls from here.
        static const char quitting[] = "\nQuitting now.\n";
        (void)!::write(STDERR_FILENO, quitting, sizeof(quitting) - 1);
        std::_Exit(130);
    }
    static const char stopping[] = "\nStopping... (press Ctrl+C again to quit straight away)\n";
    (void)!::write(STDERR_FILENO, stopping, sizeof(stopping) - 1);
}

}  // namespace

void installStopHandler() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
}

bool stopRequested() { return stopFlag.load(); }

void requestStop() { stopFlag.store(true); }

}  // namespace fvb

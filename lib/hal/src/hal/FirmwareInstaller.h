#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fvb {

// Installing new firmware (section 13 of the requirements).
//
// On the device this writes into the spare half of the ESP32's flash, then
// switches over to it on the next restart. If the new firmware doesn't call
// markGood() within a few minutes of starting, the device goes back to the old
// one by itself (ticket 2.7). On the desktop build it's simulated: see
// DesktopFirmwareInstaller.
//
// The app checks the download's size and checksum before calling finish(), so
// a damaged file is never switched to.
//
// Not part of Hardware because only the update code uses it.
class FirmwareInstaller {
public:
    virtual ~FirmwareInstaller() = default;

    // Gets ready to receive `size` bytes of firmware for `version`. False, with
    // a reason in `error`, if it can't (for example, it doesn't fit).
    virtual bool begin(size_t size, const std::string& version, std::string& error) = 0;
    // The next piece of the download. False if it couldn't be written.
    virtual bool write(const uint8_t* data, size_t length) = 0;
    // Makes the new firmware the one to start next time. Only call once all of
    // it has arrived and been checked.
    virtual bool finish(std::string& error) = 0;
    // Throws away a half-received download. Safe to call at any time.
    virtual void abort() = 0;
    // Restarts the box into whatever firmware is set to start next.
    virtual void restart() = 0;
    // Says the firmware that's running started properly and reached Telegram,
    // so it should be kept. Until this is called after an update, the device
    // will go back to the previous version if it restarts or a few minutes pass.
    virtual void markGood() = 0;
};

}  // namespace fvb

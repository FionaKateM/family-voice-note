#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "hal/FirmwareInstaller.h"
#include "hal/Logger.h"

namespace fvb {

// Pretend firmware installs for the laptop build.
//
// The download really happens and is really checked, then saved as
// <folder>/firmware.bin, with the version it claims to be in
// <folder>/version.txt. "Restarting" stops the program. The next time it
// starts, desktop_main reads version.txt and the box acts as if it's running
// that version, so you can see the "Updated to ..." message.
//
// To see what happens when an update doesn't start properly, start the program
// with --undo-update: it throws the pretend version away first, as the device
// does when it goes back to the old firmware.
class DesktopFirmwareInstaller : public FirmwareInstaller {
public:
    DesktopFirmwareInstaller(std::filesystem::path folder, Logger& log) : folder_(std::move(folder)), log_(log) {}

    bool begin(size_t size, const std::string& version, std::string& error) override;
    bool write(const uint8_t* data, size_t length) override;
    bool finish(std::string& error) override;
    void abort() override;
    void restart() override;
    void markGood() override {}

    // The version of the last pretend install, or "" if there hasn't been one.
    std::string installedVersion() const;
    // Forgets the last pretend install.
    void forget();

private:
    std::filesystem::path partPath() const { return folder_ / "download.part"; }

    std::filesystem::path folder_;
    Logger& log_;
    std::ofstream out_;
    std::string version_;
};

}  // namespace fvb

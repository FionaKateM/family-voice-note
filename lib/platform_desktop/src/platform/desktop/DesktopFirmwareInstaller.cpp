#include "platform/desktop/DesktopFirmwareInstaller.h"

#include <system_error>

#include "platform/desktop/StopRequest.h"

namespace fvb {

bool DesktopFirmwareInstaller::begin(size_t, const std::string& version, std::string& error) {
    abort();
    std::error_code ec;
    std::filesystem::create_directories(folder_, ec);
    out_.open(partPath(), std::ios::binary | std::ios::trunc);
    if (!out_) {
        error = "couldn't write to " + folder_.string();
        return false;
    }
    version_ = version;
    return true;
}

bool DesktopFirmwareInstaller::write(const uint8_t* data, size_t length) {
    if (!out_) return false;
    out_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
    return static_cast<bool>(out_);
}

bool DesktopFirmwareInstaller::finish(std::string& error) {
    if (!out_.is_open()) {
        error = "nothing was downloaded";
        return false;
    }
    out_.close();
    std::error_code ec;
    std::filesystem::rename(partPath(), folder_ / "firmware.bin", ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    std::ofstream version(folder_ / "version.txt", std::ios::trunc);
    version << version_ << "\n";
    if (!version) {
        error = "couldn't save the version";
        return false;
    }
    return true;
}

void DesktopFirmwareInstaller::abort() {
    if (out_.is_open()) out_.close();
    std::error_code ec;
    std::filesystem::remove(partPath(), ec);
}

void DesktopFirmwareInstaller::restart() {
    log_.info("Pretend update to " + version_ +
              " installed. The laptop box can't restart itself, so it's stopping. Start it again to carry on.");
    requestStop();
}

std::string DesktopFirmwareInstaller::installedVersion() const {
    std::ifstream in(folder_ / "version.txt");
    std::string version;
    std::getline(in, version);
    while (!version.empty() && (version.back() == '\r' || version.back() == ' ')) version.pop_back();
    return version;
}

void DesktopFirmwareInstaller::forget() {
    std::error_code ec;
    std::filesystem::remove(folder_ / "version.txt", ec);
    std::filesystem::remove(folder_ / "firmware.bin", ec);
}

}  // namespace fvb

#pragma once

#include <functional>
#include <string>
#include <utility>

#include "hal/Network.h"

namespace fvb {

// Real HTTPS for the laptop build, using libcurl (built into macOS; on Linux
// install libcurl4-openssl-dev).
//
// Keeps one connection open and reuses it, the same way the device should, so
// long polling Telegram doesn't set up a new encrypted connection every time.
class DesktopNetwork : public Network {
public:
    DesktopNetwork();
    ~DesktopNetwork() override;
    DesktopNetwork(const DesktopNetwork&) = delete;
    DesktopNetwork& operator=(const DesktopNetwork&) = delete;

    // A laptop is assumed to be online. Requests report their own failures.
    bool isConnected() const override { return true; }
    int8_t signalStrength() const override { return 0; }
    HttpResponse request(const HttpRequest& request) override;
    // A laptop isn't really "on" one Wi-Fi network as far as the box is
    // concerned, so this is a made-up name (the --wifi option). Changing it
    // makes the box check which country it's in, as if it had moved.
    std::string wifiName() const override { return wifiName_; }
    void setWifiName(std::string name) { wifiName_ = std::move(name); }

    // Checked about once a second while a request is in progress (more often
    // while data is moving). If it returns true the request is abandoned and
    // reported as a network error. Used so Ctrl+C doesn't have to wait up to
    // 35 seconds for Telegram's long poll to finish. Must be safe to call from
    // whichever thread makes the request.
    void setCancelCheck(std::function<bool()> check) { cancelCheck_ = std::move(check); }

private:
    void* curl_ = nullptr;  // CURL*, kept out of the header
    std::function<bool()> cancelCheck_;
    std::string wifiName_ = "Laptop";
};

}  // namespace fvb

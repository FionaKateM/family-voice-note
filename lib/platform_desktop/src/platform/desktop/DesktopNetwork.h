#pragma once

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

private:
    void* curl_ = nullptr;  // CURL*, kept out of the header
};

}  // namespace fvb

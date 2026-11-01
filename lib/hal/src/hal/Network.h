#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace fvb {

struct HttpRequest {
    std::string method = "GET";
    std::string url;  // always HTTPS
    std::map<std::string, std::string> headers;
    std::string body;
    uint32_t timeoutMs = 30000;

    // Optional. When set, the response body is handed over in pieces as it
    // arrives instead of being collected in HttpResponse::body. Used to download
    // files straight to the SD card without holding the whole file in memory.
    // Return false to stop the download early.
    // Pieces are passed whatever the status code, so check the status before
    // trusting what was received.
    std::function<bool(const char* data, size_t length)> onBodyChunk;
};

struct HttpResponse {
    int status = 0;  // 0 means the request didn't reach the server
    std::string body;  // empty when the request used onBodyChunk
    std::string error;  // filled in when status is 0

    bool ok() const { return status >= 200 && status < 300; }
};

// Internet access, as plain HTTPS requests. Telegram is built on top of this in
// lib/telegram.
//
// Implementations should keep the connection to a server open between requests
// where they can. The box asks Telegram for new messages every 25 seconds or so,
// and setting up a fresh encrypted connection each time is slow on the ESP32.
class Network {
public:
    virtual ~Network() = default;

    virtual bool isConnected() const = 0;
    // Wi-Fi signal strength in dBm (e.g. -60). 0 if not connected or not Wi-Fi.
    virtual int8_t signalStrength() const = 0;
    // The name of the Wi-Fi network it's on, or "" if it isn't on Wi-Fi or
    // can't tell. Used to notice the box has moved somewhere new, so it can
    // check which country it's in (section 8 of the requirements).
    virtual std::string wifiName() const { return ""; }
    virtual HttpResponse request(const HttpRequest& request) = 0;
};

}  // namespace fvb

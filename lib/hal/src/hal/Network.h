#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace fvb {

struct HttpRequest {
    std::string method = "GET";
    std::string url;  // always HTTPS
    std::map<std::string, std::string> headers;
    std::string body;
    uint32_t timeoutMs = 30000;
};

struct HttpResponse {
    int status = 0;  // 0 means the request didn't reach the server
    std::string body;
    std::string error;  // filled in when status is 0

    bool ok() const { return status >= 200 && status < 300; }
};

// Internet access. Kept to plain HTTPS requests for now. Ticket 1.3 will add
// whatever Telegram needs on top (file uploads, downloads straight to the SD card,
// keeping the connection open for long polling).
class Network {
public:
    virtual ~Network() = default;

    virtual bool isConnected() const = 0;
    // Wi-Fi signal strength in dBm (e.g. -60). 0 if not connected or not Wi-Fi.
    virtual int8_t signalStrength() const = 0;
    virtual HttpResponse request(const HttpRequest& request) = 0;
};

}  // namespace fvb

// Wi-Fi and HTTPS on the Core2.

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>

#include "platform/core2/Core2Hardware.h"
#include "platform/core2/RootCertificates.h"

namespace fvb {

namespace {

constexpr int kMaxRedirects = 5;
constexpr int32_t kConnectTimeoutMs = 15000;

bool isRedirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// "https://host/path" -> "https://host". Used when a redirect gives only a path.
std::string originOf(const std::string& url) {
    const size_t hostStart = url.find("://");
    if (hostStart == std::string::npos) return "";
    const size_t pathStart = url.find('/', hostStart + 3);
    return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

// Hands a response body to HttpRequest::onBodyChunk as it arrives. HTTPClient
// does the unpicking of chunked replies, then writes the plain body here.
class ChunkStream : public Stream {
public:
    explicit ChunkStream(const std::function<bool(const char*, size_t)>& onChunk) : onChunk_(onChunk) {}

    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* data, size_t length) override {
        if (stopped || length == 0) return 0;
        if (!onChunk_(reinterpret_cast<const char*>(data), length)) {
            stopped = true;
            return 0;
        }
        return length;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    bool stopped = false;

private:
    const std::function<bool(const char*, size_t)>& onChunk_;
};

}  // namespace

bool core2JoinWifi(const std::string& name, const std::string& password, uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect();
    WiFi.begin(name.c_str(), password.empty() ? nullptr : password.c_str());
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

struct Core2Network::Connection {
    WiFiClientSecure tls;
    HTTPClient http;
};

Core2Network::Core2Network() : connection_(new Connection) {
    connection_->tls.setCACert(kRootCertificates);
    connection_->http.setReuse(true);  // keep the connection open between requests
}

Core2Network::~Core2Network() { delete connection_; }

bool Core2Network::isConnected() const { return WiFi.status() == WL_CONNECTED; }

int8_t Core2Network::signalStrength() const { return isConnected() ? static_cast<int8_t>(WiFi.RSSI()) : 0; }

std::string Core2Network::wifiName() const { return isConnected() ? std::string(WiFi.SSID().c_str()) : ""; }

HttpResponse Core2Network::request(const HttpRequest& request) {
    HttpResponse response;
    if (!isConnected()) {
        response.error = "not connected to Wi-Fi";
        return response;
    }

    WiFiClientSecure& tls = connection_->tls;
    HTTPClient& http = connection_->http;
    // HTTPClient's read timeout is at most about 65 seconds.
    const uint16_t timeoutMs = static_cast<uint16_t>(std::min<uint32_t>(request.timeoutMs, 65000));
    std::string url = request.url;

    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        if (url.rfind("https://", 0) != 0) {
            response.error = "only HTTPS addresses are allowed";
            return response;
        }

        http.setConnectTimeout(std::min<int32_t>(kConnectTimeoutMs, static_cast<int32_t>(request.timeoutMs)));
        http.setTimeout(timeoutMs);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // done here, so only HTTPS is followed
        // begin() closes the open connection itself if this is a different server.
        if (!http.begin(tls, String(url.c_str()))) {
            response.error = "couldn't read the address";
            return response;
        }
        for (const auto& header : request.headers) {
            http.addHeader(header.first.c_str(), header.second.c_str());
        }
        const char* wanted[] = {"Location"};
        http.collectHeaders(wanted, 1);

        const int status = http.sendRequest(request.method.c_str(),
                                            reinterpret_cast<uint8_t*>(const_cast<char*>(request.body.data())),
                                            request.body.size());
        if (status <= 0) {
            // These messages name the problem, never the address, so they
            // can't contain the bot token.
            response.error = HTTPClient::errorToString(status).c_str();
            http.end();
            tls.stop();
            return response;
        }

        if (request.followRedirects && isRedirect(status)) {
            std::string location = http.header("Location").c_str();
            http.end();
            if (location.empty()) {
                response.error = "redirect without an address";
                return response;
            }
            if (location[0] == '/') location = originOf(url) + location;
            url = location;
            continue;
        }

        response.status = status;
        if (request.onBodyChunk) {
            ChunkStream stream(request.onBodyChunk);
            const int got = http.writeToStream(&stream);
            if (got < 0 && !stream.stopped) {
                // Lost part way through.
                response.status = 0;
                response.error = HTTPClient::errorToString(got).c_str();
            }
            // A download stopped early leaves the rest unread, so the
            // connection can't be used again.
            if (got < 0) tls.stop();
        } else {
            const String body = http.getString();
            response.body.assign(body.c_str(), body.length());
        }
        http.end();  // keeps the connection open if the server allows it
        return response;
    }

    response.error = "too many redirects";
    return response;
}

}  // namespace fvb

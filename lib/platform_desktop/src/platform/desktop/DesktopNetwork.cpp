#include "platform/desktop/DesktopNetwork.h"

#include <curl/curl.h>

#include <mutex>

namespace fvb {

namespace {

struct Transfer {
    const HttpRequest* request;
    HttpResponse* response;
};

size_t onData(char* data, size_t size, size_t count, void* userData) {
    auto* transfer = static_cast<Transfer*>(userData);
    const size_t length = size * count;
    if (transfer->request->onBodyChunk) {
        // Returning anything other than `length` tells curl to stop.
        return transfer->request->onBodyChunk(data, length) ? length : 0;
    }
    transfer->response->body.append(data, length);
    return length;
}

}  // namespace

DesktopNetwork::DesktopNetwork() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    curl_ = curl_easy_init();
}

DesktopNetwork::~DesktopNetwork() {
    if (curl_) curl_easy_cleanup(static_cast<CURL*>(curl_));
}

HttpResponse DesktopNetwork::request(const HttpRequest& request) {
    HttpResponse response;
    CURL* curl = static_cast<CURL*>(curl_);
    if (!curl) {
        response.error = "couldn't start libcurl";
        return response;
    }

    // Clears the previous request's options but keeps the open connection.
    curl_easy_reset(curl);

    char errorText[CURL_ERROR_SIZE] = {0};
    Transfer transfer{&request, &response};

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorText);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeoutMs));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    } else if (request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        }
    }

    struct curl_slist* headers = nullptr;
    for (const auto& header : request.headers) {
        headers = curl_slist_append(headers, (header.first + ": " + header.second).c_str());
    }
    // Stops curl adding "Expect: 100-continue" to uploads, which costs a round trip.
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);

    if (result != CURLE_OK && !(result == CURLE_WRITE_ERROR && status != 0)) {
        // Didn't get a full answer. curl's messages name the host but not the
        // full address, so they never contain the bot token.
        response.status = 0;
        response.error = errorText[0] ? errorText : curl_easy_strerror(result);
        return response;
    }
    // A write error with a status means the caller stopped the download on
    // purpose; report the status so it can tell what happened.
    response.status = static_cast<int>(status);
    return response;
}

}  // namespace fvb

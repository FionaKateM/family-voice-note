#include "platform/desktop/DesktopNetwork.h"

#include <curl/curl.h>

#include <fstream>
#include <mutex>
#include <vector>

namespace fvb {

namespace {

struct Transfer {
    const HttpRequest* request;
    HttpResponse* response;
    const std::function<bool()>* cancelCheck;
    bool cancelled = false;
};

// curl calls this regularly during a request, even while it's only waiting.
// Returning non-zero abandons the request.
int onProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* transfer = static_cast<Transfer*>(userData);
    if (*transfer->cancelCheck && (*transfer->cancelCheck)()) {
        transfer->cancelled = true;
        return 1;
    }
    return 0;
}

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

// file:// addresses, for trying updates from a folder on the laptop
// (--release-folder). Answers like a web server would: 200 with the file, or 404.
HttpResponse readLocalFile(const HttpRequest& request) {
    HttpResponse response;
    const std::string path = request.url.substr(7);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        response.status = 404;
        return response;
    }
    response.status = 200;
    std::vector<char> piece(16 * 1024);
    while (in) {
        in.read(piece.data(), static_cast<std::streamsize>(piece.size()));
        const size_t got = static_cast<size_t>(in.gcount());
        if (got == 0) break;
        if (request.onBodyChunk) {
            if (!request.onBodyChunk(piece.data(), got)) break;
        } else {
            response.body.append(piece.data(), got);
        }
    }
    return response;
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
    if (request.url.rfind("file://", 0) == 0) return readLocalFile(request);

    HttpResponse response;
    CURL* curl = static_cast<CURL*>(curl_);
    if (!curl) {
        response.error = "couldn't start libcurl";
        return response;
    }

    // Clears the previous request's options but keeps the open connection.
    curl_easy_reset(curl);

    char errorText[CURL_ERROR_SIZE] = {0};
    Transfer transfer{&request, &response, &cancelCheck_};

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorText);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeoutMs));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    if (request.followRedirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
    }
    if (cancelCheck_) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    }

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

    if (transfer.cancelled) {
        response.status = 0;
        response.error = "stopped because the program is closing";
        return response;
    }
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

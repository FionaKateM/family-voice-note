#include "telegram/Multipart.h"

#include <algorithm>
#include <cctype>

namespace fvb {
namespace telegram {

void MultipartBody::addField(const std::string& name, const std::string& value) {
    parts_.push_back(Part{name, "", "", value});
}

void MultipartBody::addFile(const std::string& name, const std::string& fileName, const std::string& contentType,
                            const std::string& data) {
    parts_.push_back(Part{name, fileName, contentType, data});
}

std::string MultipartBody::build() {
    // Try boundaries until one doesn't clash with any of the contents.
    for (int attempt = 0;; ++attempt) {
        boundary_ = "fvb-boundary-" + std::to_string(attempt);
        bool clash = std::any_of(parts_.begin(), parts_.end(), [&](const Part& p) {
            return p.data.find(boundary_) != std::string::npos;
        });
        if (!clash) break;
    }

    std::string body;
    size_t size = 0;
    for (const auto& p : parts_) size += p.data.size() + 200;
    body.reserve(size);

    for (const auto& p : parts_) {
        body += "--" + boundary_ + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + p.name + "\"";
        if (!p.fileName.empty()) body += "; filename=\"" + p.fileName + "\"";
        body += "\r\n";
        if (!p.contentType.empty()) body += "Content-Type: " + p.contentType + "\r\n";
        body += "\r\n";
        body += p.data;
        body += "\r\n";
    }
    body += "--" + boundary_ + "--\r\n";
    return body;
}

std::string audioContentType(const std::string& fileName) {
    size_t dot = fileName.rfind('.');
    std::string ext = dot == std::string::npos ? "" : fileName.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "ogg" || ext == "oga" || ext == "opus") return "audio/ogg";
    return "application/octet-stream";
}

}  // namespace telegram
}  // namespace fvb

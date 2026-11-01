#pragma once

#include <string>
#include <vector>

namespace fvb {
namespace telegram {

// Builds a multipart/form-data request body, the format Telegram needs for
// uploading files.
class MultipartBody {
public:
    void addField(const std::string& name, const std::string& value);
    void addFile(const std::string& name, const std::string& fileName, const std::string& contentType,
                 const std::string& data);

    // Builds the body. Chooses a boundary (the marker between parts) that
    // doesn't appear anywhere in the contents.
    std::string build();
    // The Content-Type header to send with the body. Call after build().
    std::string contentType() const { return "multipart/form-data; boundary=" + boundary_; }

private:
    struct Part {
        std::string name;
        std::string fileName;  // empty for a plain field
        std::string contentType;
        std::string data;
    };
    std::vector<Part> parts_;
    std::string boundary_;
};

// "audio/mpeg" for .mp3, "audio/ogg" for .ogg/.oga/.opus, otherwise a generic type.
std::string audioContentType(const std::string& fileName);

}  // namespace telegram
}  // namespace fvb

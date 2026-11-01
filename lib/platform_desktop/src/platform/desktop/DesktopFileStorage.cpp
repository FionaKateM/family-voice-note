#include "platform/desktop/DesktopFileStorage.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace fvb {

DesktopFileStorage::DesktopFileStorage(fs::path root, uint64_t capacityBytes)
    : root_(std::move(root)), capacity_(capacityBytes) {
    std::error_code ec;
    fs::create_directories(root_, ec);
}

bool DesktopFileStorage::resolve(const std::string& path, fs::path& out) const {
    if (path.empty() || path[0] != '/') return false;
    const fs::path relative = fs::path(path).relative_path();
    for (const auto& part : relative) {
        if (part == "..") return false;
    }
    out = root_ / relative;
    return true;
}

bool DesktopFileStorage::isMounted() const {
    std::error_code ec;
    return fs::is_directory(root_, ec);
}

bool DesktopFileStorage::exists(const std::string& path) const {
    fs::path p;
    std::error_code ec;
    return resolve(path, p) && fs::exists(p, ec);
}

bool DesktopFileStorage::readFile(const std::string& path, std::string& contents) const {
    fs::path p;
    if (!resolve(path, p)) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool DesktopFileStorage::write(const std::string& path, const std::string& contents, bool append) {
    fs::path p;
    if (!resolve(path, p)) return false;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) return false;
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(out);
}

bool DesktopFileStorage::writeFile(const std::string& path, const std::string& contents) {
    return write(path, contents, false);
}

bool DesktopFileStorage::appendFile(const std::string& path, const std::string& contents) {
    return write(path, contents, true);
}

bool DesktopFileStorage::removeFile(const std::string& path) {
    fs::path p;
    std::error_code ec;
    return resolve(path, p) && fs::is_regular_file(p, ec) && fs::remove(p, ec);
}

bool DesktopFileStorage::renameFile(const std::string& from, const std::string& to) {
    fs::path a, b;
    if (!resolve(from, a) || !resolve(to, b)) return false;
    std::error_code ec;
    if (!fs::is_regular_file(a, ec)) return false;
    fs::create_directories(b.parent_path(), ec);
    fs::rename(a, b, ec);
    return !ec;
}

int64_t DesktopFileStorage::fileSize(const std::string& path) const {
    fs::path p;
    std::error_code ec;
    if (!resolve(path, p) || !fs::is_regular_file(p, ec)) return -1;
    const auto size = fs::file_size(p, ec);
    return ec ? -1 : static_cast<int64_t>(size);
}

bool DesktopFileStorage::makeDir(const std::string& path) {
    fs::path p;
    if (!resolve(path, p)) return false;
    std::error_code ec;
    fs::create_directories(p, ec);
    return fs::is_directory(p, ec);
}

bool DesktopFileStorage::removeDir(const std::string& path) {
    fs::path p;
    if (!resolve(path, p)) return false;
    if (fs::path(path).relative_path().empty()) return false;  // never remove the whole card
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return false;
    fs::remove_all(p, ec);
    return !ec;
}

std::vector<std::string> DesktopFileStorage::listDir(const std::string& path) const {
    std::vector<std::string> names;
    fs::path p;
    std::error_code ec;
    if (!resolve(path, p) || !fs::is_directory(p, ec)) return names;
    for (const auto& entry : fs::directory_iterator(p, ec)) {
        names.push_back(entry.path().filename().string());
    }
    return names;
}

uint64_t DesktopFileStorage::totalBytes() const { return capacity_; }

uint64_t DesktopFileStorage::usedBytes() const {
    uint64_t used = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root_, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec)) used += it->file_size(ec);
    }
    return used;
}

}  // namespace fvb

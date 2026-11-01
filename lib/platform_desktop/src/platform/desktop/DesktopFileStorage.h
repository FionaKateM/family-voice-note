#pragma once

#include <filesystem>
#include <string>

#include "hal/FileStorage.h"

namespace fvb {

// Uses a folder on the laptop as the SD card.
class DesktopFileStorage : public FileStorage {
public:
    // The folder is created if it doesn't exist.
    explicit DesktopFileStorage(std::filesystem::path root,
                                uint64_t capacityBytes = 16ULL * 1024 * 1024 * 1024);

    bool isMounted() const override;
    bool exists(const std::string& path) const override;
    bool readFile(const std::string& path, std::string& contents) const override;
    bool writeFile(const std::string& path, const std::string& contents) override;
    bool appendFile(const std::string& path, const std::string& contents) override;
    bool removeFile(const std::string& path) override;
    bool renameFile(const std::string& from, const std::string& to) override;
    int64_t fileSize(const std::string& path) const override;
    bool makeDir(const std::string& path) override;
    bool removeDir(const std::string& path) override;
    std::vector<std::string> listDir(const std::string& path) const override;
    uint64_t totalBytes() const override;
    uint64_t usedBytes() const override;

    // Turns a card path like "/contacts/1/index.json" into a laptop path.
    // Returns false for paths that don't start with "/" or that contain "..".
    bool resolve(const std::string& path, std::filesystem::path& out) const;

private:
    bool write(const std::string& path, const std::string& contents, bool append);

    std::filesystem::path root_;
    uint64_t capacity_;
};

}  // namespace fvb

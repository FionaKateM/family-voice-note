#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fvb {

// The SD card: messages, photos and index files.
// Paths always start with "/" and are relative to the root of the card.
// Paths containing ".." are refused.
class FileStorage {
public:
    virtual ~FileStorage() = default;

    virtual bool isMounted() const = 0;

    virtual bool exists(const std::string& path) const = 0;
    virtual bool readFile(const std::string& path, std::string& contents) const = 0;
    // Creates any missing folders on the way.
    virtual bool writeFile(const std::string& path, const std::string& contents) = 0;
    virtual bool appendFile(const std::string& path, const std::string& contents) = 0;
    virtual bool removeFile(const std::string& path) = 0;
    // Overwrites the destination if it exists.
    virtual bool renameFile(const std::string& from, const std::string& to) = 0;
    virtual int64_t fileSize(const std::string& path) const = 0;  // -1 if missing

    virtual bool makeDir(const std::string& path) = 0;  // creates parents too
    virtual bool removeDir(const std::string& path) = 0;  // removes contents too
    // Names (not full paths) of the files and folders directly inside a folder.
    virtual std::vector<std::string> listDir(const std::string& path) const = 0;

    virtual uint64_t totalBytes() const = 0;
    virtual uint64_t usedBytes() const = 0;
};

}  // namespace fvb

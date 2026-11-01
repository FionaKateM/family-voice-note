#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/FileStorage.h"

namespace fvb {

struct StoredMessage {
    enum class Direction : uint8_t { Received, Sent };
    enum class Status : uint8_t {
        Unread,   // received, child hasn't played it to the end
        Heard,    // received, child has played it to the end
        Waiting,  // sent by the child, still in the outbox
        Delivered // sent by the child, Telegram has it
    };

    uint32_t id = 0;  // per contact, starts at 1, never reused
    Direction direction = Direction::Received;
    Status status = Status::Unread;
    int64_t time = 0;  // seconds since 1970, UTC
    uint32_t durationMs = 0;
    std::string fileName;  // inside the contact's folder, e.g. "0007.ogg"

    // From Telegram. Used for the 👀 reaction and for monitor copies.
    int64_t telegramMessageId = 0;
    std::string telegramFileId;
};

// Each contact's voice notes, in their own folder on the SD card, with a small
// index file so history loads without scanning the card.
//
// The index is rewritten in full on each change: written to index.tmp first,
// then renamed over index.txt. If the power goes during the rename, index.tmp is
// picked up next time. A damaged line is skipped rather than losing the whole list.
class MessageStore {
public:
    explicit MessageStore(FileStorage& files) : files_(files) {}

    // Oldest first.
    std::vector<StoredMessage> messages(uint32_t contact) const;
    bool find(uint32_t contact, uint32_t id, StoredMessage& out) const;
    size_t unreadCount(uint32_t contact) const;
    // Full SD card path of a message's audio.
    std::string audioPath(uint32_t contact, const StoredMessage& message) const;

    // Moves the audio file at sourcePath into the contact's folder and adds it to
    // their index. The id and file name are filled in here. Returns the new id, or
    // 0 if it couldn't be stored (the source file is left where it was).
    uint32_t add(uint32_t contact, StoredMessage message, const std::string& sourcePath);

    // Received message played to the end.
    bool markHeard(uint32_t contact, uint32_t id);
    // Sent message accepted by Telegram.
    bool markDelivered(uint32_t contact, uint32_t id, int64_t telegramMessageId, const std::string& telegramFileId);
    // Deletes the audio and the index entry.
    bool remove(uint32_t contact, uint32_t id);
    // Deletes the contact's whole folder: messages, index and photo.
    bool removeAll(uint32_t contact);

private:
    struct Index {
        uint32_t nextId = 1;
        std::vector<StoredMessage> messages;
    };

    Index load(uint32_t contact) const;
    bool save(uint32_t contact, const Index& index);

    FileStorage& files_;
};

}  // namespace fvb

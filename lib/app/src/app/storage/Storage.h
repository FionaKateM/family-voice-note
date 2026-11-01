#pragma once

#include <cstdint>
#include <string>

#include "app/storage/Bin.h"
#include "app/storage/DeviceSettings.h"
#include "app/storage/MessageStore.h"
#include "app/storage/Outbox.h"
#include "app/storage/People.h"
#include "app/storage/SetupFile.h"
#include "hal/FileStorage.h"
#include "hal/SettingsStore.h"

namespace fvb {

// Everything the box keeps, in one place. The pieces can be used directly; the
// methods here are the jobs that need more than one of them at once.
class Storage {
public:
    Storage(FileStorage& files, SettingsStore& store)
        : settings(store), people(store), messages(files), outbox(files), bin(files), files_(files) {}

    DeviceSettings settings;
    People people;
    MessageStore messages;
    Outbox outbox;
    Bin bin;

    // Reads, saves and deletes /setup.txt if it's on the card.
    SetupResult applySetupFile() { return fvb::applySetupFile(files_, settings, people); }

    // The child pressed Send. Moves the recording into the contact's folder, so it
    // shows in their history straight away, and adds it to the outbox.
    // Returns the outbox sequence number, or 0 on failure (recording left in place).
    uint32_t queueRecording(uint32_t contact, const std::string& recordingPath, uint32_t durationMs, int64_t now);

    // Telegram accepted an outbox entry. Updates the message and removes the entry.
    bool markSent(const OutboxEntry& entry, int64_t telegramMessageId, const std::string& telegramFileId);

    // The child pressed delete or re-record. Moves the recording to the bin.
    // Returns the bin path, or "" on failure.
    std::string binRecording(uint32_t contact, const std::string& recordingPath, int64_t now);

    // Removes a contact, their messages, photo and anything waiting to send to them.
    bool removeContact(uint32_t contact);

private:
    FileStorage& files_;
};

}  // namespace fvb

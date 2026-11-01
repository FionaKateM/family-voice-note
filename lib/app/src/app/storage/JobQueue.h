#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/FileStorage.h"

namespace fvb {

// One piece of Telegram work, other than sending the child's recordings (those
// are in the Outbox). Kept on the card so it survives a restart or power cut.
struct Job {
    enum class Kind : uint8_t {
        Download,  // a contact's voice note to fetch and store as unread
        Reaction,  // the 👀 on a voice note the child has listened to
        Copy,      // a copy of a voice note for a monitor
        Notice,    // a text message, e.g. telling admins something couldn't be sent
    };

    uint32_t sequence = 0;
    Kind kind = Kind::Download;
    uint32_t contact = 0;           // Download, Reaction: whose message it is
    int64_t chatId = 0;             // Copy, Notice: who it goes to
    int64_t telegramMessageId = 0;  // Download: the voice note. Reaction: the message to react to
    std::string fileId;             // Download, Copy: Telegram's stored copy of the audio
    int64_t time = 0;               // Download: when it was sent (seconds since 1970, UTC)
    uint32_t durationMs = 0;        // Download
    std::string text;               // Copy: the caption. Notice: the message
    uint32_t attempts = 0;          // failed attempts so far
    uint64_t nextAttemptAtMs = 0;   // Clock::millisSinceBoot(); 0 means straight away
};

// Telegram work waiting to be done, oldest first. One small file per job in
// /jobs, written then renamed, the same way as the outbox.
class JobQueue {
public:
    explicit JobQueue(FileStorage& files) : files_(files) {}

    // Oldest first.
    std::vector<Job> jobs() const;
    bool empty() const { return jobs().empty(); }
    size_t size() const { return jobs().size(); }

    // Fills in the sequence number. Returns it, or 0 if it couldn't be saved.
    uint32_t add(Job job);
    // Records a failed attempt and when to try next.
    bool recordFailure(uint32_t sequence, uint64_t nextAttemptAtMs);
    bool remove(uint32_t sequence);
    // Drops Download and Reaction jobs for one contact (used when they're removed).
    size_t removeForContact(uint32_t contact);
    // See Outbox::resetRetryTimes.
    void resetRetryTimes();

private:
    bool load(const std::string& name, Job& out) const;
    bool save(const Job& job);
    std::string pathFor(uint32_t sequence) const;

    FileStorage& files_;
};

}  // namespace fvb

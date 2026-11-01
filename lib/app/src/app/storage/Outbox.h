#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/FileStorage.h"

namespace fvb {

// One recording waiting to be sent. The audio itself is already in the contact's
// folder (see MessageStore); this just points at it.
struct OutboxEntry {
    uint32_t sequence = 0;   // order the child pressed Send
    uint32_t contact = 0;    // contact number
    uint32_t messageId = 0;  // id in that contact's MessageStore
    int64_t queuedAt = 0;    // seconds since 1970, UTC
    uint32_t attempts = 0;   // failed attempts so far
    uint64_t nextAttemptAtMs = 0;  // Clock::millisSinceBoot(); 0 means straight away
};

// Recordings waiting to be sent, in order. One small file per entry in /outbox,
// so adding or removing one never rewrites the others and a power cut can at
// worst lose the entry being written at that moment.
//
// Retry timing (how long to wait after each failure) belongs to ticket 1.7. This
// only stores the count and the time of the next try.
class Outbox {
public:
    explicit Outbox(FileStorage& files) : files_(files) {}

    // Oldest first.
    std::vector<OutboxEntry> entries() const;
    bool empty() const { return entries().empty(); }
    size_t size() const { return entries().size(); }

    // Returns the new entry's sequence number, or 0 if it couldn't be saved.
    uint32_t add(uint32_t contact, uint32_t messageId, int64_t queuedAt);
    // Records a failed attempt and when to try next.
    bool recordFailure(uint32_t sequence, uint64_t nextAttemptAtMs);
    // Call once Telegram has the message.
    bool remove(uint32_t sequence);
    // Drops everything for one contact (used when they're removed).
    size_t removeForContact(uint32_t contact);

    // After a restart, the millisSinceBoot clock starts again from 0, so stored
    // retry times are meaningless. Call at startup to make everything due now.
    void resetRetryTimes();

private:
    bool load(const std::string& name, OutboxEntry& out) const;
    bool save(const OutboxEntry& entry);
    std::string pathFor(uint32_t sequence) const;

    FileStorage& files_;
};

}  // namespace fvb

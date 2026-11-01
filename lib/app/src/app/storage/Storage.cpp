#include "app/storage/Storage.h"

namespace fvb {

uint32_t Storage::queueRecording(uint32_t contact, const std::string& recordingPath, uint32_t durationMs,
                                 int64_t now) {
    Contact c;
    if (!people.findContact(contact, c)) return 0;

    StoredMessage m;
    m.direction = StoredMessage::Direction::Sent;
    m.status = StoredMessage::Status::Waiting;
    m.time = now;
    m.durationMs = durationMs;
    const uint32_t id = messages.add(contact, m, recordingPath);
    if (id == 0) return 0;

    const uint32_t sequence = outbox.add(contact, id, now);
    if (sequence == 0) {
        // Without an outbox entry it would never be sent, so don't leave it looking queued.
        StoredMessage stored;
        if (messages.find(contact, id, stored)) files_.renameFile(messages.audioPath(contact, stored), recordingPath);
        messages.remove(contact, id);
    }
    return sequence;
}

bool Storage::markSent(const OutboxEntry& entry, int64_t telegramMessageId, const std::string& telegramFileId) {
    // If the message has gone (contact removed meanwhile) there's nothing to update,
    // but the entry still needs clearing.
    messages.markDelivered(entry.contact, entry.messageId, telegramMessageId, telegramFileId);
    return outbox.remove(entry.sequence);
}

bool Storage::markHeard(uint32_t contact, uint32_t messageId) {
    StoredMessage m;
    if (!messages.find(contact, messageId, m) || m.direction != StoredMessage::Direction::Received) return false;
    if (m.status == StoredMessage::Status::Heard) return true;  // already reacted to
    if (!messages.markHeard(contact, messageId)) return false;
    if (m.telegramMessageId == 0) return true;  // nothing to react to

    Job job;
    job.kind = Job::Kind::Reaction;
    job.contact = contact;
    job.telegramMessageId = m.telegramMessageId;
    // If this fails the message is still heard; the 👀 is the only thing lost.
    jobs.add(job);
    return true;
}

std::string Storage::binRecording(uint32_t contact, const std::string& recordingPath, int64_t now) {
    Contact c;
    people.findContact(contact, c);  // name is only for the file name; fine if missing
    return bin.add(recordingPath, contact, c.name, now);
}

bool Storage::removeContact(uint32_t contact) {
    if (!people.removeContact(contact)) return false;
    outbox.removeForContact(contact);
    jobs.removeForContact(contact);
    messages.removeAll(contact);
    return true;
}

}  // namespace fvb

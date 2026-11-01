#include "app/messages/Messaging.h"

#include <algorithm>
#include <set>

#include "app/storage/Paths.h"
#include "app/time/TimeChecks.h"
#include "app/time/TimeZones.h"

namespace fvb {

using telegram::Error;
using telegram::Message;
using telegram::MessageKind;
using telegram::TelegramClient;

namespace {

// Telegram has turned this down for good: the person has blocked or stopped the
// bot, or the request itself can't work. Anything else might work if tried again.
bool isRefusal(const Error& e) { return e.kind == Error::Kind::Forbidden || e.kind == Error::Kind::BadRequest; }

const char* jobName(Job::Kind kind) {
    switch (kind) {
        case Job::Kind::Download: return "download";
        case Job::Kind::Reaction: return "reaction";
        case Job::Kind::Copy: return "monitor copy";
        case Job::Kind::Notice: return "notice";
    }
    return "job";
}

// Rounded to the nearest second, and at least 1 so Telegram shows a length.
int toSeconds(uint32_t ms) { return std::max(1, static_cast<int>((ms + 500) / 1000)); }

}  // namespace

Messaging::Messaging(Bot& bot, TelegramClient& client, Storage& storage, Clock& clock, Logger& log)
    : client_(client), storage_(storage), clock_(clock), log_(log) {
    bot.setMessageHandler([this](Context& ctx, const Message& message) { onMessage(ctx, message); });
}

std::string Messaging::childName() const { return storage_.settings.childName(); }

// ---------------------------------------------------------------------------
// Coming in

void Messaging::onMessage(Context& ctx, const Message& message) {
    Contact contact;
    const bool isContact = storage_.people.findContactByTelegramId(ctx.sender().userId, contact);

    if (!isContact) {
        // Admins and monitors who aren't contacts can't send the child anything.
        if (message.kind == MessageKind::Voice) {
            ctx.reply("Only " + childName() + "'s contacts can send voice notes to the box.");
        } else {
            ctx.reply("Send /help to see what I can do.");
        }
        return;
    }

    if (message.kind == MessageKind::Voice) {
        queueDownload(ctx, message, contact);
        return;
    }

    // Text, photos, stickers, videos, round videos, music files and anything else.
    const uint64_t now = clock_.millisSinceBoot();
    auto it = lastVoiceOnlyReply_.find(ctx.sender().userId);
    if (it != lastVoiceOnlyReply_.end() && now - it->second < static_cast<uint64_t>(kVoiceOnlyReplyGapMs)) return;
    lastVoiceOnlyReply_[ctx.sender().userId] = now;
    ctx.reply(childName() + " can only receive voice notes.");
}

void Messaging::queueDownload(Context& ctx, const Message& message, const Contact& contact) {
    if (message.file.fileSize > TelegramClient::kMaxDownloadBytes) {
        // Telegram won't hand bots anything this big. Only happens with very long voice notes.
        log_.warning("A voice note was too big to download");
        ctx.reply("That voice note is too long for " + childName() + "'s box. Could you send a shorter one?");
        return;
    }

    // The same message can arrive twice if the box restarted while handling it.
    for (const auto& j : storage_.jobs.jobs()) {
        if (j.kind == Job::Kind::Download && j.contact == contact.number &&
            j.telegramMessageId == message.messageId) {
            return;
        }
    }

    Job job;
    job.kind = Job::Kind::Download;
    job.contact = contact.number;
    job.telegramMessageId = message.messageId;
    job.fileId = message.file.fileId;
    job.time = message.date;
    job.durationMs = static_cast<uint32_t>(std::max(0, message.file.durationSeconds)) * 1000;
    if (storage_.jobs.add(job) == 0) {
        log_.error("Couldn't save a voice note to download (is the SD card full or missing?)");
        ctx.reply("Sorry, " + childName() + "'s box couldn't save that. Please try again later.");
        return;
    }
    log_.info("Voice note from contact " + std::to_string(contact.number) + " queued to download");
}

// ---------------------------------------------------------------------------
// Going out: choosing what to do

uint32_t Messaging::prepare(Task& task) {
    task = Task();
    const uint64_t now = clock_.millisSinceBoot();
    uint64_t soonest = now + kIdleCheckMs;

    // The child's recordings, oldest first. A later recording to the same contact
    // waits for the earlier one, so they arrive in order.
    std::set<uint32_t> busyContacts;
    for (const auto& entry : storage_.outbox.entries()) {
        if (busyContacts.count(entry.contact)) continue;
        Contact contact;
        StoredMessage message;
        if (!storage_.people.findContact(entry.contact, contact)) {
            log_.info("Dropped a recording for a contact who has been removed");
            storage_.outbox.remove(entry.sequence);
            continue;
        }
        const std::string audio = storage_.messages.find(entry.contact, entry.messageId, message)
                                      ? storage_.messages.audioPath(entry.contact, message)
                                      : "";
        if (audio.empty() || !storage_.files().exists(audio)) {
            log_.error("A recording waiting to send has gone missing from the SD card");
            storage_.outbox.remove(entry.sequence);
            if (!audio.empty()) storage_.messages.remove(entry.contact, entry.messageId);
            continue;
        }
        busyContacts.insert(entry.contact);
        if (entry.nextAttemptAtMs > now) {
            soonest = std::min(soonest, entry.nextAttemptAtMs);
            continue;
        }
        task.kind = Task::Kind::Send;
        task.entry = entry;
        task.chatId = contact.telegramId;
        task.audioPath = audio;
        task.durationSeconds = toSeconds(message.durationMs);
        return 0;
    }

    // Everything else. Downloads from one contact are stored in the order they arrived.
    busyContacts.clear();
    for (const auto& job : storage_.jobs.jobs()) {
        if (job.kind == Job::Kind::Download && busyContacts.count(job.contact)) continue;

        Contact contact;
        bool stillWanted = true;
        switch (job.kind) {
            case Job::Kind::Download:
            case Job::Kind::Reaction:
                stillWanted = storage_.people.findContact(job.contact, contact);
                task.chatId = contact.telegramId;
                break;
            case Job::Kind::Copy:
                stillWanted = storage_.people.isMonitor(job.chatId);
                task.chatId = job.chatId;
                break;
            case Job::Kind::Notice:
                task.chatId = job.chatId;
                break;
        }
        if (!stillWanted) {
            log_.info(std::string("Dropped a ") + jobName(job.kind) + " for someone who has been removed");
            storage_.jobs.remove(job.sequence);
            continue;
        }

        if (job.kind == Job::Kind::Download) busyContacts.insert(job.contact);
        if (job.nextAttemptAtMs > now) {
            soonest = std::min(soonest, job.nextAttemptAtMs);
            continue;
        }
        task.kind = Task::Kind::Job;
        task.job = job;
        return 0;
    }

    task = Task();
    return static_cast<uint32_t>(soonest - now);
}

// ---------------------------------------------------------------------------
// Going out: talking to Telegram

void Messaging::run(Task& task) {
    task.ok = false;
    switch (task.kind) {
        case Task::Kind::None: return;
        case Task::Kind::Send:
            task.ok = client_.sendVoiceFile(task.chatId, task.audioPath, task.durationSeconds, "", &task.sent);
            break;
        case Task::Kind::Job: {
            const Job& j = task.job;
            switch (j.kind) {
                case Job::Kind::Download: task.ok = client_.downloadFile(j.fileId, paths::kDownload); break;
                case Job::Kind::Reaction:
                    task.ok = client_.setReaction(task.chatId, j.telegramMessageId, TelegramClient::kListenedReaction);
                    break;
                case Job::Kind::Copy: task.ok = client_.sendVoiceById(task.chatId, j.fileId, j.text); break;
                case Job::Kind::Notice: task.ok = client_.sendMessage(task.chatId, j.text); break;
            }
            break;
        }
    }
    task.error = task.ok ? Error() : client_.lastError();
}

// ---------------------------------------------------------------------------
// Going out: recording what happened

void Messaging::finish(Task& task) {
    switch (task.kind) {
        case Task::Kind::None: return;
        case Task::Kind::Send: finishSend(task); return;
        case Task::Kind::Job: finishJob(task); return;
    }
}

uint32_t Messaging::step() {
    Task task;
    const uint32_t wait = prepare(task);
    if (task.kind == Task::Kind::None) return wait;
    run(task);
    finish(task);
    return 0;
}

void Messaging::finishSend(Task& task) {
    const OutboxEntry& entry = task.entry;
    Contact contact;
    const bool known = storage_.people.findContact(entry.contact, contact);

    if (task.ok) {
        storage_.markSent(entry, task.sent.messageId, task.sent.file.fileId);
        log_.info("Recording sent to contact " + std::to_string(entry.contact));
        StoredMessage message;
        if (known && storage_.messages.find(entry.contact, entry.messageId, message)) {
            queueCopies(StoredMessage::Direction::Sent, contact, task.sent.file.fileId, message.time);
        }
        return;
    }

    const Error& e = task.error;
    const uint32_t attempts = entry.attempts + 1;
    log_.warning("Couldn't send a recording, try " + std::to_string(attempts) + " (" + telegram::toString(e.kind) +
                 ": " + e.description + ")");

    if (isRefusal(e)) {
        // Usually means they've blocked the bot or deleted the chat. Keep trying
        // now and then, in case they start it again, and tell the admins once.
        if (!entry.adminsTold && known) {
            tellAdmins(childName() + "'s voice note to " + contact.name +
                       " couldn't be sent. Telegram says they can't be reached, which usually means they've "
                       "blocked or stopped the bot. The box will keep trying every hour. Ask " +
                       contact.name + " to open the chat with the bot and press Start.");
        }
        storage_.outbox.recordFailure(entry.sequence, clock_.millisSinceBoot() + kUnreachableRetryMs, true);
        return;
    }
    storage_.outbox.recordFailure(entry.sequence, retryAt(attempts, e));
}

void Messaging::finishJob(Task& task) {
    const Job& job = task.job;
    if (job.kind == Job::Kind::Download) {
        finishDownload(task);
        return;
    }
    if (task.ok) {
        storage_.jobs.remove(job.sequence);
        if (job.kind == Job::Kind::Reaction) log_.info("Sent the listened reaction");
        return;
    }

    const Error& e = task.error;
    const uint32_t attempts = job.attempts + 1;
    log_.warning(std::string("Couldn't send a ") + jobName(job.kind) + ", try " + std::to_string(attempts) + " (" +
                 telegram::toString(e.kind) + ": " + e.description + ")");
    if (isRefusal(e) || attempts >= kMaxJobAttempts) {
        // A reaction to a deleted message, a monitor who blocked the bot and so on.
        log_.info(std::string("Gave up on a ") + jobName(job.kind));
        storage_.jobs.remove(job.sequence);
        return;
    }
    storage_.jobs.recordFailure(job.sequence, retryAt(attempts, e));
}

void Messaging::finishDownload(Task& task) {
    const Job& job = task.job;
    Contact contact;
    if (!storage_.people.findContact(job.contact, contact)) {
        // Removed while it was downloading.
        storage_.files().removeFile(paths::kDownload);
        storage_.jobs.remove(job.sequence);
        return;
    }

    if (task.ok) {
        // Already stored? (The box restarted after storing it but before removing the job.)
        for (const auto& m : storage_.messages.messages(job.contact)) {
            if (m.direction == StoredMessage::Direction::Received && m.telegramMessageId == job.telegramMessageId) {
                storage_.files().removeFile(paths::kDownload);
                storage_.jobs.remove(job.sequence);
                return;
            }
        }

        StoredMessage m;
        m.direction = StoredMessage::Direction::Received;
        m.status = StoredMessage::Status::Unread;
        m.time = job.time;
        m.durationMs = job.durationMs;
        m.telegramMessageId = job.telegramMessageId;
        m.telegramFileId = job.fileId;
        const uint32_t id = storage_.messages.add(job.contact, m, paths::kDownload);
        if (id == 0) {
            log_.error("Couldn't store a voice note on the SD card; will try again");
            storage_.files().removeFile(paths::kDownload);
            storage_.jobs.recordFailure(job.sequence, retryAt(job.attempts + 1, Error()));
            return;
        }
        storage_.jobs.remove(job.sequence);
        log_.info("Voice note " + std::to_string(id) + " from contact " + std::to_string(job.contact) + " stored");
        queueCopies(StoredMessage::Direction::Received, contact, job.fileId, job.time);
        return;
    }

    const Error& e = task.error;
    const uint32_t attempts = job.attempts + 1;
    log_.warning("Couldn't download a voice note, try " + std::to_string(attempts) + " (" +
                 telegram::toString(e.kind) + ": " + e.description + ")");
    if (isRefusal(e) || attempts >= kMaxJobAttempts) {
        log_.error("Gave up downloading a voice note");
        storage_.jobs.remove(job.sequence);
        queueNotice(contact.telegramId,
                    "Sorry, your voice note didn't reach " + childName() + "'s box. Please send it again.");
        return;
    }
    storage_.jobs.recordFailure(job.sequence, retryAt(attempts, e));
}

// ---------------------------------------------------------------------------
// Monitor copies and notices

void Messaging::queueCopies(StoredMessage::Direction direction, const Contact& contact, const std::string& fileId,
                            int64_t time) {
    const MonitorMode mode = storage_.settings.monitorMode();
    const bool sent = direction == StoredMessage::Direction::Sent;
    const bool wanted = mode == MonitorMode::All || (sent && mode == MonitorMode::SentOnly) ||
                        (!sent && mode == MonitorMode::ReceivedOnly);
    if (!wanted || fileId.empty()) return;

    const std::string caption =
        sent ? copyCaption(childName(), contact.name, time) : copyCaption(contact.name, childName(), time);
    for (const auto& monitor : storage_.people.monitors()) {
        // The contact already has their own voice note.
        if (monitor.telegramId == contact.telegramId) continue;
        Job job;
        job.kind = Job::Kind::Copy;
        job.chatId = monitor.telegramId;
        job.fileId = fileId;
        job.text = caption;
        if (storage_.jobs.add(job) == 0) log_.warning("Couldn't queue a monitor copy");
    }
}

std::string Messaging::copyCaption(const std::string& from, const std::string& to, int64_t unixTime) const {
    std::string caption = from + " to " + to;
    LocalTime local;
    if (unixTime > 0 && timezones::toLocal(storage_.settings.timezone(), unixTime, local)) {
        caption += ", " + formatHourMinute(local);
    }
    return caption;
}

void Messaging::queueNotice(int64_t chatId, const std::string& text) {
    Job job;
    job.kind = Job::Kind::Notice;
    job.chatId = chatId;
    job.text = text;
    if (storage_.jobs.add(job) == 0) log_.warning("Couldn't queue a message: " + text);
}

void Messaging::tellAdmins(const std::string& text) {
    for (const auto& admin : storage_.people.admins()) queueNotice(admin.telegramId, text);
}

// ---------------------------------------------------------------------------
// Retry timing

uint32_t Messaging::retryDelayMs(uint32_t attempts) {
    if (attempts == 0) return 0;
    uint64_t delay = kFirstRetryMs;
    for (uint32_t i = 1; i < attempts && delay < kLongestRetryMs; ++i) delay *= 2;
    return static_cast<uint32_t>(std::min<uint64_t>(delay, kLongestRetryMs));
}

uint64_t Messaging::retryAt(uint32_t attempts, const Error& error) const {
    uint64_t delay = retryDelayMs(attempts);
    if (error.kind == Error::Kind::Unauthorized) delay = kUnreachableRetryMs;  // the bot's problem; it shows the screen
    if (error.kind == Error::Kind::RateLimited && error.retryAfterSeconds > 0) {
        delay = std::max<uint64_t>(delay, static_cast<uint64_t>(error.retryAfterSeconds) * 1000);
    }
    return clock_.millisSinceBoot() + delay;
}

}  // namespace fvb

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "app/bot/Bot.h"
#include "app/storage/Storage.h"
#include "hal/Clock.h"
#include "hal/Logger.h"
#include "telegram/TelegramClient.h"

namespace fvb {

// Voice notes in and out (section 7 of the requirements), the 👀 reaction and
// monitor copies (section 11).
//
// Coming in, from the bot:
//   - A contact's voice note is queued to download. Once it's on the card it
//     shows as unread on the box.
//   - Anything else a contact sends (text, photo, sticker, video, music and so
//     on) gets "[Child] can only receive voice notes.", at most once a minute
//     each, so an album of ten photos gets one reply.
//
// Going out, in the background:
//   - The outbox: the child's recordings, sent as voice notes with their length.
//   - Downloads, 👀 reactions, monitor copies and notices to admins (JobQueue).
//
// Everything that fails is tried again later, with longer gaps each time (see
// retryDelayMs). The child's recordings are never given up on. If Telegram says
// a contact can't be reached (they've blocked or stopped the bot), the admins are
// told once and the box keeps trying every hour, in case they start it again.
// Recordings to one contact always go in the order the child sent them, and
// voice notes from one contact are stored in the order they arrived.
//
// Uses its own TelegramClient, separate from the bot's, so sending doesn't wait
// for the bot's long poll and the two never share a connection.
//
// Doing the work is split in three, like Bot::poll and Bot::handle, so the slow
// part can run without holding the box's lock:
//
//   prepare()   picks the next thing that's due. Touches storage.
//   run()       talks to Telegram. Doesn't touch storage, apart from the
//               TelegramClient reading or writing the audio file itself.
//   finish()    records the result. Touches storage.
//
// step() does all three, for tests.
class Messaging {
public:
    // Gaps between tries: 5 s, 10 s, 20 s ... up to 15 minutes.
    static constexpr uint32_t kFirstRetryMs = 5000;
    static constexpr uint32_t kLongestRetryMs = 15 * 60 * 1000;
    // When Telegram says a contact can't be reached, or rejects the bot token.
    static constexpr uint32_t kUnreachableRetryMs = 60 * 60 * 1000;
    // How often prepare() looks again when nothing is waiting.
    static constexpr uint32_t kIdleCheckMs = 1000;
    // Jobs (not recordings) are dropped after this many failed tries. With the
    // gaps above that's well over a day.
    static constexpr uint32_t kMaxJobAttempts = 100;
    // The "can only receive voice notes" reply goes to each person at most this often.
    static constexpr int64_t kVoiceOnlyReplyGapMs = 60 * 1000;

    struct Task {
        enum class Kind : uint8_t { None, Send, Job };
        Kind kind = Kind::None;
        OutboxEntry entry;  // for Send
        Job job;            // for Job

        // Filled in by prepare().
        int64_t chatId = 0;
        std::string audioPath;
        int durationSeconds = 0;

        // Filled in by run().
        bool ok = false;
        telegram::Error error;
        telegram::Message sent;
    };

    // Registers the message handler with the bot. `client` is the one used for
    // sending, not the bot's own.
    Messaging(Bot& bot, telegram::TelegramClient& client, Storage& storage, Clock& clock, Logger& log);

    // Fills in `task` with the next thing due. If nothing is due, task.kind is
    // None and it returns how many milliseconds until it's worth asking again.
    uint32_t prepare(Task& task);
    void run(Task& task);
    void finish(Task& task);

    // prepare, run and finish. Returns how long to wait (0 if it did something).
    uint32_t step();

    // How long to wait after the nth failed try (n starts at 1).
    static uint32_t retryDelayMs(uint32_t attempts);

    // The caption on a monitor's copy, e.g. "Child to Granny, 14:32". The time is
    // in the box's own timezone, and left off if that isn't known yet.
    std::string copyCaption(const std::string& from, const std::string& to, int64_t unixTime) const;

private:
    void onMessage(Context& ctx, const telegram::Message& message);
    void queueDownload(Context& ctx, const telegram::Message& message, const Contact& contact);

    void finishSend(Task& task);
    void finishJob(Task& task);
    void finishDownload(Task& task);
    // Queues a copy for every monitor, if the monitor setting includes this direction.
    void queueCopies(StoredMessage::Direction direction, const Contact& contact, const std::string& fileId,
                     int64_t time);
    void queueNotice(int64_t chatId, const std::string& text);
    void tellAdmins(const std::string& text);
    // Next try time for a failure, honouring Telegram's "wait this long".
    uint64_t retryAt(uint32_t attempts, const telegram::Error& error) const;
    std::string childName() const;

    telegram::TelegramClient& client_;
    Storage& storage_;
    Clock& clock_;
    Logger& log_;

    // When each person last got the "can only receive voice notes" reply.
    // Only kept in memory: after a restart they might get it once more.
    std::map<int64_t, uint64_t> lastVoiceOnlyReply_;
};

}  // namespace fvb

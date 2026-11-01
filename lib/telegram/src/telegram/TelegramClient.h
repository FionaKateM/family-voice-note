#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "hal/FileStorage.h"
#include "hal/Logger.h"
#include "hal/Network.h"
#include "telegram/Types.h"

namespace fvb {
namespace telegram {

// Why the last call failed.
struct Error {
    enum class Kind {
        None,
        Network,       // couldn't reach Telegram (offline, timeout)
        Unauthorized,  // bot token missing or rejected: show the setup problem screen
        Forbidden,     // the person has blocked the bot or never started a chat with it
        BadRequest,    // Telegram didn't accept the request (e.g. chat not found)
        Conflict,      // something else is already reading this bot's messages
        RateLimited,   // sending too fast; wait retryAfterSeconds
        Server,        // Telegram had a problem; try again later
        BadResponse,   // Telegram's reply couldn't be understood
        Local,         // a problem on the box (file missing, SD card write failed, bad input)
    };

    Kind kind = Kind::None;
    int httpStatus = 0;
    std::string description;  // never contains the bot token
    int retryAfterSeconds = 0;  // only for RateLimited

    // True if trying the same thing again later might work. The outbox uses this
    // to decide between retrying and giving up.
    bool isTemporary() const {
        return kind == Kind::Network || kind == Kind::RateLimited || kind == Kind::Server ||
               kind == Kind::Conflict;
    }
};

const char* toString(Error::Kind kind);

// Talks to Telegram's Bot API over the Network interface.
//
// This is a small client written for the box rather than UniversalTelegramBot.
// See docs/telegram.md for why.
//
// Every call blocks until Telegram answers (up to ~35 seconds for getUpdates).
// Calls return true on success. On failure they return false and lastError()
// says why. Nothing here retries by itself: the caller decides.
//
// Never logs the bot token or anyone's Telegram ID.
class TelegramClient {
public:
    // How long getUpdates waits for something to arrive (section 14 of the requirements).
    static constexpr uint32_t kLongPollSeconds = 25;
    // Telegram's limit on downloads through the Bot API.
    static constexpr int64_t kMaxDownloadBytes = 20LL * 1024 * 1024;
    // Telegram's limit on button callback data.
    static constexpr size_t kMaxCallbackDataBytes = 64;
    // The reaction sent when the child has listened to a message. It's on
    // Telegram's list of allowed reaction emoji.
    static constexpr const char* kListenedReaction = "\xF0\x9F\x91\x80";  // 👀

    TelegramClient(Network& network, FileStorage& files, Logger& log, std::string botToken,
                   std::string apiBase = "https://api.telegram.org");

    const Error& lastError() const { return lastError_; }

    // ---- The bot itself ----

    // Checks the token and learns the bot's username. Call once when online.
    bool getMe(BotInfo& info);
    // The username from the last successful getMe, or empty.
    const std::string& botUsername() const { return botUsername_; }

    // ---- Receiving ----

    // Waits up to timeoutSeconds for new messages and button presses, and adds
    // them to `updates` in order. Returns true with no updates if nothing arrived.
    //
    // Each call tells Telegram the previous batch has been dealt with, so it
    // won't be sent again. Updates Telegram sends that can't be understood are
    // logged and skipped, so one bad update can't block everything behind it.
    bool getUpdates(std::vector<Update>& updates, uint32_t timeoutSeconds = kLongPollSeconds);

    // The next update ID to ask for. Save this after handling each batch and
    // restore it at startup, otherwise the last batch is handled twice after a
    // restart. (Saving it is up to the caller.)
    int64_t nextUpdateId() const { return nextUpdateId_; }
    void setNextUpdateId(int64_t id) { nextUpdateId_ = id; }

    // ---- Sending text and buttons ----

    // Sends plain text (no formatting, so names with * or _ show as typed).
    // `keyboard` is optional. `sent` (optional) receives the message Telegram created.
    bool sendMessage(ChatId chatId, const std::string& text, const InlineKeyboard* keyboard = nullptr,
                     Message* sent = nullptr);
    // Changes the text and buttons of a message the bot sent earlier. Pass
    // nullptr or an empty keyboard to remove the buttons.
    bool editMessageText(ChatId chatId, int64_t messageId, const std::string& text,
                         const InlineKeyboard* keyboard = nullptr);
    // Must be called for every button press, or the button shows a spinner on
    // the person's phone for a while. `text` (optional) pops up briefly.
    bool answerCallbackQuery(const std::string& callbackQueryId, const std::string& text = "");

    // ---- Voice notes ----

    // Uploads a recording from the SD card as a voice note. Including the
    // duration makes it show as a normal voice bubble with the right length.
    // Works with .mp3 and .ogg (Opus) files. `sent->file.fileId` can then be
    // used to send copies without uploading again.
    bool sendVoiceFile(ChatId chatId, const std::string& sdPath, int durationSeconds,
                       const std::string& caption = "", Message* sent = nullptr);
    // Sends a voice note Telegram already has (used for monitor copies).
    bool sendVoiceById(ChatId chatId, const std::string& fileId, const std::string& caption = "",
                       Message* sent = nullptr);

    // ---- Reactions ----

    // Puts an emoji reaction on a message (e.g. kListenedReaction). An empty
    // emoji removes the bot's reaction.
    bool setReaction(ChatId chatId, int64_t messageId, const std::string& emoji);

    // ---- Files and photos ----

    bool getFile(const std::string& fileId, FileInfo& info);
    // Downloads a file Telegram holds (a voice note, photo, etc.) to the SD card.
    // Writes to "<sdPath>.part" first and only renames it once the whole file has
    // arrived, so a half-finished download is never mistaken for a real one.
    bool downloadFile(const std::string& fileId, const std::string& sdPath);

    // Finds someone's current profile photo, choosing the size closest to
    // `targetWidth`. Returns true with hasPhoto false if they have none, or
    // their privacy settings hide it from bots.
    bool getProfilePhoto(UserId userId, PhotoSize& photo, bool& hasPhoto, int targetWidth = 320);

    // ---- Invite links ----

    // A t.me link that opens a chat with this bot and sends "/start <code>".
    // Needs getMe to have succeeded first. Returns empty if the username isn't
    // known yet or the code isn't valid.
    std::string inviteLink(const std::string& code) const;
    // Telegram allows 1 to 64 letters, digits, _ and - in a start code.
    static bool isValidStartCode(const std::string& code);

private:
    // Sends a JSON request to a Bot API method. On success, `reply` holds
    // Telegram's whole answer and reply["result"] is the useful part.
    bool call(const std::string& method, const JsonDocument& request, JsonDocument& reply,
              uint32_t timeoutMs = 30000);
    bool send(const std::string& method, const HttpRequest& request, JsonDocument& reply);
    bool fail(Error::Kind kind, const std::string& description, int httpStatus = 0);
    bool parseSent(JsonVariantConst result, Message* sent);
    std::string redact(const std::string& text) const;
    std::string methodUrl(const std::string& method) const;

    Network& network_;
    FileStorage& files_;
    Logger& log_;
    std::string token_;
    std::string apiBase_;
    std::string botUsername_;
    int64_t nextUpdateId_ = 0;
    Error lastError_;
};

}  // namespace telegram
}  // namespace fvb

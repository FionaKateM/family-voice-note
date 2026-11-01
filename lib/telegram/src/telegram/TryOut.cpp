#include "telegram/TryOut.h"

#include <utility>
#include <vector>

namespace fvb {
namespace telegram {

namespace {
const char* kHelp =
    "Try:\n"
    "/buttons to see tap buttons\n"
    "/photo to save your profile photo\n"
    "or send a voice note and it will come back to you.";
}

TryOut::TryOut(TelegramClient& client, Logger& log, std::string joinCode, std::string saveFolder)
    : client_(client), log_(log), joinCode_(std::move(joinCode)), saveFolder_(std::move(saveFolder)) {}

bool TryOut::start() {
    BotInfo bot;
    if (!client_.getMe(bot)) {
        warn("Couldn't check the bot token");
        return false;
    }
    link_ = client_.inviteLink(joinCode_);
    if (link_.empty()) {
        log_.error("Couldn't make an invite link (does the bot have a username?)");
        return false;
    }
    log_.info("Connected to Telegram as @" + bot.username);
    log_.info("Open this link on your phone to start: " + link_);
    return true;
}

uint32_t TryOut::step() {
    std::vector<Update> updates;
    if (!client_.getUpdates(updates)) {
        warn("Couldn't get messages");
        const Error& e = client_.lastError();
        if (e.kind == Error::Kind::RateLimited && e.retryAfterSeconds > 0) {
            return static_cast<uint32_t>(e.retryAfterSeconds) * 1000;
        }
        return 5000;
    }
    for (const auto& update : updates) handle(update);
    return 0;
}

void TryOut::handle(const Update& update) {
    if (update.type == Update::Type::Message) {
        handleMessage(update.message);
    } else if (update.type == Update::Type::CallbackQuery) {
        handleButton(update.callbackQuery);
    }
}

void TryOut::handleMessage(const Message& message) {
    if (!message.isPrivateChat()) return;

    std::string command, argument;
    const bool isCommand = message.kind == MessageKind::Text && parseCommand(message.text, command, argument);

    if (!hasJoined()) {
        // Only the invite link lets someone in. Everyone else is ignored, with no reply.
        if (isCommand && command == "start" && argument == joinCode_) {
            chat_ = message.chatId;
            log_.info("Someone joined through the invite link");
            reply(std::string("Connected to the laptop box.\n") + kHelp);
        }
        return;
    }
    if (message.chatId != chat_) return;

    if (message.kind == MessageKind::Voice) {
        handleVoice(message);
    } else if (isCommand && command == "buttons") {
        InlineKeyboard keyboard = {{{"Yes", "try:yes"}, {"No", "try:no"}}};
        reply("Do the buttons work?", &keyboard);
    } else if (isCommand && command == "photo") {
        handlePhotoCommand(message);
    } else {
        reply(std::string("Got a ") + toString(message.kind) + ".\n" + kHelp);
    }
}

void TryOut::handleButton(const CallbackQuery& query) {
    if (!hasJoined() || query.chatId != chat_) return;

    if (!client_.answerCallbackQuery(query.id, "Got it")) warn("Couldn't answer the button press");
    const std::string choice = query.data == "try:yes" ? "Yes" : query.data == "try:no" ? "No" : "something else";
    if (!client_.editMessageText(query.chatId, query.messageId, "You pressed " + choice + ".")) {
        warn("Couldn't edit the message");
    }
}

void TryOut::handleVoice(const Message& message) {
    const std::string path = saveFolder_ + "/voice-" + std::to_string(message.messageId) + ".ogg";
    if (!client_.downloadFile(message.file.fileId, path)) {
        warn("Couldn't download the voice note");
        reply("Couldn't download that voice note.");
        return;
    }
    log_.info("Saved a voice note to " + path);

    if (!client_.setReaction(chat_, message.messageId, TelegramClient::kListenedReaction)) {
        warn("Couldn't add the reaction");
    }

    Message sent;
    if (!client_.sendVoiceFile(chat_, path, message.file.durationSeconds, "Your voice note, uploaded again",
                               &sent)) {
        warn("Couldn't upload the voice note");
        reply("Couldn't send the voice note back.");
        return;
    }
    if (!client_.sendVoiceById(chat_, sent.file.fileId, "And again, re-sent without uploading")) {
        warn("Couldn't re-send the voice note");
    }
}

void TryOut::handlePhotoCommand(const Message& message) {
    PhotoSize photo;
    bool hasPhoto = false;
    if (!client_.getProfilePhoto(message.from.id, photo, hasPhoto)) {
        warn("Couldn't look up the profile photo");
        reply("Couldn't look up your profile photo.");
        return;
    }
    if (!hasPhoto) {
        reply("I can't see a profile photo. You might not have one, or your privacy settings hide it from bots.");
        return;
    }
    const std::string path = saveFolder_ + "/profile.jpg";
    if (!client_.downloadFile(photo.fileId, path)) {
        warn("Couldn't download the profile photo");
        reply("Found your profile photo but couldn't download it.");
        return;
    }
    reply("Saved your profile photo (" + std::to_string(photo.width) + "x" + std::to_string(photo.height) +
          ") to the pretend SD card.");
}

void TryOut::reply(const std::string& text, const InlineKeyboard* keyboard) {
    if (!client_.sendMessage(chat_, text, keyboard)) warn("Couldn't send a message");
}

void TryOut::warn(const std::string& what) {
    const Error& e = client_.lastError();
    log_.warning(what + " (" + toString(e.kind) + ": " + e.description + ")");
}

}  // namespace telegram
}  // namespace fvb

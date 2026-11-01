#pragma once

// The pieces of Telegram's Bot API the box uses, cut down to the fields it needs.
// Full reference: https://core.telegram.org/bots/api

#include <cstdint>
#include <string>
#include <vector>

namespace fvb {
namespace telegram {

// Telegram user and chat IDs can be larger than 32 bits, so always use int64_t.
using ChatId = int64_t;
using UserId = int64_t;

struct User {
    UserId id = 0;
    bool isBot = false;
    std::string firstName;
    std::string lastName;
    std::string username;  // without the @, may be empty
    std::string languageCode;
};

// The bot itself, from getMe.
struct BotInfo {
    UserId id = 0;
    std::string username;  // used to build invite links
    std::string firstName;
};

// One size of a photo. Telegram sends several sizes of every photo.
struct PhotoSize {
    std::string fileId;
    std::string fileUniqueId;
    int width = 0;
    int height = 0;
    int64_t fileSize = 0;  // 0 if Telegram didn't say
};

// Any file Telegram stores: voice notes, documents, audio and so on.
struct FileRef {
    std::string fileId;  // use this to download or re-send it
    std::string fileUniqueId;  // stays the same for the same file, even across bots
    std::string mimeType;
    std::string fileName;
    int64_t fileSize = 0;  // 0 if Telegram didn't say
    int durationSeconds = 0;  // voice, audio and video only
};

// What kind of message someone sent. The box only accepts Voice from contacts.
// Everything else gets the "can only receive voice notes" reply (ticket 1.7),
// apart from photos during contact setup (ticket 1.5).
enum class MessageKind {
    Text,
    Voice,
    Photo,        // a normal photo
    PhotoAsFile,  // a photo sent as a file, which setup turns down
    Document,
    Sticker,
    Video,
    VideoNote,  // round video message
    Audio,      // music file
    Animation,  // GIF
    Other,
};

const char* toString(MessageKind kind);

struct Message {
    int64_t messageId = 0;
    ChatId chatId = 0;
    std::string chatType;  // "private" for one-to-one chats with the bot
    User from;
    int64_t date = 0;  // seconds since 1 January 1970 (UTC)

    MessageKind kind = MessageKind::Other;
    std::string text;     // for Text
    std::string caption;  // for media, may be empty
    FileRef file;         // for Voice, PhotoAsFile, Document, Sticker, Video, VideoNote, Audio, Animation
    std::vector<PhotoSize> photos;  // for Photo, every size Telegram offered

    bool isPrivateChat() const { return chatType == "private"; }
};

// Someone pressed a button under one of the bot's messages.
struct CallbackQuery {
    std::string id;  // pass to answerCallbackQuery
    User from;
    std::string data;  // the button's callback data
    // The message the button was on. Both are 0 if Telegram didn't include it
    // (only happens for very old messages).
    ChatId chatId = 0;
    int64_t messageId = 0;
};

struct Update {
    enum class Type { Message, CallbackQuery, Other };

    int64_t updateId = 0;
    Type type = Type::Other;
    Message message;              // for Type::Message
    CallbackQuery callbackQuery;  // for Type::CallbackQuery
};

// A tap button shown under a message. Pressing it sends callbackData back to
// the bot as a CallbackQuery. Telegram limits callbackData to 64 bytes.
struct InlineButton {
    std::string text;
    std::string callbackData;
};

// Rows of buttons, top to bottom, each row left to right.
using InlineKeyboard = std::vector<std::vector<InlineButton>>;

// Telegram's details about a stored file, from getFile.
struct FileInfo {
    std::string fileId;
    std::string filePath;  // used to build the download address
    int64_t fileSize = 0;
};

// Picks the photo size whose width is closest to targetWidth (for example 320,
// the Core2 screen width). On a tie, picks the larger one. Returns nullptr if
// the list is empty.
const PhotoSize* closestToWidth(const std::vector<PhotoSize>& sizes, int targetWidth);

// Splits "/start abc" or "/start@SomeBot abc" into command "start" and
// argument "abc". The command is lower-cased. Returns false if the text isn't
// a command.
bool parseCommand(const std::string& text, std::string& command, std::string& argument);

}  // namespace telegram
}  // namespace fvb

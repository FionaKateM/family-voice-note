#include "telegram/Parse.h"

#include <cctype>

namespace fvb {
namespace telegram {

namespace {

std::string str(JsonVariantConst v) {
    const char* s = v.as<const char*>();
    return s ? std::string(s) : std::string();
}

void parseFile(JsonVariantConst json, FileRef& file) {
    file.fileId = str(json["file_id"]);
    file.fileUniqueId = str(json["file_unique_id"]);
    file.mimeType = str(json["mime_type"]);
    file.fileName = str(json["file_name"]);
    file.fileSize = json["file_size"] | static_cast<int64_t>(0);
    file.durationSeconds = json["duration"] | 0;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

const char* toString(MessageKind kind) {
    switch (kind) {
        case MessageKind::Text: return "text";
        case MessageKind::Voice: return "voice note";
        case MessageKind::Photo: return "photo";
        case MessageKind::PhotoAsFile: return "photo sent as a file";
        case MessageKind::Document: return "file";
        case MessageKind::Sticker: return "sticker";
        case MessageKind::Video: return "video";
        case MessageKind::VideoNote: return "round video";
        case MessageKind::Audio: return "music file";
        case MessageKind::Animation: return "GIF";
        case MessageKind::Other: return "other";
    }
    return "other";
}

const PhotoSize* closestToWidth(const std::vector<PhotoSize>& sizes, int targetWidth) {
    const PhotoSize* best = nullptr;
    int bestGap = 0;
    for (const auto& size : sizes) {
        int gap = size.width > targetWidth ? size.width - targetWidth : targetWidth - size.width;
        if (!best || gap < bestGap || (gap == bestGap && size.width > best->width)) {
            best = &size;
            bestGap = gap;
        }
    }
    return best;
}

bool parseCommand(const std::string& text, std::string& command, std::string& argument) {
    if (text.size() < 2 || text[0] != '/') return false;
    size_t end = text.find_first_of(" \n\t");
    std::string word = text.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    // "/start@SomeBot" -> "start"
    size_t at = word.find('@');
    if (at != std::string::npos) word.resize(at);
    if (word.empty()) return false;
    for (char& c : word) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    command = word;

    argument.clear();
    if (end != std::string::npos) {
        size_t first = text.find_first_not_of(" \n\t", end);
        size_t last = text.find_last_not_of(" \n\t");
        if (first != std::string::npos) argument = text.substr(first, last - first + 1);
    }
    return true;
}

bool parseUser(JsonVariantConst json, User& user) {
    if (!json["id"].is<int64_t>()) return false;
    user.id = json["id"].as<int64_t>();
    user.isBot = json["is_bot"] | false;
    user.firstName = str(json["first_name"]);
    user.lastName = str(json["last_name"]);
    user.username = str(json["username"]);
    user.languageCode = str(json["language_code"]);
    return true;
}

bool parsePhotoSizes(JsonVariantConst json, std::vector<PhotoSize>& sizes) {
    if (!json.is<JsonArrayConst>()) return false;
    sizes.clear();
    for (JsonVariantConst item : json.as<JsonArrayConst>()) {
        PhotoSize size;
        size.fileId = str(item["file_id"]);
        size.fileUniqueId = str(item["file_unique_id"]);
        size.width = item["width"] | 0;
        size.height = item["height"] | 0;
        size.fileSize = item["file_size"] | static_cast<int64_t>(0);
        if (!size.fileId.empty()) sizes.push_back(size);
    }
    return true;
}

bool parseMessage(JsonVariantConst json, Message& message) {
    if (!json["message_id"].is<int64_t>() || !json["chat"]["id"].is<int64_t>()) return false;
    message = Message();
    message.messageId = json["message_id"].as<int64_t>();
    message.chatId = json["chat"]["id"].as<int64_t>();
    message.chatType = str(json["chat"]["type"]);
    parseUser(json["from"], message.from);
    message.date = json["date"] | static_cast<int64_t>(0);
    message.caption = str(json["caption"]);

    // Order matters: Telegram also fills in "document" for GIFs.
    if (json["voice"].is<JsonObjectConst>()) {
        message.kind = MessageKind::Voice;
        parseFile(json["voice"], message.file);
    } else if (json["text"].is<const char*>()) {
        message.kind = MessageKind::Text;
        message.text = str(json["text"]);
    } else if (json["photo"].is<JsonArrayConst>()) {
        message.kind = MessageKind::Photo;
        parsePhotoSizes(json["photo"], message.photos);
    } else if (json["animation"].is<JsonObjectConst>()) {
        message.kind = MessageKind::Animation;
        parseFile(json["animation"], message.file);
    } else if (json["sticker"].is<JsonObjectConst>()) {
        message.kind = MessageKind::Sticker;
        parseFile(json["sticker"], message.file);
    } else if (json["video_note"].is<JsonObjectConst>()) {
        message.kind = MessageKind::VideoNote;
        parseFile(json["video_note"], message.file);
    } else if (json["video"].is<JsonObjectConst>()) {
        message.kind = MessageKind::Video;
        parseFile(json["video"], message.file);
    } else if (json["audio"].is<JsonObjectConst>()) {
        message.kind = MessageKind::Audio;
        parseFile(json["audio"], message.file);
    } else if (json["document"].is<JsonObjectConst>()) {
        parseFile(json["document"], message.file);
        message.kind = startsWith(message.file.mimeType, "image/") ? MessageKind::PhotoAsFile
                                                                   : MessageKind::Document;
    } else {
        message.kind = MessageKind::Other;
    }
    return true;
}

bool parseCallbackQuery(JsonVariantConst json, CallbackQuery& query) {
    if (!json["id"].is<const char*>()) return false;
    query = CallbackQuery();
    query.id = str(json["id"]);
    parseUser(json["from"], query.from);
    query.data = str(json["data"]);
    if (json["message"].is<JsonObjectConst>()) {
        query.chatId = json["message"]["chat"]["id"] | static_cast<int64_t>(0);
        query.messageId = json["message"]["message_id"] | static_cast<int64_t>(0);
    }
    return true;
}

bool parseUpdate(JsonVariantConst json, Update& update) {
    if (!json["update_id"].is<int64_t>()) return false;
    update = Update();
    update.updateId = json["update_id"].as<int64_t>();
    if (json["message"].is<JsonObjectConst>()) {
        if (!parseMessage(json["message"], update.message)) return false;
        update.type = Update::Type::Message;
    } else if (json["callback_query"].is<JsonObjectConst>()) {
        if (!parseCallbackQuery(json["callback_query"], update.callbackQuery)) return false;
        update.type = Update::Type::CallbackQuery;
    }
    return true;
}

void writeKeyboard(JsonObject request, const InlineKeyboard& keyboard) {
    JsonArray rows = request["reply_markup"]["inline_keyboard"].to<JsonArray>();
    for (const auto& row : keyboard) {
        JsonArray jsonRow = rows.add<JsonArray>();
        for (const auto& button : row) {
            JsonObject b = jsonRow.add<JsonObject>();
            b["text"] = button.text;
            b["callback_data"] = button.callbackData;
        }
    }
}

}  // namespace telegram
}  // namespace fvb

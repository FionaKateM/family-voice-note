#include "telegram/TelegramClient.h"

#include <utility>

#include "telegram/Multipart.h"
#include "telegram/Parse.h"

namespace fvb {
namespace telegram {

namespace {

bool keyboardIsValid(const InlineKeyboard& keyboard) {
    for (const auto& row : keyboard) {
        for (const auto& button : row) {
            if (button.text.empty() || button.callbackData.empty() ||
                button.callbackData.size() > TelegramClient::kMaxCallbackDataBytes) {
                return false;
            }
        }
    }
    return true;
}

std::string baseName(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

const char* toString(Error::Kind kind) {
    switch (kind) {
        case Error::Kind::None: return "none";
        case Error::Kind::Network: return "network";
        case Error::Kind::Unauthorized: return "unauthorized";
        case Error::Kind::Forbidden: return "forbidden";
        case Error::Kind::BadRequest: return "bad request";
        case Error::Kind::Conflict: return "conflict";
        case Error::Kind::RateLimited: return "rate limited";
        case Error::Kind::Server: return "server";
        case Error::Kind::BadResponse: return "bad response";
        case Error::Kind::Local: return "local";
    }
    return "unknown";
}

TelegramClient::TelegramClient(Network& network, FileStorage& files, Logger& log, std::string botToken,
                               std::string apiBase)
    : network_(network), files_(files), log_(log), token_(std::move(botToken)), apiBase_(std::move(apiBase)) {}

// ---------------------------------------------------------------------------
// Plumbing

std::string TelegramClient::methodUrl(const std::string& method) const {
    return apiBase_ + "/bot" + token_ + "/" + method;
}

std::string TelegramClient::redact(const std::string& text) const {
    if (token_.empty()) return text;
    std::string out = text;
    for (size_t pos = out.find(token_); pos != std::string::npos; pos = out.find(token_, pos)) {
        out.replace(pos, token_.size(), "<token>");
    }
    return out;
}

bool TelegramClient::fail(Error::Kind kind, const std::string& description, int httpStatus) {
    lastError_ = Error();
    lastError_.kind = kind;
    lastError_.httpStatus = httpStatus;
    lastError_.description = redact(description);
    return false;
}

bool TelegramClient::call(const std::string& method, const JsonDocument& request, JsonDocument& reply,
                          uint32_t timeoutMs) {
    HttpRequest http;
    http.method = "POST";
    http.url = methodUrl(method);
    http.headers["Content-Type"] = "application/json";
    serializeJson(request, http.body);
    http.timeoutMs = timeoutMs;
    return send(method, http, reply);
}

bool TelegramClient::send(const std::string& method, const HttpRequest& request, JsonDocument& reply) {
    if (token_.empty()) return fail(Error::Kind::Unauthorized, "no bot token");

    HttpResponse response = network_.request(request);
    if (response.status == 0) {
        return fail(Error::Kind::Network, method + ": " + response.error);
    }

    reply.clear();
    DeserializationError parseError = deserializeJson(reply, response.body);
    if (parseError) {
        // Telegram always answers in JSON, so this is probably a proxy or captive
        // Wi-Fi page. Treat server-side codes as temporary.
        Error::Kind kind = response.status >= 500 ? Error::Kind::Server : Error::Kind::BadResponse;
        return fail(kind, method + ": reply wasn't JSON (HTTP " + std::to_string(response.status) + ")",
                    response.status);
    }

    if (reply["ok"] == true && response.ok()) {
        lastError_ = Error();
        return true;
    }

    int code = reply["error_code"] | response.status;
    std::string description = method + ": " + (reply["description"] | "no description");
    Error::Kind kind;
    switch (code) {
        case 401:
        case 404:  // Telegram answers 404 for a token that isn't even the right shape
            kind = Error::Kind::Unauthorized;
            break;
        case 403: kind = Error::Kind::Forbidden; break;
        case 409: kind = Error::Kind::Conflict; break;
        case 429: kind = Error::Kind::RateLimited; break;
        default: kind = code >= 500 ? Error::Kind::Server : Error::Kind::BadRequest; break;
    }
    fail(kind, description, code);
    lastError_.retryAfterSeconds = reply["parameters"]["retry_after"] | 0;
    log_.warning("Telegram " + lastError_.description);
    return false;
}

bool TelegramClient::parseSent(JsonVariantConst result, Message* sent) {
    if (!sent) return true;
    if (!parseMessage(result, *sent)) return fail(Error::Kind::BadResponse, "couldn't read the sent message");
    return true;
}

// ---------------------------------------------------------------------------
// The bot itself

bool TelegramClient::getMe(BotInfo& info) {
    JsonDocument request, reply;
    request.to<JsonObject>();
    if (!call("getMe", request, reply)) return false;

    JsonVariantConst result = reply["result"];
    if (!result["id"].is<int64_t>()) return fail(Error::Kind::BadResponse, "getMe: no bot id");
    info.id = result["id"].as<int64_t>();
    info.username = result["username"] | "";
    info.firstName = result["first_name"] | "";
    botUsername_ = info.username;
    return true;
}

// ---------------------------------------------------------------------------
// Receiving

bool TelegramClient::getUpdates(std::vector<Update>& updates, uint32_t timeoutSeconds) {
    JsonDocument request, reply;
    if (nextUpdateId_ != 0) request["offset"] = nextUpdateId_;
    request["timeout"] = timeoutSeconds;
    // Only ask for what the box uses. Telegram remembers this setting.
    JsonArray allowed = request["allowed_updates"].to<JsonArray>();
    allowed.add("message");
    allowed.add("callback_query");

    // Leave room beyond Telegram's own wait for the reply to arrive.
    const uint32_t httpTimeoutMs = (timeoutSeconds + 10) * 1000;
    if (!call("getUpdates", request, reply, httpTimeoutMs)) return false;

    JsonArrayConst results = reply["result"].as<JsonArrayConst>();
    if (results.isNull()) return fail(Error::Kind::BadResponse, "getUpdates: result isn't a list");

    for (JsonVariantConst item : results) {
        // Move past every update, even ones that can't be read, so a bad one
        // doesn't come back for ever.
        if (item["update_id"].is<int64_t>()) {
            int64_t id = item["update_id"].as<int64_t>();
            if (id >= nextUpdateId_) nextUpdateId_ = id + 1;
        }
        Update update;
        if (!parseUpdate(item, update)) {
            log_.warning("Skipped an update from Telegram that couldn't be read");
            continue;
        }
        updates.push_back(std::move(update));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Sending text and buttons

bool TelegramClient::sendMessage(ChatId chatId, const std::string& text, const InlineKeyboard* keyboard,
                                 Message* sent) {
    if (text.empty()) return fail(Error::Kind::Local, "sendMessage: empty text");
    if (keyboard && !keyboardIsValid(*keyboard)) return fail(Error::Kind::Local, "sendMessage: bad button");

    JsonDocument request, reply;
    request["chat_id"] = chatId;
    request["text"] = text;
    if (keyboard && !keyboard->empty()) writeKeyboard(request.as<JsonObject>(), *keyboard);
    if (!call("sendMessage", request, reply)) return false;
    return parseSent(reply["result"], sent);
}

bool TelegramClient::editMessageText(ChatId chatId, int64_t messageId, const std::string& text,
                                     const InlineKeyboard* keyboard) {
    if (text.empty()) return fail(Error::Kind::Local, "editMessageText: empty text");
    if (keyboard && !keyboardIsValid(*keyboard)) return fail(Error::Kind::Local, "editMessageText: bad button");

    JsonDocument request, reply;
    request["chat_id"] = chatId;
    request["message_id"] = messageId;
    request["text"] = text;
    if (keyboard && !keyboard->empty()) writeKeyboard(request.as<JsonObject>(), *keyboard);
    return call("editMessageText", request, reply);
}

bool TelegramClient::answerCallbackQuery(const std::string& callbackQueryId, const std::string& text) {
    JsonDocument request, reply;
    request["callback_query_id"] = callbackQueryId;
    if (!text.empty()) request["text"] = text;
    return call("answerCallbackQuery", request, reply);
}

// ---------------------------------------------------------------------------
// Voice notes

bool TelegramClient::sendVoiceFile(ChatId chatId, const std::string& sdPath, int durationSeconds,
                                   const std::string& caption, Message* sent) {
    // The whole recording is read into memory (PSRAM on the device). At the
    // 360 second maximum an MP3 is around 1.5 MB, which fits comfortably.
    std::string audio;
    if (!files_.readFile(sdPath, audio)) return fail(Error::Kind::Local, "sendVoice: can't read " + sdPath);
    if (audio.empty()) return fail(Error::Kind::Local, "sendVoice: empty file " + sdPath);

    MultipartBody body;
    body.addField("chat_id", std::to_string(chatId));
    if (durationSeconds > 0) body.addField("duration", std::to_string(durationSeconds));
    if (!caption.empty()) body.addField("caption", caption);
    const std::string fileName = baseName(sdPath);
    body.addFile("voice", fileName, audioContentType(fileName), audio);
    audio.clear();
    audio.shrink_to_fit();

    HttpRequest http;
    http.method = "POST";
    http.url = methodUrl("sendVoice");
    http.body = body.build();
    http.headers["Content-Type"] = body.contentType();
    http.timeoutMs = 120000;  // uploads can be slow on poor Wi-Fi

    JsonDocument reply;
    if (!send("sendVoice", http, reply)) return false;
    return parseSent(reply["result"], sent);
}

bool TelegramClient::sendVoiceById(ChatId chatId, const std::string& fileId, const std::string& caption,
                                   Message* sent) {
    if (fileId.empty()) return fail(Error::Kind::Local, "sendVoice: no file id");
    JsonDocument request, reply;
    request["chat_id"] = chatId;
    request["voice"] = fileId;
    if (!caption.empty()) request["caption"] = caption;
    if (!call("sendVoice", request, reply)) return false;
    return parseSent(reply["result"], sent);
}

// ---------------------------------------------------------------------------
// Reactions

bool TelegramClient::setReaction(ChatId chatId, int64_t messageId, const std::string& emoji) {
    JsonDocument request, reply;
    request["chat_id"] = chatId;
    request["message_id"] = messageId;
    JsonArray reactions = request["reaction"].to<JsonArray>();
    if (!emoji.empty()) {
        JsonObject r = reactions.add<JsonObject>();
        r["type"] = "emoji";
        r["emoji"] = emoji;
    }
    return call("setMessageReaction", request, reply);
}

// ---------------------------------------------------------------------------
// Files and photos

bool TelegramClient::getFile(const std::string& fileId, FileInfo& info) {
    JsonDocument request, reply;
    request["file_id"] = fileId;
    if (!call("getFile", request, reply)) return false;

    JsonVariantConst result = reply["result"];
    info.fileId = result["file_id"] | fileId.c_str();
    info.filePath = result["file_path"] | "";
    info.fileSize = result["file_size"] | static_cast<int64_t>(0);
    if (info.filePath.empty()) return fail(Error::Kind::BadResponse, "getFile: no file path (file too big?)");
    return true;
}

bool TelegramClient::downloadFile(const std::string& fileId, const std::string& sdPath) {
    FileInfo info;
    if (!getFile(fileId, info)) return false;
    if (info.fileSize > kMaxDownloadBytes) return fail(Error::Kind::Local, "download: file is over 20 MB");

    const std::string partPath = sdPath + ".part";
    if (!files_.writeFile(partPath, "")) return fail(Error::Kind::Local, "download: can't write " + partPath);

    int64_t received = 0;
    bool writeFailed = false;
    HttpRequest http;
    http.url = apiBase_ + "/file/bot" + token_ + "/" + info.filePath;
    http.timeoutMs = 120000;
    http.onBodyChunk = [&](const char* data, size_t length) {
        received += static_cast<int64_t>(length);
        if (received > kMaxDownloadBytes || !files_.appendFile(partPath, std::string(data, length))) {
            writeFailed = true;
            return false;
        }
        return true;
    };

    HttpResponse response = network_.request(http);
    if (response.status == 0 && !writeFailed) {
        files_.removeFile(partPath);
        return fail(Error::Kind::Network, "download: " + response.error);
    }
    if (writeFailed) {
        files_.removeFile(partPath);
        return fail(Error::Kind::Local, "download: couldn't save to the SD card");
    }
    if (!response.ok()) {
        files_.removeFile(partPath);
        Error::Kind kind = response.status >= 500 ? Error::Kind::Server : Error::Kind::BadRequest;
        return fail(kind, "download failed (HTTP " + std::to_string(response.status) + ")", response.status);
    }
    if (info.fileSize > 0 && received != info.fileSize) {
        files_.removeFile(partPath);
        return fail(Error::Kind::Network, "download: only part of the file arrived");
    }
    if (!files_.renameFile(partPath, sdPath)) {
        files_.removeFile(partPath);
        return fail(Error::Kind::Local, "download: couldn't rename to " + sdPath);
    }
    lastError_ = Error();
    return true;
}

bool TelegramClient::getProfilePhoto(UserId userId, PhotoSize& photo, bool& hasPhoto, int targetWidth) {
    hasPhoto = false;
    JsonDocument request, reply;
    request["user_id"] = userId;
    request["limit"] = 1;
    if (!call("getUserProfilePhotos", request, reply)) return false;

    // result.photos is a list of photos, each a list of sizes. The first is the current one.
    JsonVariantConst first = reply["result"]["photos"][0];
    std::vector<PhotoSize> sizes;
    if (!parsePhotoSizes(first, sizes)) return true;  // no photo
    const PhotoSize* best = closestToWidth(sizes, targetWidth);
    if (!best) return true;
    photo = *best;
    hasPhoto = true;
    return true;
}

// ---------------------------------------------------------------------------
// Invite links

bool TelegramClient::isValidStartCode(const std::string& code) {
    if (code.empty() || code.size() > 64) return false;
    for (char c : code) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string TelegramClient::inviteLink(const std::string& code) const {
    if (botUsername_.empty() || !isValidStartCode(code)) return "";
    return "https://t.me/" + botUsername_ + "?start=" + code;
}

}  // namespace telegram
}  // namespace fvb

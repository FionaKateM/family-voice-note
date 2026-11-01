#pragma once

// Pretend Telegram replies for tests. Queue them on a FakeNetwork:
//
//   hw.network.responses.push_back(telegramOk(R"({"id":1,"username":"TestBot"})"));
//
// Nothing here talks to the real Telegram.

#include <string>

#include "hal/Network.h"

namespace fvb {
namespace fakes {

// A token-shaped string that won't trip the secrets check.
constexpr const char* kTestBotToken = "123:TEST-TOKEN";

inline HttpResponse telegramOk(const std::string& resultJson) {
    return HttpResponse{200, R"({"ok":true,"result":)" + resultJson + "}", ""};
}

inline HttpResponse telegramError(int code, const std::string& description, int retryAfter = 0) {
    std::string body = R"({"ok":false,"error_code":)" + std::to_string(code) + R"(,"description":")" +
                       description + "\"";
    if (retryAfter > 0) body += R"(,"parameters":{"retry_after":)" + std::to_string(retryAfter) + "}";
    body += "}";
    return HttpResponse{code, body, ""};
}

inline HttpResponse networkDown() { return HttpResponse{0, "", "Could not resolve host: api.telegram.org"}; }

// The Bot API method a request was for, e.g. "sendMessage".
inline std::string telegramMethod(const HttpRequest& request) {
    size_t slash = request.url.rfind('/');
    return slash == std::string::npos ? request.url : request.url.substr(slash + 1);
}

// ---- Pieces of JSON shaped like what Telegram sends ----

inline std::string userJson(long long id, const std::string& firstName = "Granny") {
    return R"({"id":)" + std::to_string(id) + R"(,"is_bot":false,"first_name":")" + firstName +
           R"(","language_code":"en"})";
}

// A message in a private chat. `content` is the type-specific part, e.g. R"("text":"hi")".
inline std::string messageJson(long long messageId, long long chatId, const std::string& content,
                               const std::string& chatType = "private") {
    return R"({"message_id":)" + std::to_string(messageId) + R"(,"from":)" + userJson(chatId) +
           R"(,"chat":{"id":)" + std::to_string(chatId) + R"(,"type":")" + chatType +
           R"("},"date":1790000000,)" + content + "}";
}

inline std::string textContent(const std::string& text) { return R"("text":")" + text + "\""; }

inline std::string voiceContent(const std::string& fileId, int duration, long long size = 12345) {
    return R"("voice":{"file_id":")" + fileId + R"(","file_unique_id":"u-)" + fileId + R"(","duration":)" +
           std::to_string(duration) + R"(,"mime_type":"audio/ogg","file_size":)" + std::to_string(size) + "}";
}

inline std::string messageUpdate(long long updateId, const std::string& message) {
    return R"({"update_id":)" + std::to_string(updateId) + R"(,"message":)" + message + "}";
}

inline std::string buttonUpdate(long long updateId, long long chatId, long long messageId, const std::string& data) {
    return R"({"update_id":)" + std::to_string(updateId) + R"(,"callback_query":{"id":"cb-)" +
           std::to_string(updateId) + R"(","from":)" + userJson(chatId) + R"(,"message":)" +
           messageJson(messageId, chatId, textContent("Pick one")) + R"(,"chat_instance":"1","data":")" + data +
           "\"}}";
}

}  // namespace fakes
}  // namespace fvb

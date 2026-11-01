// TelegramClient against pretend Telegram replies. Never touches the real Telegram.

#include <ArduinoJson.h>
#include <unity.h>

#include <string>
#include <vector>

#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"
#include "telegram/TelegramClient.h"

using namespace fvb;
using namespace fvb::fakes;
using namespace fvb::telegram;

static FakeHardware* hw;
static TelegramClient* client;

void setUp() {
    hw = new FakeHardware();
    client = new TelegramClient(hw->network, hw->files, hw->log, kTestBotToken);
}

void tearDown() {
    delete client;
    delete hw;
}

static void queue(const HttpResponse& response) { hw->network.responses.push_back(response); }

static const HttpRequest& lastRequest() { return hw->network.requests.back(); }

static JsonDocument lastBody() {
    JsonDocument doc;
    deserializeJson(doc, lastRequest().body);
    return doc;
}

static JsonDocument lastBodyOf(size_t index) {
    JsonDocument doc;
    deserializeJson(doc, hw->network.requests.at(index).body);
    return doc;
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static bool logMentionsToken() { return hw->log.contains(kTestBotToken); }

// ---- getMe and errors ----

void test_get_me_learns_username() {
    queue(telegramOk(R"({"id":42,"is_bot":true,"first_name":"Box","username":"TestBoxBot"})"));
    BotInfo info;
    TEST_ASSERT_TRUE(client->getMe(info));
    TEST_ASSERT_EQUAL(42, info.id);
    TEST_ASSERT_EQUAL_STRING("TestBoxBot", info.username.c_str());
    TEST_ASSERT_EQUAL_STRING("TestBoxBot", client->botUsername().c_str());
    TEST_ASSERT_EQUAL_STRING("https://api.telegram.org/bot123:TEST-TOKEN/getMe", lastRequest().url.c_str());
    TEST_ASSERT_EQUAL_STRING("POST", lastRequest().method.c_str());
    TEST_ASSERT_EQUAL(Error::Kind::None, client->lastError().kind);
}

void test_rejected_token_is_unauthorized_and_token_is_never_shown() {
    queue(telegramError(401, "Unauthorized"));
    BotInfo info;
    TEST_ASSERT_FALSE(client->getMe(info));
    TEST_ASSERT_EQUAL(Error::Kind::Unauthorized, client->lastError().kind);
    TEST_ASSERT_FALSE(client->lastError().isTemporary());
    TEST_ASSERT_FALSE(contains(client->lastError().description, kTestBotToken));
    TEST_ASSERT_FALSE(logMentionsToken());
}

void test_malformed_token_404_is_unauthorized() {
    queue(telegramError(404, "Not Found"));
    BotInfo info;
    TEST_ASSERT_FALSE(client->getMe(info));
    TEST_ASSERT_EQUAL(Error::Kind::Unauthorized, client->lastError().kind);
}

void test_missing_token_fails_without_network() {
    TelegramClient noToken(hw->network, hw->files, hw->log, "");
    BotInfo info;
    TEST_ASSERT_FALSE(noToken.getMe(info));
    TEST_ASSERT_EQUAL(Error::Kind::Unauthorized, noToken.lastError().kind);
    TEST_ASSERT_EQUAL(0, hw->network.requests.size());
}

void test_offline_is_a_temporary_network_error() {
    queue(networkDown());
    BotInfo info;
    TEST_ASSERT_FALSE(client->getMe(info));
    TEST_ASSERT_EQUAL(Error::Kind::Network, client->lastError().kind);
    TEST_ASSERT_TRUE(client->lastError().isTemporary());
}

void test_non_json_reply_from_server_error_is_temporary() {
    queue(HttpResponse{502, "<html>Bad Gateway</html>", ""});
    BotInfo info;
    TEST_ASSERT_FALSE(client->getMe(info));
    TEST_ASSERT_EQUAL(Error::Kind::Server, client->lastError().kind);
    TEST_ASSERT_TRUE(client->lastError().isTemporary());
}

void test_rate_limit_reports_retry_after() {
    queue(telegramError(429, "Too Many Requests: retry after 7", 7));
    TEST_ASSERT_FALSE(client->sendMessage(1, "hi"));
    TEST_ASSERT_EQUAL(Error::Kind::RateLimited, client->lastError().kind);
    TEST_ASSERT_EQUAL(7, client->lastError().retryAfterSeconds);
    TEST_ASSERT_TRUE(client->lastError().isTemporary());
}

void test_blocked_bot_is_forbidden_and_not_temporary() {
    queue(telegramError(403, "Forbidden: bot was blocked by the user"));
    TEST_ASSERT_FALSE(client->sendMessage(1, "hi"));
    TEST_ASSERT_EQUAL(Error::Kind::Forbidden, client->lastError().kind);
    TEST_ASSERT_FALSE(client->lastError().isTemporary());
}

void test_conflict_is_reported() {
    queue(telegramError(409, "Conflict: terminated by other getUpdates request"));
    std::vector<Update> updates;
    TEST_ASSERT_FALSE(client->getUpdates(updates));
    TEST_ASSERT_EQUAL(Error::Kind::Conflict, client->lastError().kind);
}

void test_success_clears_previous_error() {
    queue(networkDown());
    queue(telegramOk(R"({"id":42,"username":"TestBoxBot"})"));
    BotInfo info;
    TEST_ASSERT_FALSE(client->getMe(info));
    TEST_ASSERT_TRUE(client->getMe(info));
    TEST_ASSERT_EQUAL(Error::Kind::None, client->lastError().kind);
}

// ---- getUpdates ----

void test_get_updates_long_polls_and_parses() {
    queue(telegramOk("[" + messageUpdate(100, messageJson(5, 777, textContent("hello"))) + "," +
                     messageUpdate(101, messageJson(6, 777, voiceContent("VOICE1", 4))) + "," +
                     buttonUpdate(102, 777, 9, "yes") + "]"));
    std::vector<Update> updates;
    TEST_ASSERT_TRUE(client->getUpdates(updates));

    JsonDocument body = lastBody();
    TEST_ASSERT_EQUAL(25, body["timeout"].as<int>());
    TEST_ASSERT_TRUE(body["offset"].isNull());  // first call asks for everything
    TEST_ASSERT_EQUAL_STRING("message", body["allowed_updates"][0]);
    TEST_ASSERT_EQUAL_STRING("callback_query", body["allowed_updates"][1]);
    TEST_ASSERT_EQUAL(35000, lastRequest().timeoutMs);

    TEST_ASSERT_EQUAL(3, updates.size());
    TEST_ASSERT_EQUAL(Update::Type::Message, updates[0].type);
    TEST_ASSERT_EQUAL(MessageKind::Text, updates[0].message.kind);
    TEST_ASSERT_EQUAL_STRING("hello", updates[0].message.text.c_str());
    TEST_ASSERT_EQUAL(777, updates[0].message.chatId);
    TEST_ASSERT_EQUAL(MessageKind::Voice, updates[1].message.kind);
    TEST_ASSERT_EQUAL_STRING("VOICE1", updates[1].message.file.fileId.c_str());
    TEST_ASSERT_EQUAL(4, updates[1].message.file.durationSeconds);
    TEST_ASSERT_EQUAL(Update::Type::CallbackQuery, updates[2].type);
    TEST_ASSERT_EQUAL_STRING("yes", updates[2].callbackQuery.data.c_str());
    TEST_ASSERT_EQUAL(9, updates[2].callbackQuery.messageId);
    TEST_ASSERT_EQUAL(777, updates[2].callbackQuery.chatId);

    // The next call confirms everything up to 102.
    TEST_ASSERT_EQUAL(103, client->nextUpdateId());
    queue(telegramOk("[]"));
    updates.clear();
    TEST_ASSERT_TRUE(client->getUpdates(updates));
    TEST_ASSERT_EQUAL(103, lastBody()["offset"].as<long long>());
    TEST_ASSERT_EQUAL(0, updates.size());
}

void test_get_updates_skips_unreadable_update_but_moves_past_it() {
    queue(telegramOk(R"([{"update_id":200,"message":{"message_id":"oops"}},)" +
                     messageUpdate(201, messageJson(1, 5, textContent("fine"))) + "]"));
    std::vector<Update> updates;
    TEST_ASSERT_TRUE(client->getUpdates(updates));
    TEST_ASSERT_EQUAL(1, updates.size());
    TEST_ASSERT_EQUAL(201, updates[0].updateId);
    TEST_ASSERT_EQUAL(202, client->nextUpdateId());
    TEST_ASSERT_TRUE(hw->log.contains("Skipped an update"));
}

void test_get_updates_keeps_other_update_types_as_other() {
    queue(telegramOk(R"([{"update_id":300,"edited_message":{"message_id":1,"chat":{"id":1,"type":"private"}}}])"));
    std::vector<Update> updates;
    TEST_ASSERT_TRUE(client->getUpdates(updates));
    TEST_ASSERT_EQUAL(1, updates.size());
    TEST_ASSERT_EQUAL(Update::Type::Other, updates[0].type);
    TEST_ASSERT_EQUAL(301, client->nextUpdateId());
}

void test_failed_get_updates_does_not_move_offset() {
    client->setNextUpdateId(50);
    queue(networkDown());
    std::vector<Update> updates;
    TEST_ASSERT_FALSE(client->getUpdates(updates));
    TEST_ASSERT_EQUAL(50, client->nextUpdateId());
}

void test_restored_offset_is_sent() {
    client->setNextUpdateId(9000);
    queue(telegramOk("[]"));
    std::vector<Update> updates;
    TEST_ASSERT_TRUE(client->getUpdates(updates, 1));
    TEST_ASSERT_EQUAL(9000, lastBody()["offset"].as<long long>());
    TEST_ASSERT_EQUAL(1, lastBody()["timeout"].as<int>());
    TEST_ASSERT_EQUAL(11000, lastRequest().timeoutMs);
}

void test_ids_bigger_than_32_bits_survive() {
    const long long bigId = 7123456789LL;
    queue(telegramOk("[" + messageUpdate(1, messageJson(1, bigId, textContent("hi"))) + "]"));
    std::vector<Update> updates;
    TEST_ASSERT_TRUE(client->getUpdates(updates));
    TEST_ASSERT_TRUE(updates[0].message.chatId == bigId);
    TEST_ASSERT_TRUE(updates[0].message.from.id == bigId);

    queue(telegramOk(messageJson(2, bigId, textContent("reply"))));
    TEST_ASSERT_TRUE(client->sendMessage(bigId, "reply"));
    TEST_ASSERT_TRUE(lastBody()["chat_id"].as<long long>() == bigId);
}

// ---- Text and buttons ----

void test_send_message_with_buttons() {
    queue(telegramOk(messageJson(77, 5, textContent("Approve?"))));
    InlineKeyboard keyboard = {{{"Yes", "approve:1"}, {"No", "reject:1"}}, {{"Later", "later"}}};
    Message sent;
    TEST_ASSERT_TRUE(client->sendMessage(5, "Approve?", &keyboard, &sent));
    TEST_ASSERT_EQUAL(77, sent.messageId);

    JsonDocument body = lastBody();
    TEST_ASSERT_EQUAL_STRING("sendMessage", telegramMethod(lastRequest()).c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", lastRequest().headers.at("Content-Type").c_str());
    TEST_ASSERT_EQUAL(5, body["chat_id"].as<int>());
    TEST_ASSERT_EQUAL_STRING("Approve?", body["text"]);
    TEST_ASSERT_TRUE(body["parse_mode"].isNull());  // plain text, so names show as typed
    JsonArrayConst rows = body["reply_markup"]["inline_keyboard"];
    TEST_ASSERT_EQUAL(2, rows.size());
    TEST_ASSERT_EQUAL_STRING("No", rows[0][1]["text"]);
    TEST_ASSERT_EQUAL_STRING("reject:1", rows[0][1]["callback_data"]);
    TEST_ASSERT_EQUAL_STRING("later", rows[1][0]["callback_data"]);
}

void test_send_message_without_buttons_has_no_markup() {
    queue(telegramOk(messageJson(1, 5, textContent("hi"))));
    TEST_ASSERT_TRUE(client->sendMessage(5, "hi"));
    TEST_ASSERT_TRUE(lastBody()["reply_markup"].isNull());
}

void test_too_long_button_data_is_refused_before_sending() {
    InlineKeyboard keyboard = {{{"Yes", std::string(65, 'x')}}};
    TEST_ASSERT_FALSE(client->sendMessage(5, "Approve?", &keyboard));
    TEST_ASSERT_EQUAL(Error::Kind::Local, client->lastError().kind);
    TEST_ASSERT_EQUAL(0, hw->network.requests.size());

    InlineKeyboard fits = {{{"Yes", std::string(64, 'x')}}};
    queue(telegramOk(messageJson(1, 5, textContent("Approve?"))));
    TEST_ASSERT_TRUE(client->sendMessage(5, "Approve?", &fits));
}

void test_empty_text_is_refused() {
    TEST_ASSERT_FALSE(client->sendMessage(5, ""));
    TEST_ASSERT_EQUAL(0, hw->network.requests.size());
}

void test_edit_message_text_can_remove_buttons() {
    queue(telegramOk(messageJson(9, 5, textContent("Done"))));
    TEST_ASSERT_TRUE(client->editMessageText(5, 9, "Done"));
    JsonDocument body = lastBody();
    TEST_ASSERT_EQUAL_STRING("editMessageText", telegramMethod(lastRequest()).c_str());
    TEST_ASSERT_EQUAL(9, body["message_id"].as<int>());
    TEST_ASSERT_EQUAL_STRING("Done", body["text"]);
    TEST_ASSERT_TRUE(body["reply_markup"].isNull());
}

void test_answer_callback_query() {
    queue(telegramOk("true"));
    TEST_ASSERT_TRUE(client->answerCallbackQuery("cb-1", "Saved"));
    JsonDocument body = lastBody();
    TEST_ASSERT_EQUAL_STRING("answerCallbackQuery", telegramMethod(lastRequest()).c_str());
    TEST_ASSERT_EQUAL_STRING("cb-1", body["callback_query_id"]);
    TEST_ASSERT_EQUAL_STRING("Saved", body["text"]);
}

// ---- Voice notes ----

void test_send_voice_file_uploads_with_duration() {
    const std::string audio = std::string("ID3\x00\x01\x02 fake mp3 bytes", 23);
    hw->files.files["/outbox/0001.mp3"] = audio;
    queue(telegramOk(messageJson(88, 5, voiceContent("SENT-VOICE", 12))));

    Message sent;
    TEST_ASSERT_TRUE(client->sendVoiceFile(5, "/outbox/0001.mp3", 12, "", &sent));
    TEST_ASSERT_EQUAL_STRING("SENT-VOICE", sent.file.fileId.c_str());
    TEST_ASSERT_EQUAL(88, sent.messageId);

    const HttpRequest& req = lastRequest();
    TEST_ASSERT_EQUAL_STRING("sendVoice", telegramMethod(req).c_str());
    const std::string contentType = req.headers.at("Content-Type");
    TEST_ASSERT_TRUE(contains(contentType, "multipart/form-data; boundary="));
    const std::string boundary = contentType.substr(contentType.find('=') + 1);
    TEST_ASSERT_TRUE(contains(req.body, "--" + boundary + "\r\n"));
    TEST_ASSERT_TRUE(contains(req.body, "--" + boundary + "--\r\n"));
    TEST_ASSERT_TRUE(contains(req.body, "name=\"chat_id\"\r\n\r\n5\r\n"));
    TEST_ASSERT_TRUE(contains(req.body, "name=\"duration\"\r\n\r\n12\r\n"));
    TEST_ASSERT_TRUE(contains(req.body, "name=\"voice\"; filename=\"0001.mp3\"\r\nContent-Type: audio/mpeg\r\n"));
    TEST_ASSERT_TRUE(contains(req.body, audio));  // the bytes, including the zero byte
    TEST_ASSERT_FALSE(contains(req.body, "caption"));
    TEST_ASSERT_EQUAL(120000, req.timeoutMs);
}

void test_send_voice_file_with_caption() {
    hw->files.files["/a.ogg"] = "opus";
    queue(telegramOk(messageJson(1, 5, voiceContent("V", 3))));
    TEST_ASSERT_TRUE(client->sendVoiceFile(5, "/a.ogg", 3, "Child to Granny, 14:32"));
    TEST_ASSERT_TRUE(contains(lastRequest().body, "name=\"caption\"\r\n\r\nChild to Granny, 14:32\r\n"));
    TEST_ASSERT_TRUE(contains(lastRequest().body, "Content-Type: audio/ogg"));
}

void test_send_voice_file_missing_file_is_local_error() {
    TEST_ASSERT_FALSE(client->sendVoiceFile(5, "/outbox/missing.mp3", 3));
    TEST_ASSERT_EQUAL(Error::Kind::Local, client->lastError().kind);
    TEST_ASSERT_EQUAL(0, hw->network.requests.size());
}

void test_send_voice_by_id_reuses_telegram_copy() {
    queue(telegramOk(messageJson(2, 9, voiceContent("SENT-VOICE", 12))));
    TEST_ASSERT_TRUE(client->sendVoiceById(9, "SENT-VOICE", "Child to Granny, 14:32"));
    JsonDocument body = lastBody();
    TEST_ASSERT_EQUAL_STRING("sendVoice", telegramMethod(lastRequest()).c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", lastRequest().headers.at("Content-Type").c_str());
    TEST_ASSERT_EQUAL_STRING("SENT-VOICE", body["voice"]);
    TEST_ASSERT_EQUAL_STRING("Child to Granny, 14:32", body["caption"]);
}

// ---- Reactions ----

void test_set_reaction_eyes() {
    queue(telegramOk("true"));
    TEST_ASSERT_TRUE(client->setReaction(5, 33, TelegramClient::kListenedReaction));
    JsonDocument body = lastBody();
    TEST_ASSERT_EQUAL_STRING("setMessageReaction", telegramMethod(lastRequest()).c_str());
    TEST_ASSERT_EQUAL(33, body["message_id"].as<int>());
    TEST_ASSERT_EQUAL_STRING("emoji", body["reaction"][0]["type"]);
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x91\x80", body["reaction"][0]["emoji"]);
}

void test_empty_reaction_clears_it() {
    queue(telegramOk("true"));
    TEST_ASSERT_TRUE(client->setReaction(5, 33, ""));
    JsonDocument body = lastBody();
    TEST_ASSERT_TRUE(body["reaction"].is<JsonArray>());
    TEST_ASSERT_EQUAL(0, body["reaction"].size());
}

// ---- Downloads ----

void test_download_file_saves_to_sd_card() {
    const std::string audio = "OggS pretend opus data that is a bit longer than one chunk";
    queue(telegramOk(R"({"file_id":"F1","file_unique_id":"U1","file_size":)" + std::to_string(audio.size()) +
                     R"(,"file_path":"voice/file_7.oga"})"));
    queue(HttpResponse{200, audio, ""});

    TEST_ASSERT_TRUE(client->downloadFile("F1", "/contacts/1/in/0001.ogg"));
    TEST_ASSERT_EQUAL_STRING(audio.c_str(), hw->files.files["/contacts/1/in/0001.ogg"].c_str());
    TEST_ASSERT_FALSE(hw->files.exists("/contacts/1/in/0001.ogg.part"));

    TEST_ASSERT_EQUAL(2, hw->network.requests.size());
    TEST_ASSERT_EQUAL_STRING("F1", lastBodyOf(0)["file_id"]);
    TEST_ASSERT_EQUAL_STRING("https://api.telegram.org/file/bot123:TEST-TOKEN/voice/file_7.oga",
                             lastRequest().url.c_str());
    TEST_ASSERT_EQUAL_STRING("GET", lastRequest().method.c_str());
}

void test_download_with_missing_bytes_leaves_nothing() {
    queue(telegramOk(R"({"file_id":"F1","file_size":100,"file_path":"voice/a.oga"})"));
    queue(HttpResponse{200, "only part", ""});
    TEST_ASSERT_FALSE(client->downloadFile("F1", "/in/a.ogg"));
    TEST_ASSERT_EQUAL(Error::Kind::Network, client->lastError().kind);
    TEST_ASSERT_TRUE(hw->files.files.empty());
}

void test_download_http_error_leaves_nothing() {
    queue(telegramOk(R"({"file_id":"F1","file_path":"voice/a.oga"})"));
    queue(HttpResponse{404, R"({"ok":false,"error_code":404,"description":"Not Found"})", ""});
    TEST_ASSERT_FALSE(client->downloadFile("F1", "/in/a.ogg"));
    TEST_ASSERT_EQUAL(Error::Kind::BadRequest, client->lastError().kind);
    TEST_ASSERT_TRUE(hw->files.files.empty());
    TEST_ASSERT_FALSE(contains(client->lastError().description, kTestBotToken));
}

void test_download_network_drop_leaves_nothing() {
    queue(telegramOk(R"({"file_id":"F1","file_path":"voice/a.oga"})"));
    queue(networkDown());
    TEST_ASSERT_FALSE(client->downloadFile("F1", "/in/a.ogg"));
    TEST_ASSERT_EQUAL(Error::Kind::Network, client->lastError().kind);
    TEST_ASSERT_TRUE(hw->files.files.empty());
}

void test_download_over_20mb_is_refused_before_downloading() {
    queue(telegramOk(R"({"file_id":"F1","file_size":30000000,"file_path":"voice/a.oga"})"));
    TEST_ASSERT_FALSE(client->downloadFile("F1", "/in/a.ogg"));
    TEST_ASSERT_EQUAL(Error::Kind::Local, client->lastError().kind);
    TEST_ASSERT_EQUAL(1, hw->network.requests.size());
}

void test_get_file_without_path_is_an_error() {
    queue(telegramOk(R"({"file_id":"F1"})"));
    FileInfo info;
    TEST_ASSERT_FALSE(client->getFile("F1", info));
    TEST_ASSERT_EQUAL(Error::Kind::BadResponse, client->lastError().kind);
}

// ---- Profile photos ----

void test_profile_photo_picks_size_closest_to_320() {
    queue(telegramOk(R"({"total_count":2,"photos":[[
        {"file_id":"S","file_unique_id":"s","width":160,"height":160},
        {"file_id":"M","file_unique_id":"m","width":320,"height":320},
        {"file_id":"L","file_unique_id":"l","width":640,"height":640}],
        [{"file_id":"OLD","file_unique_id":"o","width":320,"height":320}]]})"));
    PhotoSize photo;
    bool hasPhoto = false;
    TEST_ASSERT_TRUE(client->getProfilePhoto(5, photo, hasPhoto));
    TEST_ASSERT_TRUE(hasPhoto);
    TEST_ASSERT_EQUAL_STRING("M", photo.fileId.c_str());
    JsonDocument body = lastBody();
    TEST_ASSERT_EQUAL_STRING("getUserProfilePhotos", telegramMethod(lastRequest()).c_str());
    TEST_ASSERT_EQUAL(5, body["user_id"].as<int>());
    TEST_ASSERT_EQUAL(1, body["limit"].as<int>());
}

void test_no_profile_photo() {
    queue(telegramOk(R"({"total_count":0,"photos":[]})"));
    PhotoSize photo;
    bool hasPhoto = true;
    TEST_ASSERT_TRUE(client->getProfilePhoto(5, photo, hasPhoto));
    TEST_ASSERT_FALSE(hasPhoto);
}

// ---- Invite links ----

void test_invite_link_needs_username_and_valid_code() {
    TEST_ASSERT_EQUAL_STRING("", client->inviteLink("abc123").c_str());

    queue(telegramOk(R"({"id":42,"username":"TestBoxBot"})"));
    BotInfo info;
    TEST_ASSERT_TRUE(client->getMe(info));
    TEST_ASSERT_EQUAL_STRING("https://t.me/TestBoxBot?start=abc_1-2", client->inviteLink("abc_1-2").c_str());
    TEST_ASSERT_EQUAL_STRING("", client->inviteLink("has space").c_str());
}

void test_start_code_rules() {
    TEST_ASSERT_TRUE(TelegramClient::isValidStartCode("a"));
    TEST_ASSERT_TRUE(TelegramClient::isValidStartCode(std::string(64, 'Z')));
    TEST_ASSERT_TRUE(TelegramClient::isValidStartCode("Ab9_-"));
    TEST_ASSERT_FALSE(TelegramClient::isValidStartCode(""));
    TEST_ASSERT_FALSE(TelegramClient::isValidStartCode(std::string(65, 'a')));
    TEST_ASSERT_FALSE(TelegramClient::isValidStartCode("a=b"));
    TEST_ASSERT_FALSE(TelegramClient::isValidStartCode("a/b"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_get_me_learns_username);
    RUN_TEST(test_rejected_token_is_unauthorized_and_token_is_never_shown);
    RUN_TEST(test_malformed_token_404_is_unauthorized);
    RUN_TEST(test_missing_token_fails_without_network);
    RUN_TEST(test_offline_is_a_temporary_network_error);
    RUN_TEST(test_non_json_reply_from_server_error_is_temporary);
    RUN_TEST(test_rate_limit_reports_retry_after);
    RUN_TEST(test_blocked_bot_is_forbidden_and_not_temporary);
    RUN_TEST(test_conflict_is_reported);
    RUN_TEST(test_success_clears_previous_error);
    RUN_TEST(test_get_updates_long_polls_and_parses);
    RUN_TEST(test_get_updates_skips_unreadable_update_but_moves_past_it);
    RUN_TEST(test_get_updates_keeps_other_update_types_as_other);
    RUN_TEST(test_failed_get_updates_does_not_move_offset);
    RUN_TEST(test_restored_offset_is_sent);
    RUN_TEST(test_ids_bigger_than_32_bits_survive);
    RUN_TEST(test_send_message_with_buttons);
    RUN_TEST(test_send_message_without_buttons_has_no_markup);
    RUN_TEST(test_too_long_button_data_is_refused_before_sending);
    RUN_TEST(test_empty_text_is_refused);
    RUN_TEST(test_edit_message_text_can_remove_buttons);
    RUN_TEST(test_answer_callback_query);
    RUN_TEST(test_send_voice_file_uploads_with_duration);
    RUN_TEST(test_send_voice_file_with_caption);
    RUN_TEST(test_send_voice_file_missing_file_is_local_error);
    RUN_TEST(test_send_voice_by_id_reuses_telegram_copy);
    RUN_TEST(test_set_reaction_eyes);
    RUN_TEST(test_empty_reaction_clears_it);
    RUN_TEST(test_download_file_saves_to_sd_card);
    RUN_TEST(test_download_with_missing_bytes_leaves_nothing);
    RUN_TEST(test_download_http_error_leaves_nothing);
    RUN_TEST(test_download_network_drop_leaves_nothing);
    RUN_TEST(test_download_over_20mb_is_refused_before_downloading);
    RUN_TEST(test_get_file_without_path_is_an_error);
    RUN_TEST(test_profile_photo_picks_size_closest_to_320);
    RUN_TEST(test_no_profile_photo);
    RUN_TEST(test_invite_link_needs_username_and_valid_code);
    RUN_TEST(test_start_code_rules);
    return UNITY_END();
}

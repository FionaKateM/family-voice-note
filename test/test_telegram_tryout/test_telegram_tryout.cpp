// The laptop try-out bot, against pretend Telegram replies.

#include <ArduinoJson.h>
#include <unity.h>

#include <string>
#include <vector>

#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"
#include "telegram/TryOut.h"

using namespace fvb;
using namespace fvb::fakes;
using namespace fvb::telegram;

static const long long kTester = 1001;
static const long long kStranger = 2002;

static FakeHardware* hw;
static TelegramClient* client;
static TryOut* tryOut;

void setUp() {
    hw = new FakeHardware();
    client = new TelegramClient(hw->network, hw->files, hw->log, kTestBotToken);
    tryOut = new TryOut(*client, hw->log, "JOINCODE");
}

void tearDown() {
    delete tryOut;
    delete client;
    delete hw;
}

static void queue(const HttpResponse& response) { hw->network.responses.push_back(response); }

static std::vector<std::string> methodsCalled() {
    std::vector<std::string> methods;
    for (const auto& r : hw->network.requests) methods.push_back(telegramMethod(r));
    return methods;
}

static JsonDocument bodyOf(size_t index) {
    JsonDocument doc;
    deserializeJson(doc, hw->network.requests.at(index).body);
    return doc;
}

// Polls once with the given updates and lets the try-out answer them. Replies
// queued before calling this are used for whatever the try-out sends after the poll.
static void deliver(const std::string& updatesJson) {
    hw->network.requests.clear();
    hw->network.responses.push_front(telegramOk("[" + updatesJson + "]"));
    TEST_ASSERT_EQUAL(0, tryOut->step());
}

static void startAndJoin() {
    queue(telegramOk(R"({"id":42,"username":"TestBoxBot"})"));
    TEST_ASSERT_TRUE(tryOut->start());
    queue(telegramOk(messageJson(1, kTester, textContent("ok"))));  // welcome message
    deliver(messageUpdate(1, messageJson(1, kTester, textContent("/start JOINCODE"))));
    TEST_ASSERT_TRUE(tryOut->hasJoined());
    hw->network.requests.clear();
}

void test_start_prints_invite_link() {
    queue(telegramOk(R"({"id":42,"username":"TestBoxBot"})"));
    TEST_ASSERT_TRUE(tryOut->start());
    TEST_ASSERT_EQUAL_STRING("https://t.me/TestBoxBot?start=JOINCODE", tryOut->link().c_str());
    TEST_ASSERT_TRUE(hw->log.contains("https://t.me/TestBoxBot?start=JOINCODE"));
}

void test_start_fails_with_bad_token() {
    queue(telegramError(401, "Unauthorized"));
    TEST_ASSERT_FALSE(tryOut->start());
    TEST_ASSERT_FALSE(hw->log.contains(kTestBotToken));
}

void test_ignores_everyone_until_the_link_is_used() {
    queue(telegramOk(R"({"id":42,"username":"TestBoxBot"})"));
    TEST_ASSERT_TRUE(tryOut->start());

    deliver(messageUpdate(1, messageJson(1, kStranger, textContent("hello"))) + "," +
            messageUpdate(2, messageJson(2, kStranger, textContent("/start WRONG"))) + "," +
            messageUpdate(3, messageJson(3, kStranger, textContent("/start JOINCODE"), "group")));
    TEST_ASSERT_FALSE(tryOut->hasJoined());
    TEST_ASSERT_EQUAL(1, hw->network.requests.size());  // just the poll, no replies
}

void test_joining_sends_welcome() {
    startAndJoin();
    // startAndJoin checked hasJoined; the reply went to the person who joined.
    queue(telegramOk(messageJson(5, kTester, textContent("ok"))));
    deliver(messageUpdate(2, messageJson(2, kTester, textContent("what?"))));
    TEST_ASSERT_EQUAL(2, hw->network.requests.size());
    TEST_ASSERT_EQUAL_STRING("sendMessage", telegramMethod(hw->network.requests[1]).c_str());
    TEST_ASSERT_TRUE(bodyOf(1)["chat_id"].as<long long>() == kTester);
    TEST_ASSERT_TRUE(std::string(bodyOf(1)["text"].as<const char*>()).find("Got a text") == 0);
}

void test_strangers_still_ignored_after_join() {
    startAndJoin();
    deliver(messageUpdate(2, messageJson(2, kStranger, textContent("/start JOINCODE"))) + "," +
            buttonUpdate(3, kStranger, 9, "try:yes"));
    TEST_ASSERT_EQUAL(1, hw->network.requests.size());
}

void test_buttons_round_trip() {
    startAndJoin();
    queue(telegramOk(messageJson(50, kTester, textContent("Do the buttons work?"))));
    deliver(messageUpdate(2, messageJson(2, kTester, textContent("/buttons"))));
    JsonDocument shown = bodyOf(1);
    TEST_ASSERT_EQUAL_STRING("try:yes", shown["reply_markup"]["inline_keyboard"][0][0]["callback_data"]);

    queue(telegramOk("true"));
    queue(telegramOk(messageJson(50, kTester, textContent("You pressed Yes."))));
    deliver(buttonUpdate(3, kTester, 50, "try:yes"));
    std::vector<std::string> expected = {"getUpdates", "answerCallbackQuery", "editMessageText"};
    TEST_ASSERT_TRUE(methodsCalled() == expected);
    TEST_ASSERT_EQUAL_STRING("You pressed Yes.", bodyOf(2)["text"]);
    TEST_ASSERT_EQUAL(50, bodyOf(2)["message_id"].as<int>());
}

void test_voice_note_is_saved_reacted_to_and_sent_back_twice() {
    startAndJoin();
    queue(telegramOk(R"({"file_id":"IN","file_size":4,"file_path":"voice/x.oga"})"));
    queue(HttpResponse{200, "OggS", ""});
    queue(telegramOk("true"));
    queue(telegramOk(messageJson(61, kTester, voiceContent("UPLOADED", 6))));
    queue(telegramOk(messageJson(62, kTester, voiceContent("UPLOADED", 6))));

    deliver(messageUpdate(2, messageJson(60, kTester, voiceContent("IN", 6, 4))));

    std::vector<std::string> expected = {"getUpdates", "getFile", "x.oga", "setMessageReaction", "sendVoice",
                                         "sendVoice"};
    TEST_ASSERT_TRUE(methodsCalled() == expected);
    TEST_ASSERT_EQUAL_STRING("OggS", hw->files.files["/tryout/voice-60.ogg"].c_str());
    TEST_ASSERT_EQUAL(60, bodyOf(3)["message_id"].as<int>());
    TEST_ASSERT_TRUE(hw->network.requests[4].body.find("name=\"duration\"\r\n\r\n6\r\n") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("UPLOADED", bodyOf(5)["voice"]);
}

void test_photo_command_saves_profile_photo() {
    startAndJoin();
    queue(telegramOk(R"({"total_count":1,"photos":[[{"file_id":"P","file_unique_id":"p","width":320,"height":320}]]})"));
    queue(telegramOk(R"({"file_id":"P","file_path":"photos/p.jpg"})"));
    queue(HttpResponse{200, "JPEG", ""});
    queue(telegramOk(messageJson(70, kTester, textContent("Saved"))));

    deliver(messageUpdate(2, messageJson(2, kTester, textContent("/photo"))));
    TEST_ASSERT_EQUAL_STRING("JPEG", hw->files.files["/tryout/profile.jpg"].c_str());
    TEST_ASSERT_TRUE(bodyOf(1)["user_id"].as<long long>() == kTester);
}

void test_step_waits_after_errors() {
    queue(networkDown());
    TEST_ASSERT_EQUAL(5000, tryOut->step());
    queue(telegramError(429, "Too Many Requests", 3));
    TEST_ASSERT_EQUAL(3000, tryOut->step());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_start_prints_invite_link);
    RUN_TEST(test_start_fails_with_bad_token);
    RUN_TEST(test_ignores_everyone_until_the_link_is_used);
    RUN_TEST(test_joining_sends_welcome);
    RUN_TEST(test_strangers_still_ignored_after_join);
    RUN_TEST(test_buttons_round_trip);
    RUN_TEST(test_voice_note_is_saved_reacted_to_and_sent_back_twice);
    RUN_TEST(test_photo_command_saves_profile_photo);
    RUN_TEST(test_step_waits_after_errors);
    return UNITY_END();
}

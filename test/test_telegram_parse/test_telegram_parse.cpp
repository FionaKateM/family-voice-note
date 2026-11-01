// Reading Telegram's JSON, commands, photo sizes and upload bodies.

#include <ArduinoJson.h>
#include <unity.h>

#include <string>

#include "fakes/FakeTelegram.h"
#include "telegram/Multipart.h"
#include "telegram/Parse.h"

using namespace fvb::fakes;
using namespace fvb::telegram;

void setUp() {}
void tearDown() {}

static Message parse(const std::string& content, const std::string& chatType = "private") {
    JsonDocument doc;
    deserializeJson(doc, messageJson(10, 20, content, chatType));
    Message m;
    TEST_ASSERT_TRUE(parseMessage(doc.as<JsonVariantConst>(), m));
    return m;
}

// ---- Message kinds ----

void test_text_message() {
    Message m = parse(textContent("hello"));
    TEST_ASSERT_EQUAL(MessageKind::Text, m.kind);
    TEST_ASSERT_EQUAL_STRING("hello", m.text.c_str());
    TEST_ASSERT_EQUAL(10, m.messageId);
    TEST_ASSERT_EQUAL(20, m.chatId);
    TEST_ASSERT_EQUAL(20, m.from.id);
    TEST_ASSERT_EQUAL_STRING("Granny", m.from.firstName.c_str());
    TEST_ASSERT_TRUE(m.isPrivateChat());
}

void test_group_chat_is_not_private() {
    Message m = parse(textContent("hello"), "group");
    TEST_ASSERT_FALSE(m.isPrivateChat());
}

void test_voice_message() {
    Message m = parse(voiceContent("V1", 17, 4096));
    TEST_ASSERT_EQUAL(MessageKind::Voice, m.kind);
    TEST_ASSERT_EQUAL_STRING("V1", m.file.fileId.c_str());
    TEST_ASSERT_EQUAL_STRING("u-V1", m.file.fileUniqueId.c_str());
    TEST_ASSERT_EQUAL(17, m.file.durationSeconds);
    TEST_ASSERT_EQUAL(4096, m.file.fileSize);
    TEST_ASSERT_EQUAL_STRING("audio/ogg", m.file.mimeType.c_str());
}

void test_photo_message_keeps_all_sizes_and_caption() {
    Message m = parse(R"("photo":[{"file_id":"a","file_unique_id":"a","width":90,"height":90},
                                  {"file_id":"b","file_unique_id":"b","width":1280,"height":960}],
                       "caption":"me")");
    TEST_ASSERT_EQUAL(MessageKind::Photo, m.kind);
    TEST_ASSERT_EQUAL(2, m.photos.size());
    TEST_ASSERT_EQUAL(1280, m.photos[1].width);
    TEST_ASSERT_EQUAL_STRING("me", m.caption.c_str());
}

void test_image_sent_as_file() {
    Message m = parse(R"("document":{"file_id":"d","file_unique_id":"d","mime_type":"image/jpeg","file_name":"me.jpg"})");
    TEST_ASSERT_EQUAL(MessageKind::PhotoAsFile, m.kind);
    TEST_ASSERT_EQUAL_STRING("me.jpg", m.file.fileName.c_str());
}

void test_other_document() {
    Message m = parse(R"("document":{"file_id":"d","file_unique_id":"d","mime_type":"application/pdf"})");
    TEST_ASSERT_EQUAL(MessageKind::Document, m.kind);
}

void test_gif_is_animation_not_document() {
    // Telegram sends both "animation" and "document" for GIFs.
    Message m = parse(R"("animation":{"file_id":"g","file_unique_id":"g","mime_type":"video/mp4"},
                         "document":{"file_id":"g","file_unique_id":"g","mime_type":"video/mp4"})");
    TEST_ASSERT_EQUAL(MessageKind::Animation, m.kind);
}

void test_sticker_video_round_video_and_music() {
    TEST_ASSERT_EQUAL(MessageKind::Sticker, parse(R"("sticker":{"file_id":"s","file_unique_id":"s"})").kind);
    TEST_ASSERT_EQUAL(MessageKind::Video, parse(R"("video":{"file_id":"v","file_unique_id":"v","duration":3})").kind);
    TEST_ASSERT_EQUAL(MessageKind::VideoNote,
                      parse(R"("video_note":{"file_id":"n","file_unique_id":"n","duration":3})").kind);
    TEST_ASSERT_EQUAL(MessageKind::Audio, parse(R"("audio":{"file_id":"m","file_unique_id":"m","duration":3})").kind);
}

void test_unknown_content_is_other() {
    TEST_ASSERT_EQUAL(MessageKind::Other, parse(R"("location":{"latitude":1,"longitude":2})").kind);
}

void test_message_without_ids_is_rejected() {
    JsonDocument doc;
    deserializeJson(doc, R"({"text":"hi"})");
    Message m;
    TEST_ASSERT_FALSE(parseMessage(doc.as<JsonVariantConst>(), m));
}

void test_button_press() {
    JsonDocument doc;
    deserializeJson(doc, buttonUpdate(5, 20, 99, "approve:3"));
    Update u;
    TEST_ASSERT_TRUE(parseUpdate(doc.as<JsonVariantConst>(), u));
    TEST_ASSERT_EQUAL(Update::Type::CallbackQuery, u.type);
    TEST_ASSERT_EQUAL_STRING("cb-5", u.callbackQuery.id.c_str());
    TEST_ASSERT_EQUAL_STRING("approve:3", u.callbackQuery.data.c_str());
    TEST_ASSERT_EQUAL(20, u.callbackQuery.from.id);
    TEST_ASSERT_EQUAL(20, u.callbackQuery.chatId);
    TEST_ASSERT_EQUAL(99, u.callbackQuery.messageId);
}

// ---- Commands ----

void test_parse_command() {
    std::string command, argument;
    TEST_ASSERT_TRUE(parseCommand("/start abc123", command, argument));
    TEST_ASSERT_EQUAL_STRING("start", command.c_str());
    TEST_ASSERT_EQUAL_STRING("abc123", argument.c_str());

    TEST_ASSERT_TRUE(parseCommand("/Settings@TestBoxBot", command, argument));
    TEST_ASSERT_EQUAL_STRING("settings", command.c_str());
    TEST_ASSERT_EQUAL_STRING("", argument.c_str());

    TEST_ASSERT_TRUE(parseCommand("/trash   getlast  ", command, argument));
    TEST_ASSERT_EQUAL_STRING("trash", command.c_str());
    TEST_ASSERT_EQUAL_STRING("getlast", argument.c_str());

    TEST_ASSERT_FALSE(parseCommand("hello", command, argument));
    TEST_ASSERT_FALSE(parseCommand("/", command, argument));
    TEST_ASSERT_FALSE(parseCommand("/@bot", command, argument));
    TEST_ASSERT_FALSE(parseCommand("", command, argument));
}

// ---- Photo sizes ----

void test_closest_to_width() {
    std::vector<PhotoSize> sizes = {{"a", "", 90, 90, 0}, {"b", "", 300, 300, 0}, {"c", "", 800, 800, 0}};
    TEST_ASSERT_EQUAL_STRING("b", closestToWidth(sizes, 320)->fileId.c_str());
    TEST_ASSERT_EQUAL_STRING("c", closestToWidth(sizes, 700)->fileId.c_str());
    TEST_ASSERT_NULL(closestToWidth({}, 320));
}

void test_closest_to_width_tie_prefers_larger() {
    std::vector<PhotoSize> sizes = {{"small", "", 300, 300, 0}, {"big", "", 340, 340, 0}};
    TEST_ASSERT_EQUAL_STRING("big", closestToWidth(sizes, 320)->fileId.c_str());
}

// ---- Upload bodies ----

void test_multipart_body_layout() {
    MultipartBody body;
    body.addField("chat_id", "5");
    body.addFile("voice", "a.mp3", "audio/mpeg", "DATA");
    std::string built = body.build();
    TEST_ASSERT_EQUAL_STRING("multipart/form-data; boundary=fvb-boundary-0", body.contentType().c_str());
    TEST_ASSERT_EQUAL_STRING(
        "--fvb-boundary-0\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n5\r\n"
        "--fvb-boundary-0\r\n"
        "Content-Disposition: form-data; name=\"voice\"; filename=\"a.mp3\"\r\n"
        "Content-Type: audio/mpeg\r\n\r\nDATA\r\n"
        "--fvb-boundary-0--\r\n",
        built.c_str());
}

void test_multipart_boundary_avoids_file_contents() {
    MultipartBody body;
    body.addFile("voice", "a.mp3", "audio/mpeg", "xx fvb-boundary-0 fvb-boundary-1 xx");
    body.build();
    TEST_ASSERT_EQUAL_STRING("multipart/form-data; boundary=fvb-boundary-2", body.contentType().c_str());
}

void test_audio_content_types() {
    TEST_ASSERT_EQUAL_STRING("audio/mpeg", audioContentType("0001.mp3").c_str());
    TEST_ASSERT_EQUAL_STRING("audio/mpeg", audioContentType("X.MP3").c_str());
    TEST_ASSERT_EQUAL_STRING("audio/ogg", audioContentType("a.ogg").c_str());
    TEST_ASSERT_EQUAL_STRING("audio/ogg", audioContentType("a.oga").c_str());
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", audioContentType("noext").c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_text_message);
    RUN_TEST(test_group_chat_is_not_private);
    RUN_TEST(test_voice_message);
    RUN_TEST(test_photo_message_keeps_all_sizes_and_caption);
    RUN_TEST(test_image_sent_as_file);
    RUN_TEST(test_other_document);
    RUN_TEST(test_gif_is_animation_not_document);
    RUN_TEST(test_sticker_video_round_video_and_music);
    RUN_TEST(test_unknown_content_is_other);
    RUN_TEST(test_message_without_ids_is_rejected);
    RUN_TEST(test_button_press);
    RUN_TEST(test_parse_command);
    RUN_TEST(test_closest_to_width);
    RUN_TEST(test_closest_to_width_tie_prefers_larger);
    RUN_TEST(test_multipart_body_layout);
    RUN_TEST(test_multipart_boundary_avoids_file_contents);
    RUN_TEST(test_audio_content_types);
    return UNITY_END();
}

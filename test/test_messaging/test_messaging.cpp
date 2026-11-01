// Voice notes in and out, the outbox with retries, the 👀 reaction and monitor
// copies (ticket 1.7), against a pretend Telegram.

#include <ArduinoJson.h>
#include <unity.h>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/bot/Bot.h"
#include "app/invites/InviteFlows.h"
#include "app/messages/Messaging.h"
#include "app/storage/Paths.h"
#include "fakes/FakeRandom.h"
#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;
using namespace fvb::telegram;

static const long long kAdmin = 1001;
static const long long kOtherAdmin = 1002;
static const long long kGranny = 5005;
static const long long kGrandad = 6006;
static const long long kMonitor = 7007;
static const int64_t kNow = 1790000000;  // 2026-09-21 14:13:20 UTC, 15:13 in London

// Answers each Bot API method the way Telegram would. Put a reply in `forced`
// to make the next call to that method fail (or answer differently).
class PretendTelegram : public Network {
public:
    bool isConnected() const override { return true; }
    int8_t signalStrength() const override { return -60; }

    HttpResponse request(const HttpRequest& req) override {
        requests.push_back(req);
        const bool isDownload = req.url.find("/file/bot") != std::string::npos;
        const std::string method = isDownload ? "download" : telegramMethod(req);
        auto f = forced.find(method);
        if (f != forced.end() && !f->second.empty()) {
            HttpResponse r = f->second.front();
            f->second.pop_front();
            return r;
        }
        if (isDownload) {
            const std::string data = "OGG-AUDIO";
            if (req.onBodyChunk) req.onBodyChunk(data.data(), data.size());
            return HttpResponse{200, "", ""};
        }
        if (method == "getUpdates") {
            if (batches.empty()) return telegramOk("[]");
            std::string batch = batches.front();
            batches.pop_front();
            return telegramOk("[" + batch + "]");
        }
        if (method == "getMe") return telegramOk(R"({"id":42,"is_bot":true,"first_name":"Box","username":"TestBoxBot"})");
        if (method == "sendMessage") {
            return telegramOk(R"({"message_id":)" + std::to_string(++lastMessageId) +
                              R"(,"chat":{"id":1,"type":"private"},"date":1790000000,"text":"x"})");
        }
        if (method == "sendVoice") {
            ++lastMessageId;
            return telegramOk(R"({"message_id":)" + std::to_string(lastMessageId) +
                              R"(,"chat":{"id":1,"type":"private"},"date":1790000000,"voice":{"file_id":"sent-file-)" +
                              std::to_string(lastMessageId) + R"(","file_unique_id":"u","duration":3}})");
        }
        if (method == "setMessageReaction") return telegramOk("true");
        if (method == "getFile") return telegramOk(R"({"file_id":"f","file_unique_id":"u","file_size":9,"file_path":"voice/file_1.oga"})");
        if (method == "answerCallbackQuery" || method == "editMessageText") return telegramOk("true");
        return telegramError(400, "Bad Request: not pretended");
    }

    std::vector<HttpRequest> requests;
    std::deque<std::string> batches;
    std::map<std::string, std::deque<HttpResponse>> forced;
    long long lastMessageId = 900;
};

static FakeHardware* hw;
static PretendTelegram* net;
static FakeRandom* rng;
static std::unique_ptr<Storage> storage;
static std::unique_ptr<TelegramClient> botClient;
static std::unique_ptr<TelegramClient> sendClient;
static std::unique_ptr<Bot> bot;
static std::unique_ptr<InviteFlows> flows;
static std::unique_ptr<Messaging> messaging;
static long long nextUpdate = 100;
static uint32_t granny = 0;
static uint32_t grandad = 0;

// A new bot and messaging on the same storage, as after a restart.
static void restart() {
    messaging.reset();
    flows.reset();
    bot.reset();
    botClient.reset(new TelegramClient(*net, hw->files, hw->log, kTestBotToken));
    sendClient.reset(new TelegramClient(*net, hw->files, hw->log, kTestBotToken));
    bot.reset(new Bot(*botClient, *storage, hw->settings, hw->clock, hw->log));
    flows.reset(new InviteFlows(*bot, *storage, hw->settings, *rng));
    messaging.reset(new Messaging(*bot, *sendClient, *storage, hw->clock, hw->log));
    bot->step();  // connect
    TEST_ASSERT_TRUE(bot->state() == Bot::State::Running);
}

static uint32_t addContact(long long id, const std::string& name) {
    Contact c;
    c.telegramId = id;
    c.name = name;
    c.timezone = "Europe/London";
    return storage->people.addContact(c);
}

void setUp() {
    hw = new FakeHardware();
    net = new PretendTelegram();
    rng = new FakeRandom();
    hw->clock.timeSet = true;
    hw->clock.unixSeconds = kNow;
    hw->clock.bootMs = 1000;
    storage.reset(new Storage(hw->files, hw->settings));
    storage->settings.setChildName("Child");
    storage->settings.setTimezone("Europe/London");
    storage->people.addAdmin(Person{kAdmin, "Parent"});
    storage->people.addAdmin(Person{kOtherAdmin, "Other parent"});
    granny = addContact(kGranny, "Granny");
    grandad = addContact(kGrandad, "Grandad");
    restart();
}

void tearDown() {
    messaging.reset();
    flows.reset();
    bot.reset();
    botClient.reset();
    sendClient.reset();
    storage.reset();
    delete rng;
    delete net;
    delete hw;
}

// ---- Sending things to the bot ----

static void deliver(const std::string& update) {
    net->requests.clear();
    net->batches.push_back(update);
    bot->step();
}

static void sendContent(long long from, const std::string& content, long long messageId = 0) {
    ++nextUpdate;
    deliver(messageUpdate(nextUpdate, messageJson(messageId ? messageId : nextUpdate, from, content)));
}

static void say(long long from, const std::string& text) { sendContent(from, textContent(text)); }

// Runs messaging until nothing more is due right now.
static void work() {
    for (int i = 0; i < 50; ++i) {
        if (messaging->step() != 0) return;
    }
    TEST_FAIL_MESSAGE("messaging never ran out of work");
}

static void later(uint64_t ms) {
    hw->clock.bootMs += ms;
    hw->clock.unixSeconds += static_cast<int64_t>(ms / 1000);
}

static HttpResponse refused() { return telegramError(403, "Forbidden: bot was blocked by the user"); }

// ---- Looking at what was sent ----

static JsonDocument body(const HttpRequest& r) {
    JsonDocument doc;
    deserializeJson(doc, r.body);
    return doc;
}

static std::vector<HttpRequest> sent(const std::string& method) {
    std::vector<HttpRequest> out;
    for (const auto& r : net->requests) {
        if (r.url.find("/file/bot") == std::string::npos && telegramMethod(r) == method) out.push_back(r);
    }
    return out;
}

static std::vector<std::string> textsTo(long long chat) {
    std::vector<std::string> out;
    for (const auto& r : sent("sendMessage")) {
        if (body(r)["chat_id"].as<long long>() == chat) out.push_back(body(r)["text"].as<std::string>());
    }
    return out;
}

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// A multipart field's value, e.g. "chat_id" from an uploaded voice note.
static std::string formField(const HttpRequest& r, const std::string& name) {
    const std::string marker = "name=\"" + name + "\"\r\n\r\n";
    const size_t at = r.body.find(marker);
    if (at == std::string::npos) return "<none>";
    const size_t start = at + marker.size();
    return r.body.substr(start, r.body.find("\r\n", start) - start);
}

static size_t jobsOfKind(Job::Kind kind) {
    size_t n = 0;
    for (const auto& j : storage->jobs.jobs()) n += j.kind == kind ? 1 : 0;
    return n;
}

// A recording the child has pressed Send on.
static uint32_t queueRecording(uint32_t contact, uint32_t durationMs = 4200) {
    hw->files.files[paths::kRecording] = "MP3-AUDIO";
    const uint32_t seq = storage->queueRecording(contact, paths::kRecording, durationMs, kNow);
    TEST_ASSERT_NOT_EQUAL(0, seq);
    return seq;
}

static StoredMessage onlyMessage(uint32_t contact) {
    const auto list = storage->messages.messages(contact);
    TEST_ASSERT_EQUAL(1, list.size());
    return list[0];
}

// ---------------------------------------------------------------------------
// Voice notes coming in

void test_voice_note_from_contact_is_stored_as_unread() {
    sendContent(kGranny, voiceContent("granny-voice", 7), 555);
    TEST_ASSERT_EQUAL(1, storage->jobs.size());
    TEST_ASSERT_EQUAL(0, textsTo(kGranny).size());  // no reply, it just arrives

    work();
    const StoredMessage m = onlyMessage(granny);
    TEST_ASSERT_TRUE(m.direction == StoredMessage::Direction::Received);
    TEST_ASSERT_TRUE(m.status == StoredMessage::Status::Unread);
    TEST_ASSERT_EQUAL(555, m.telegramMessageId);
    TEST_ASSERT_EQUAL_STRING("granny-voice", m.telegramFileId.c_str());
    TEST_ASSERT_EQUAL(7000, m.durationMs);
    TEST_ASSERT_EQUAL(1790000000, m.time);
    TEST_ASSERT_EQUAL_STRING("OGG-AUDIO", hw->files.files[storage->messages.audioPath(granny, m)].c_str());
    TEST_ASSERT_EQUAL(1, storage->messages.unreadCount(granny));
    TEST_ASSERT_TRUE(storage->jobs.empty());
    TEST_ASSERT_FALSE(hw->files.exists(paths::kDownload));
}

void test_voice_notes_from_one_contact_keep_their_order() {
    sendContent(kGranny, voiceContent("first", 3), 301);
    sendContent(kGranny, voiceContent("second", 3), 302);
    net->forced["download"].push_back(networkDown());
    work();  // first fails, so second waits behind it
    TEST_ASSERT_EQUAL(0, storage->messages.messages(granny).size());

    later(Messaging::kFirstRetryMs);
    work();
    const auto list = storage->messages.messages(granny);
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL(301, list[0].telegramMessageId);
    TEST_ASSERT_EQUAL(302, list[1].telegramMessageId);
}

void test_failed_download_is_tried_again_later() {
    sendContent(kGranny, voiceContent("v", 3));
    net->forced["getFile"].push_back(networkDown());
    work();
    TEST_ASSERT_EQUAL(0, storage->messages.messages(granny).size());
    TEST_ASSERT_EQUAL(1, storage->jobs.jobs()[0].attempts);

    later(Messaging::kFirstRetryMs - 1);
    work();
    TEST_ASSERT_EQUAL(0, storage->messages.messages(granny).size());  // not yet

    later(1);
    work();
    TEST_ASSERT_EQUAL(1, storage->messages.messages(granny).size());
}

void test_download_telegram_refuses_tells_the_contact() {
    sendContent(kGranny, voiceContent("v", 3));
    net->forced["getFile"].push_back(telegramError(400, "Bad Request: invalid file_id"));
    work();
    TEST_ASSERT_EQUAL(0, storage->messages.messages(granny).size());
    const auto texts = textsTo(kGranny);
    TEST_ASSERT_EQUAL(1, texts.size());
    TEST_ASSERT_TRUE(contains(texts[0], "didn't reach Child's box"));
    TEST_ASSERT_TRUE(storage->jobs.empty());
}

void test_very_big_voice_note_is_turned_down_straight_away() {
    sendContent(kGranny, voiceContent("huge", 3000, 25LL * 1024 * 1024));
    TEST_ASSERT_TRUE(storage->jobs.empty());
    TEST_ASSERT_TRUE(contains(textsTo(kGranny).at(0), "too long"));
}

void test_same_voice_note_twice_is_stored_once() {
    sendContent(kGranny, voiceContent("v", 3), 444);
    // Handled again, as if the box restarted before saving how far it had got.
    hw->settings.remove("tg_offset");
    restart();
    --nextUpdate;
    sendContent(kGranny, voiceContent("v", 3), 444);
    TEST_ASSERT_EQUAL(1, storage->jobs.size());
    work();

    // And once more after it's been stored.
    Job again;
    again.kind = Job::Kind::Download;
    again.contact = granny;
    again.telegramMessageId = 444;
    again.fileId = "v";
    storage->jobs.add(again);
    work();
    TEST_ASSERT_EQUAL(1, storage->messages.messages(granny).size());
}

void test_voice_note_during_me_still_arrives() {
    say(kGranny, "/me");
    ConversationState state;
    TEST_ASSERT_TRUE(bot->conversations().find(kGranny, state));
    sendContent(kGranny, voiceContent("v", 3));
    TEST_ASSERT_EQUAL(1, storage->jobs.size());
    TEST_ASSERT_TRUE(bot->conversations().find(kGranny, state));  // /me carries on
}

void test_downloads_survive_a_restart() {
    sendContent(kGranny, voiceContent("v", 3));
    restart();
    work();
    TEST_ASSERT_EQUAL(1, storage->messages.messages(granny).size());
}

void test_removed_contacts_downloads_are_dropped() {
    sendContent(kGranny, voiceContent("v", 3));
    storage->removeContact(granny);
    work();
    TEST_ASSERT_TRUE(storage->jobs.empty());
    TEST_ASSERT_EQUAL(0, sent("getFile").size());
}

// ---------------------------------------------------------------------------
// Things that aren't voice notes

void test_text_from_contact_gets_voice_only_reply() {
    say(kGranny, "hello darling");
    const auto texts = textsTo(kGranny);
    TEST_ASSERT_EQUAL(1, texts.size());
    TEST_ASSERT_EQUAL_STRING("Child can only receive voice notes.", texts[0].c_str());
    TEST_ASSERT_TRUE(storage->jobs.empty());
}

void test_every_other_kind_gets_voice_only_reply() {
    const std::vector<std::string> contents = {
        R"("photo":[{"file_id":"p","file_unique_id":"p1","width":90,"height":90}])",
        R"("sticker":{"file_id":"s","file_unique_id":"s1","width":512,"height":512,"type":"regular","is_animated":false,"is_video":false})",
        R"("video":{"file_id":"v","file_unique_id":"v1","width":1,"height":1,"duration":3})",
        R"("video_note":{"file_id":"vn","file_unique_id":"vn1","length":240,"duration":3})",
        R"("audio":{"file_id":"a","file_unique_id":"a1","duration":120,"mime_type":"audio/mpeg"})",
    };
    for (const auto& content : contents) {
        later(Messaging::kVoiceOnlyReplyGapMs);
        sendContent(kGranny, content);
        const auto texts = textsTo(kGranny);
        TEST_ASSERT_EQUAL_MESSAGE(1, texts.size(), content.c_str());
        TEST_ASSERT_EQUAL_STRING("Child can only receive voice notes.", texts[0].c_str());
    }
    TEST_ASSERT_TRUE(storage->jobs.empty());
}

void test_voice_only_reply_at_most_once_a_minute() {
    say(kGranny, "one");
    say(kGranny, "two");
    TEST_ASSERT_EQUAL(0, textsTo(kGranny).size());
    say(kGrandad, "three");  // someone else still gets theirs
    TEST_ASSERT_EQUAL(1, textsTo(kGrandad).size());
    later(Messaging::kVoiceOnlyReplyGapMs);
    say(kGranny, "four");
    TEST_ASSERT_EQUAL(1, textsTo(kGranny).size());
}

void test_admin_who_is_not_a_contact_cannot_send_voice_notes() {
    sendContent(kAdmin, voiceContent("v", 3));
    TEST_ASSERT_TRUE(storage->jobs.empty());
    TEST_ASSERT_TRUE(contains(textsTo(kAdmin).at(0), "Only Child's contacts"));
    say(kAdmin, "hello");
    TEST_ASSERT_TRUE(contains(textsTo(kAdmin).at(0), "/help"));
}

void test_strangers_voice_notes_are_ignored() {
    sendContent(9999, voiceContent("v", 3));
    TEST_ASSERT_TRUE(storage->jobs.empty());
    TEST_ASSERT_EQUAL(0, sent("sendMessage").size());
}

// ---------------------------------------------------------------------------
// The outbox

void test_recording_is_sent_as_a_voice_note_with_its_length() {
    queueRecording(granny, 4600);
    net->requests.clear();
    work();
    const auto voices = sent("sendVoice");
    TEST_ASSERT_EQUAL(1, voices.size());
    TEST_ASSERT_EQUAL_STRING("5005", formField(voices[0], "chat_id").c_str());
    TEST_ASSERT_EQUAL_STRING("5", formField(voices[0], "duration").c_str());
    TEST_ASSERT_TRUE(contains(voices[0].body, "MP3-AUDIO"));
    TEST_ASSERT_EQUAL_STRING("<none>", formField(voices[0], "caption").c_str());  // a plain voice bubble

    const StoredMessage m = onlyMessage(granny);
    TEST_ASSERT_TRUE(m.status == StoredMessage::Status::Delivered);
    TEST_ASSERT_EQUAL(net->lastMessageId, m.telegramMessageId);
    TEST_ASSERT_EQUAL_STRING(("sent-file-" + std::to_string(net->lastMessageId)).c_str(), m.telegramFileId.c_str());
    TEST_ASSERT_TRUE(storage->outbox.empty());
}

void test_short_recording_still_has_a_length() {
    queueRecording(granny, 300);
    work();
    TEST_ASSERT_EQUAL_STRING("1", formField(sent("sendVoice").at(0), "duration").c_str());
}

void test_outbox_retries_with_longer_gaps() {
    queueRecording(granny);
    for (int i = 0; i < 3; ++i) net->forced["sendVoice"].push_back(networkDown());

    work();  // fails: wait 5 s
    TEST_ASSERT_EQUAL(1, storage->outbox.entries()[0].attempts);
    later(5000);
    work();  // fails: wait 10 s
    TEST_ASSERT_EQUAL(2, storage->outbox.entries()[0].attempts);
    later(9999);
    work();
    TEST_ASSERT_EQUAL(2, storage->outbox.entries()[0].attempts);  // too soon
    later(1);
    work();  // fails: wait 20 s
    later(20000);
    work();
    TEST_ASSERT_TRUE(storage->outbox.empty());
    TEST_ASSERT_TRUE(onlyMessage(granny).status == StoredMessage::Status::Delivered);
}

void test_retry_gaps() {
    TEST_ASSERT_EQUAL(5000, Messaging::retryDelayMs(1));
    TEST_ASSERT_EQUAL(10000, Messaging::retryDelayMs(2));
    TEST_ASSERT_EQUAL(20000, Messaging::retryDelayMs(3));
    TEST_ASSERT_EQUAL(640000, Messaging::retryDelayMs(8));
    TEST_ASSERT_EQUAL(Messaging::kLongestRetryMs, Messaging::retryDelayMs(9));
    TEST_ASSERT_EQUAL(Messaging::kLongestRetryMs, Messaging::retryDelayMs(500));
}

void test_outbox_waits_as_long_as_telegram_asks() {
    queueRecording(granny);
    net->forced["sendVoice"].push_back(telegramError(429, "Too Many Requests: retry after 30", 30));
    work();
    later(29000);
    work();
    TEST_ASSERT_FALSE(storage->outbox.empty());
    later(1000);
    work();
    TEST_ASSERT_TRUE(storage->outbox.empty());
}

void test_outbox_keeps_order_per_contact_but_others_go_ahead() {
    queueRecording(granny);
    queueRecording(granny);
    queueRecording(grandad);
    net->forced["sendVoice"].push_back(networkDown());  // Granny's first
    net->requests.clear();
    work();
    // Granny's second waited; Grandad's went.
    const auto voices = sent("sendVoice");
    TEST_ASSERT_EQUAL(2, voices.size());
    TEST_ASSERT_EQUAL_STRING("6006", formField(voices[1], "chat_id").c_str());
    TEST_ASSERT_EQUAL(2, storage->outbox.size());

    later(Messaging::kFirstRetryMs);
    work();
    const auto list = storage->messages.messages(granny);
    TEST_ASSERT_TRUE(list[0].status == StoredMessage::Status::Delivered);
    TEST_ASSERT_TRUE(list[1].status == StoredMessage::Status::Delivered);
    TEST_ASSERT_TRUE(list[0].telegramMessageId < list[1].telegramMessageId);
}

void test_blocked_contact_tells_admins_once_and_keeps_trying_hourly() {
    queueRecording(granny);
    net->forced["sendVoice"].push_back(refused());
    net->forced["sendVoice"].push_back(refused());
    net->requests.clear();
    work();
    TEST_ASSERT_EQUAL(1, textsTo(kAdmin).size());
    TEST_ASSERT_EQUAL(1, textsTo(kOtherAdmin).size());
    TEST_ASSERT_TRUE(contains(textsTo(kAdmin)[0], "voice note to Granny couldn't be sent"));
    TEST_ASSERT_TRUE(storage->outbox.entries()[0].adminsTold);

    later(Messaging::kUnreachableRetryMs - 1);
    work();
    TEST_ASSERT_EQUAL(1, storage->outbox.entries()[0].attempts);
    later(1);
    net->requests.clear();
    work();  // refused again, nobody told again
    TEST_ASSERT_EQUAL(0, sent("sendMessage").size());
    later(Messaging::kUnreachableRetryMs);
    work();
    TEST_ASSERT_TRUE(storage->outbox.empty());
}

void test_outbox_survives_a_restart() {
    queueRecording(granny);
    net->forced["sendVoice"].push_back(networkDown());
    work();
    // After a restart the boot clock starts again, so App resets the retry times.
    hw->clock.bootMs = 0;
    storage->outbox.resetRetryTimes();
    restart();
    work();
    TEST_ASSERT_TRUE(storage->outbox.empty());
}

void test_recording_for_removed_contact_is_dropped() {
    queueRecording(grandad);
    storage->people.removeContact(grandad);  // as if removing them half-finished
    work();
    TEST_ASSERT_TRUE(storage->outbox.empty());
    TEST_ASSERT_EQUAL(0, sent("sendVoice").size());
}

void test_missing_recording_file_is_dropped() {
    queueRecording(granny);
    const StoredMessage m = onlyMessage(granny);
    hw->files.files.erase(storage->messages.audioPath(granny, m));
    work();
    TEST_ASSERT_TRUE(storage->outbox.empty());
    TEST_ASSERT_EQUAL(0, storage->messages.messages(granny).size());
}

void test_old_outbox_entries_still_load() {
    // Written before ticket 1.7, without the "admins told" field.
    hw->files.files["/outbox/3.txt"] = "1\t9\t1790000000\t2\t0\n";
    const auto entries = storage->outbox.entries();
    TEST_ASSERT_EQUAL(1, entries.size());
    TEST_ASSERT_EQUAL(2, entries[0].attempts);
    TEST_ASSERT_FALSE(entries[0].adminsTold);
}

// ---------------------------------------------------------------------------
// The 👀 reaction

static uint32_t receive(long long from, long long telegramMessageId) {
    sendContent(from, voiceContent("v" + std::to_string(telegramMessageId), 3), telegramMessageId);
    work();
    return storage->messages.messages(from == kGranny ? granny : grandad).back().id;
}

void test_heard_message_gets_the_reaction() {
    const uint32_t id = receive(kGranny, 777);
    TEST_ASSERT_TRUE(storage->markHeard(granny, id));
    TEST_ASSERT_EQUAL(1, jobsOfKind(Job::Kind::Reaction));
    net->requests.clear();
    work();
    const auto r = sent("setMessageReaction");
    TEST_ASSERT_EQUAL(1, r.size());
    JsonDocument b = body(r[0]);
    TEST_ASSERT_EQUAL(kGranny, b["chat_id"].as<long long>());
    TEST_ASSERT_EQUAL(777, b["message_id"].as<long long>());
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x91\x80", b["reaction"][0]["emoji"]);
    TEST_ASSERT_TRUE(storage->jobs.empty());
}

void test_reaction_only_sent_the_first_time() {
    const uint32_t id = receive(kGranny, 777);
    storage->markHeard(granny, id);
    storage->markHeard(granny, id);
    TEST_ASSERT_EQUAL(1, jobsOfKind(Job::Kind::Reaction));
}

void test_reaction_is_retried_then_given_up_if_refused() {
    const uint32_t id = receive(kGranny, 777);
    storage->markHeard(granny, id);
    net->forced["setMessageReaction"].push_back(networkDown());
    net->forced["setMessageReaction"].push_back(telegramError(400, "Bad Request: message to react not found"));
    work();
    TEST_ASSERT_EQUAL(1, jobsOfKind(Job::Kind::Reaction));
    later(Messaging::kFirstRetryMs);
    work();
    TEST_ASSERT_TRUE(storage->jobs.empty());
}

void test_sent_messages_are_not_marked_heard() {
    queueRecording(granny);
    TEST_ASSERT_FALSE(storage->markHeard(granny, onlyMessage(granny).id));
    TEST_ASSERT_TRUE(storage->jobs.empty());
}

// ---------------------------------------------------------------------------
// Monitor copies

static void addMonitor(long long id) { storage->people.addMonitor(Person{id, "Watcher"}); }

static std::vector<HttpRequest> copiesTo(long long chat) {
    std::vector<HttpRequest> out;
    for (const auto& r : sent("sendVoice")) {
        // Copies are sent by file ID as JSON; uploads are multipart.
        JsonDocument b = body(r);
        if (b["chat_id"].is<long long>() && b["chat_id"].as<long long>() == chat) out.push_back(r);
    }
    return out;
}

void test_monitor_gets_copies_of_both_directions_when_all() {
    addMonitor(kMonitor);
    storage->settings.setMonitorMode(MonitorMode::All);
    net->requests.clear();
    sendContent(kGranny, voiceContent("granny-voice", 3));
    work();
    queueRecording(granny);
    work();

    const auto copies = copiesTo(kMonitor);
    TEST_ASSERT_EQUAL(2, copies.size());
    TEST_ASSERT_EQUAL_STRING("granny-voice", body(copies[0])["voice"]);
    TEST_ASSERT_EQUAL_STRING("Granny to Child, 15:13", body(copies[0])["caption"]);
    // The child's recording is copied using the file ID Telegram gave it, not uploaded again.
    const StoredMessage mine = storage->messages.messages(granny).back();
    TEST_ASSERT_EQUAL_STRING(mine.telegramFileId.c_str(), body(copies[1])["voice"]);
    TEST_ASSERT_EQUAL_STRING("Child to Granny, 15:13", body(copies[1])["caption"]);
    TEST_ASSERT_EQUAL(1, sent("sendVoice").size() - copies.size());  // only one upload
}

void test_monitor_mode_decides_which_are_copied() {
    addMonitor(kMonitor);

    storage->settings.setMonitorMode(MonitorMode::Off);
    sendContent(kGranny, voiceContent("a", 3));
    work();
    queueRecording(granny);
    work();
    TEST_ASSERT_EQUAL(0, copiesTo(kMonitor).size());

    storage->settings.setMonitorMode(MonitorMode::SentOnly);
    net->requests.clear();
    sendContent(kGranny, voiceContent("b", 3));
    work();
    queueRecording(granny);
    work();
    auto copies = copiesTo(kMonitor);
    TEST_ASSERT_EQUAL(1, copies.size());
    TEST_ASSERT_TRUE(contains(body(copies[0])["caption"].as<std::string>(), "Child to Granny"));

    storage->settings.setMonitorMode(MonitorMode::ReceivedOnly);
    net->requests.clear();
    sendContent(kGranny, voiceContent("c", 3));
    work();
    queueRecording(granny);
    work();
    copies = copiesTo(kMonitor);
    TEST_ASSERT_EQUAL(1, copies.size());
    TEST_ASSERT_EQUAL_STRING("c", body(copies[0])["voice"]);
}

void test_monitor_who_is_the_contact_gets_no_copy_of_their_own() {
    addMonitor(kGranny);
    addMonitor(kMonitor);
    storage->settings.setMonitorMode(MonitorMode::All);
    net->requests.clear();
    sendContent(kGranny, voiceContent("v", 3));
    work();
    TEST_ASSERT_EQUAL(0, copiesTo(kGranny).size());
    TEST_ASSERT_EQUAL(1, copiesTo(kMonitor).size());
}

void test_copy_for_removed_monitor_is_dropped() {
    addMonitor(kMonitor);
    storage->settings.setMonitorMode(MonitorMode::All);
    net->forced["sendVoice"].push_back(networkDown());
    sendContent(kGranny, voiceContent("v", 3));
    work();
    TEST_ASSERT_EQUAL(1, jobsOfKind(Job::Kind::Copy));
    storage->people.removeMonitor(kMonitor);
    later(Messaging::kFirstRetryMs);
    work();
    TEST_ASSERT_TRUE(storage->jobs.empty());
}

void test_copy_caption_leaves_out_time_when_unknown() {
    TEST_ASSERT_EQUAL_STRING("Child to Granny, 15:13", messaging->copyCaption("Child", "Granny", kNow).c_str());
    TEST_ASSERT_EQUAL_STRING("Child to Granny", messaging->copyCaption("Child", "Granny", 0).c_str());
    hw->settings.remove("timezone");  // not confirmed yet
    TEST_ASSERT_EQUAL_STRING("Child to Granny", messaging->copyCaption("Child", "Granny", kNow).c_str());
}

// ---------------------------------------------------------------------------
// Timing

void test_nothing_to_do_waits_a_second() {
    Messaging::Task task;
    TEST_ASSERT_EQUAL(Messaging::kIdleCheckMs, messaging->prepare(task));
    TEST_ASSERT_TRUE(task.kind == Messaging::Task::Kind::None);
}

void test_waits_until_next_retry_is_due() {
    addMonitor(kMonitor);
    queueRecording(granny);
    net->forced["sendVoice"].push_back(networkDown());
    work();
    Messaging::Task task;
    later(4000);
    TEST_ASSERT_EQUAL(1000, messaging->prepare(task));
    later(800);
    TEST_ASSERT_EQUAL(200, messaging->prepare(task));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_voice_note_from_contact_is_stored_as_unread);
    RUN_TEST(test_voice_notes_from_one_contact_keep_their_order);
    RUN_TEST(test_failed_download_is_tried_again_later);
    RUN_TEST(test_download_telegram_refuses_tells_the_contact);
    RUN_TEST(test_very_big_voice_note_is_turned_down_straight_away);
    RUN_TEST(test_same_voice_note_twice_is_stored_once);
    RUN_TEST(test_voice_note_during_me_still_arrives);
    RUN_TEST(test_downloads_survive_a_restart);
    RUN_TEST(test_removed_contacts_downloads_are_dropped);
    RUN_TEST(test_text_from_contact_gets_voice_only_reply);
    RUN_TEST(test_every_other_kind_gets_voice_only_reply);
    RUN_TEST(test_voice_only_reply_at_most_once_a_minute);
    RUN_TEST(test_admin_who_is_not_a_contact_cannot_send_voice_notes);
    RUN_TEST(test_strangers_voice_notes_are_ignored);
    RUN_TEST(test_recording_is_sent_as_a_voice_note_with_its_length);
    RUN_TEST(test_short_recording_still_has_a_length);
    RUN_TEST(test_outbox_retries_with_longer_gaps);
    RUN_TEST(test_retry_gaps);
    RUN_TEST(test_outbox_waits_as_long_as_telegram_asks);
    RUN_TEST(test_outbox_keeps_order_per_contact_but_others_go_ahead);
    RUN_TEST(test_blocked_contact_tells_admins_once_and_keeps_trying_hourly);
    RUN_TEST(test_outbox_survives_a_restart);
    RUN_TEST(test_recording_for_removed_contact_is_dropped);
    RUN_TEST(test_missing_recording_file_is_dropped);
    RUN_TEST(test_old_outbox_entries_still_load);
    RUN_TEST(test_heard_message_gets_the_reaction);
    RUN_TEST(test_reaction_only_sent_the_first_time);
    RUN_TEST(test_reaction_is_retried_then_given_up_if_refused);
    RUN_TEST(test_sent_messages_are_not_marked_heard);
    RUN_TEST(test_monitor_gets_copies_of_both_directions_when_all);
    RUN_TEST(test_monitor_mode_decides_which_are_copied);
    RUN_TEST(test_monitor_who_is_the_contact_gets_no_copy_of_their_own);
    RUN_TEST(test_copy_for_removed_monitor_is_dropped);
    RUN_TEST(test_copy_caption_leaves_out_time_when_unknown);
    RUN_TEST(test_nothing_to_do_waits_a_second);
    RUN_TEST(test_waits_until_next_retry_is_due);
    return UNITY_END();
}

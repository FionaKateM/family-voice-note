// Invite links, setup conversations, approval and /me (ticket 1.5), against a
// pretend Telegram.

#include <ArduinoJson.h>
#include <unity.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "app/bot/Bot.h"
#include "app/bot/Pickers.h"
#include "app/invites/InviteFlows.h"
#include "fakes/FakeRandom.h"
#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;
using namespace fvb::telegram;

static const long long kAdmin = 1001;
static const long long kOtherAdmin = 1002;
static const long long kContact = 2002;
static const long long kGranny = 5005;  // the person being invited
static const long long kCousin = 6006;  // someone the link is forwarded to
static const int64_t kNow = 1790000000;
static const int64_t kDay = 24 * 60 * 60;

// Answers each Bot API method the way Telegram would, so a whole conversation
// can run without queueing replies in the right order.
class PretendTelegram : public Network {
public:
    bool isConnected() const override { return true; }
    int8_t signalStrength() const override { return -60; }

    HttpResponse request(const HttpRequest& req) override {
        requests.push_back(req);
        const std::string method = telegramMethod(req);
        if (req.url.find("/file/bot") != std::string::npos) {
            if (failDownloads) return HttpResponse{404, "", ""};
            const std::string data = "JPEG-DATA";
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
            JsonDocument b;
            deserializeJson(b, req.body);
            const long long chat = b["chat_id"].as<long long>();
            return telegramOk(R"({"message_id":)" + std::to_string(++lastMessageId) + R"(,"chat":{"id":)" +
                              std::to_string(chat) + R"(,"type":"private"},"date":1790000000,"text":"x"})");
        }
        if (method == "editMessageText") return telegramOk(R"({"message_id":1,"chat":{"id":1,"type":"private"},"date":1,"text":"x"})");
        if (method == "answerCallbackQuery") return telegramOk("true");
        if (method == "getUserProfilePhotos") {
            if (!hasProfilePhoto) return telegramOk(R"({"total_count":0,"photos":[]})");
            return telegramOk(R"({"total_count":1,"photos":[[{"file_id":"profile-small","file_unique_id":"a","width":160,"height":160},{"file_id":"profile-320","file_unique_id":"b","width":320,"height":320},{"file_id":"profile-big","file_unique_id":"c","width":640,"height":640}]]})");
        }
        if (method == "getFile") return telegramOk(R"({"file_id":"f","file_unique_id":"u","file_size":9,"file_path":"photos/file_1.jpg"})");
        return telegramError(400, "Bad Request: not pretended");
    }

    std::vector<HttpRequest> requests;
    std::deque<std::string> batches;
    long long lastMessageId = 500;
    bool hasProfilePhoto = true;
    bool failDownloads = false;
};

static FakeHardware* hw;
static PretendTelegram* net;
static FakeRandom* rng;
static std::unique_ptr<Storage> storage;
static std::unique_ptr<TelegramClient> client;
static std::unique_ptr<Bot> bot;
static std::unique_ptr<InviteFlows> flows;
static long long nextUpdate = 100;

// A new client, bot and flows on the same storage, as after a restart.
static void restart() {
    flows.reset();
    bot.reset();
    client.reset(new TelegramClient(*net, hw->files, hw->log, kTestBotToken));
    bot.reset(new Bot(*client, *storage, hw->settings, hw->clock, hw->log));
    flows.reset(new InviteFlows(*bot, *storage, hw->settings, *rng));
    bot->step();  // connect
    TEST_ASSERT_TRUE(bot->state() == Bot::State::Running);
}

void setUp() {
    hw = new FakeHardware();
    net = new PretendTelegram();
    rng = new FakeRandom();
    hw->clock.timeSet = true;
    hw->clock.unixSeconds = kNow;
    storage.reset(new Storage(hw->files, hw->settings));
    storage->settings.setChildName("Child");
    storage->settings.setTimezone("Europe/London");
    storage->people.addAdmin(Person{kAdmin, "Parent"});
    storage->people.addAdmin(Person{kOtherAdmin, "Other parent"});
    Contact uncle;
    uncle.telegramId = kContact;
    uncle.name = "Uncle";
    uncle.timezone = "Europe/Paris";
    storage->people.addContact(uncle);
    restart();
}

void tearDown() {
    flows.reset();
    bot.reset();
    client.reset();
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

// fakes::userJson calls everyone "Granny". Give each pretend person their own
// Telegram name instead, the same whether they type or tap a button.
static std::string renamed(std::string json, long long who) {
    std::string name;
    if (who == kAdmin) {
        name = R"("first_name":"Parent")";
    } else if (who == kOtherAdmin) {
        name = R"("first_name":"Alex")";
    } else {
        name = R"("first_name":"Pat","last_name":"Example","username":"patexample")";
    }
    const std::string original = R"("first_name":"Granny")";
    for (size_t at = json.find(original); at != std::string::npos; at = json.find(original, at + name.size())) {
        json.replace(at, original.size(), name);
    }
    return json;
}

static void sendContent(long long from, const std::string& content) {
    ++nextUpdate;
    deliver(renamed(messageUpdate(nextUpdate, messageJson(nextUpdate, from, content)), from));
}

static void say(long long from, const std::string& text) { sendContent(from, textContent(text)); }

static void press(long long from, const std::string& data) {
    ++nextUpdate;
    deliver(renamed(buttonUpdate(nextUpdate, from, 77, data), from));
}

static void sendPhoto(long long from) {
    sendContent(from, R"("photo":[{"file_id":"sent-90","file_unique_id":"p1","width":90,"height":90},{"file_id":"sent-320","file_unique_id":"p2","width":320,"height":320},{"file_id":"sent-1280","file_unique_id":"p3","width":1280,"height":1280}])");
}

// ---- Looking at what the bot did ----

static JsonDocument body(const HttpRequest& r) {
    JsonDocument doc;
    deserializeJson(doc, r.body);
    return doc;
}

static std::vector<HttpRequest> sent(const std::string& method, long long to = 0) {
    std::vector<HttpRequest> out;
    for (const auto& r : net->requests) {
        if (telegramMethod(r) != method) continue;
        if (to != 0 && body(r)["chat_id"].as<long long>() != to) continue;
        out.push_back(r);
    }
    return out;
}

static std::vector<std::string> replies(long long to = 0) {
    std::vector<std::string> out;
    for (const auto& r : sent("sendMessage", to)) out.push_back(body(r)["text"].as<std::string>());
    return out;
}

static std::string lastReply(long long to) {
    auto r = replies(to);
    TEST_ASSERT_FALSE_MESSAGE(r.empty(), "expected a reply");
    return r.back();
}

static std::string lastEdit() {
    auto e = sent("editMessageText");
    TEST_ASSERT_FALSE_MESSAGE(e.empty(), "expected a message to be changed");
    return body(e.back())["text"].as<std::string>();
}

static std::string popupText() {
    auto a = sent("answerCallbackQuery");
    if (a.empty()) return "<none>";
    return body(a.back())["text"] | "";
}

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

static void assertSilent() {
    for (const auto& r : net->requests) TEST_ASSERT_EQUAL_STRING("getUpdates", telegramMethod(r).c_str());
}

// Every button in the last message sent or edited, as (label, callback data).
static std::vector<std::pair<std::string, std::string>> lastButtons() {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto it = net->requests.rbegin(); it != net->requests.rend(); ++it) {
        const std::string m = telegramMethod(*it);
        if (m != "sendMessage" && m != "editMessageText") continue;
        JsonDocument b = body(*it);
        if (!b["reply_markup"].is<JsonObject>()) continue;
        for (JsonArray row : b["reply_markup"]["inline_keyboard"].as<JsonArray>()) {
            for (JsonObject button : row) {
                out.emplace_back(button["text"].as<std::string>(), button["callback_data"].as<std::string>());
            }
        }
        return out;
    }
    return out;
}

static std::string buttonNamed(const std::string& label) {
    for (const auto& b : lastButtons()) {
        if (b.first == label) return b.second;
    }
    TEST_FAIL_MESSAGE(("no button called " + label).c_str());
    return "";
}

static void tap(long long from, const std::string& label) { press(from, buttonNamed(label)); }

// /addcontact (or another) from an admin; returns the invite code.
static std::string makeInvite(const std::string& command = "/addcontact") {
    say(kAdmin, command);
    auto r = replies(kAdmin);
    TEST_ASSERT_EQUAL(2, r.size());
    const std::string marker = "https://t.me/TestBoxBot?start=";
    const size_t at = r[1].find(marker);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, r[1].c_str());
    return r[1].substr(at + marker.size());
}

static void openLink(long long who, const std::string& code) { say(who, "/start " + code); }

// Goes through contact setup up to (not including) the notice.
static void setUpAsGranny(const std::string& code) {
    openLink(kGranny, code);
    say(kGranny, "Granny");
    tap(kGranny, "United Kingdom");
    tap(kGranny, "Keep these");
    tap(kGranny, "Use my profile picture");
}

static uint32_t pendingId() {
    auto all = flows->pending().all();
    TEST_ASSERT_EQUAL(1, all.size());
    return all[0].id;
}

// ---------------------------------------------------------------------------
// Making invite links

void test_admin_gets_an_invite_link_to_forward() {
    say(kAdmin, "/addcontact");
    auto r = replies(kAdmin);
    TEST_ASSERT_EQUAL(2, r.size());
    TEST_ASSERT_TRUE(contains(r[0], "48 hours"));
    TEST_ASSERT_TRUE(contains(r[1], "You're invited to send voice notes to Child"));
    TEST_ASSERT_TRUE(contains(r[1], "https://t.me/TestBoxBot?start="));

    auto invites = flows->invites().all();
    TEST_ASSERT_EQUAL(1, invites.size());
    TEST_ASSERT_EQUAL(InviteFlows::kCodeLength, invites[0].code.size());
    TEST_ASSERT_TRUE(TelegramClient::isValidStartCode(invites[0].code));
    TEST_ASSERT_TRUE(invites[0].role == InviteRole::Contact);
    TEST_ASSERT_EQUAL(kNow, invites[0].createdAt);
    TEST_ASSERT_EQUAL(kAdmin, invites[0].createdBy);
    TEST_ASSERT_EQUAL(0, invites[0].userId);
}

void test_every_invite_code_is_different() {
    const std::string a = makeInvite();
    const std::string b = makeInvite();
    TEST_ASSERT_TRUE(a != b);
}

void test_only_admins_can_invite() {
    say(kContact, "/addcontact");
    TEST_ASSERT_EQUAL_STRING("Only admins can use /addcontact.", lastReply(kContact).c_str());
    TEST_ASSERT_EQUAL(0, flows->invites().all().size());
}

void test_invite_commands_are_in_admin_help() {
    say(kAdmin, "/help");
    const std::string help = lastReply(kAdmin);
    TEST_ASSERT_TRUE(contains(help, "/addcontact"));
    TEST_ASSERT_TRUE(contains(help, "/addmonitor"));
    TEST_ASSERT_TRUE(contains(help, "/addadmin"));
    TEST_ASSERT_FALSE(contains(help, "/me "));  // the admin isn't a contact

    say(kContact, "/help");
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "/me"));
    TEST_ASSERT_FALSE(contains(lastReply(kContact), "/addcontact"));
}

void test_no_contact_invites_when_the_box_is_full() {
    for (int i = 0; i < 29; ++i) {
        Contact c;
        c.telegramId = 70000 + i;
        c.name = "Person " + std::to_string(i);
        storage->people.addContact(c);
    }
    say(kAdmin, "/addcontact");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "already has 30 contacts"));
    TEST_ASSERT_EQUAL(0, flows->invites().all().size());
}

// ---------------------------------------------------------------------------
// Contact setup

void test_contact_setup_from_start_to_approval() {
    const std::string code = makeInvite();
    openLink(kGranny, code);
    auto r = replies(kGranny);
    TEST_ASSERT_EQUAL(2, r.size());
    TEST_ASSERT_TRUE(contains(r[0], "invited to send voice notes to Child"));
    TEST_ASSERT_TRUE(contains(r[1], "What does Child call you?"));
    TEST_ASSERT_EQUAL(kGranny, flows->invites().all()[0].userId);

    say(kGranny, "Granny");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Which country do you live in?"));
    // The box's own country and the other contact's are offered first.
    auto buttons = lastButtons();
    TEST_ASSERT_EQUAL_STRING("United Kingdom", buttons[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("France", buttons[1].first.c_str());
    TEST_ASSERT_EQUAL_STRING("Another country", buttons[2].first.c_str());

    tap(kGranny, "United Kingdom");
    TEST_ASSERT_EQUAL_STRING("Country: United Kingdom", lastEdit().c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "9pm to 9am"));

    tap(kGranny, "Keep these");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Telegram profile picture"));

    tap(kGranny, "Use my profile picture");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Child's parents may be able to hear all voice notes to and from you."));
    TEST_ASSERT_EQUAL(0, flows->pending().all().size());  // not until they've read the notice

    tap(kGranny, "I understand");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Child's parents now need to approve you"));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kGranny, state));
    TEST_ASSERT_EQUAL(0, flows->invites().all().size());  // the link is finished with

    // Both admins are asked.
    for (long long admin : {kAdmin, kOtherAdmin}) {
        const std::string ask = lastReply(admin);
        TEST_ASSERT_TRUE_MESSAGE(contains(ask, "Granny has finished setting up. Approve?"), ask.c_str());
        TEST_ASSERT_TRUE(contains(ask, "Pat Example (@patexample)"));
        TEST_ASSERT_TRUE(contains(ask, "United Kingdom"));
    }
    PendingApproval p = flows->pending().all()[0];
    TEST_ASSERT_EQUAL(2, p.adminMessages.size());
    TEST_ASSERT_EQUAL_STRING("profile-320", p.photoFileId.c_str());

    // The first admin approves.
    press(kAdmin, "approve:" + std::to_string(p.id) + ":y");
    Contact granny;
    TEST_ASSERT_TRUE(storage->people.findContactByTelegramId(kGranny, granny));
    TEST_ASSERT_EQUAL_STRING("Granny", granny.name.c_str());
    TEST_ASSERT_EQUAL_STRING("Europe/London", granny.timezone.c_str());
    TEST_ASSERT_EQUAL(21 * 60, granny.sleepStart);
    TEST_ASSERT_EQUAL(9 * 60, granny.sleepEnd);
    TEST_ASSERT_TRUE(storage->hasContactPhoto(granny.number));
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "You're all set. Send Child a voice note any time."));
    TEST_ASSERT_EQUAL_STRING("Approved", popupText().c_str());
    TEST_ASSERT_EQUAL(0, flows->pending().all().size());

    // Both admins' messages are changed so nobody answers twice.
    auto edits = sent("editMessageText");
    TEST_ASSERT_EQUAL(2, edits.size());
    TEST_ASSERT_EQUAL_STRING("Granny was approved by you.", body(edits[0])["text"].as<std::string>().c_str());
    TEST_ASSERT_EQUAL(kOtherAdmin, body(edits[1])["chat_id"].as<long long>());
    TEST_ASSERT_EQUAL_STRING("Granny was approved by Parent.", body(edits[1])["text"].as<std::string>().c_str());

    // Now Granny is a contact.
    say(kGranny, "/help");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Send Child a voice note any time"));
}

void test_second_admin_pressing_after_the_first_does_nothing() {
    setUpAsGranny(makeInvite());
    tap(kGranny, "I understand");
    const uint32_t id = pendingId();
    press(kAdmin, "approve:" + std::to_string(id) + ":y");
    press(kOtherAdmin, "approve:" + std::to_string(id) + ":n");
    TEST_ASSERT_EQUAL_STRING("This has already been answered.", popupText().c_str());
    TEST_ASSERT_TRUE(storage->people.isContact(kGranny));
    TEST_ASSERT_EQUAL(0, replies(kGranny).size());
}

void test_turned_down_contact_is_told_and_not_added() {
    setUpAsGranny(makeInvite());
    tap(kGranny, "I understand");
    press(kOtherAdmin, "approve:" + std::to_string(pendingId()) + ":n");
    TEST_ASSERT_FALSE(storage->people.isContact(kGranny));
    TEST_ASSERT_EQUAL_STRING("Sorry, Child's parents didn't approve this.", lastReply(kGranny).c_str());
    auto edits = sent("editMessageText");
    TEST_ASSERT_EQUAL(2, edits.size());
    TEST_ASSERT_EQUAL_STRING("Granny was turned down by you.", body(edits[0])["text"].as<std::string>().c_str());
    TEST_ASSERT_EQUAL_STRING("Granny was turned down by Alex.", body(edits[1])["text"].as<std::string>().c_str());
    TEST_ASSERT_EQUAL(0, flows->pending().all().size());

    // And they're a stranger again.
    say(kGranny, "hello?");
    assertSilent();
}

void test_only_admins_can_approve() {
    setUpAsGranny(makeInvite());
    tap(kGranny, "I understand");
    press(kContact, "approve:" + std::to_string(pendingId()) + ":y");
    TEST_ASSERT_EQUAL_STRING("Only admins can do that.", popupText().c_str());
    TEST_ASSERT_FALSE(storage->people.isContact(kGranny));
}

void test_someone_waiting_for_approval_is_told_so() {
    setUpAsGranny(makeInvite());
    tap(kGranny, "I understand");
    say(kGranny, "any news?");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "just need to approve you"));
}

void test_photo_failing_to_download_still_adds_the_contact() {
    setUpAsGranny(makeInvite());
    tap(kGranny, "I understand");
    net->failDownloads = true;
    press(kAdmin, "approve:" + std::to_string(pendingId()) + ":y");
    Contact granny;
    TEST_ASSERT_TRUE(storage->people.findContactByTelegramId(kGranny, granny));
    TEST_ASSERT_FALSE(storage->hasContactPhoto(granny.number));
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "couldn't download Granny's photo"));
}

void test_name_must_be_sensible() {
    openLink(kGranny, makeInvite());
    say(kGranny, "/granny");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "looks like a command"));
    say(kGranny, "A name that is far too long to fit on the screen");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "a bit long"));
    say(kGranny, "uncle");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "already called Uncle"));
    sendPhoto(kGranny);
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Please type the name"));
    say(kGranny, "  Nana  ");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Which country"));

    ConversationState state;
    TEST_ASSERT_TRUE(bot->conversations().find(kGranny, state));
    TEST_ASSERT_EQUAL_STRING("Nana", state.get("name").c_str());
}

void test_typing_a_country_works() {
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    say(kGranny, "france");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "sleep hours"));
    ConversationState state;
    bot->conversations().find(kGranny, state);
    TEST_ASSERT_EQUAL_STRING("Europe/Paris", state.get("zone").c_str());
}

void test_country_with_several_zones_asks_for_a_city() {
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    tap(kGranny, "Another country");
    TEST_ASSERT_TRUE(contains(lastEdit(), "letter"));
    tap(kGranny, "U");
    tap(kGranny, "United States");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Which city"));
    tap(kGranny, "Chicago");
    TEST_ASSERT_EQUAL_STRING("Time zone: United States (Chicago)", lastEdit().c_str());

    ConversationState state;
    bot->conversations().find(kGranny, state);
    TEST_ASSERT_EQUAL_STRING("America/Chicago", state.get("zone").c_str());
    TEST_ASSERT_EQUAL_STRING("sleep", state.step.c_str());
}

void test_changing_sleep_hours() {
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    tap(kGranny, "United Kingdom");
    tap(kGranny, "Change");
    TEST_ASSERT_TRUE(contains(lastEdit(), "go to sleep"));
    TEST_ASSERT_EQUAL(24, lastButtons().size());
    tap(kGranny, "10pm");
    tap(kGranny, "6am");
    TEST_ASSERT_EQUAL_STRING("Sleep hours: 10pm to 6am", lastEdit().c_str());
    ConversationState state;
    bot->conversations().find(kGranny, state);
    TEST_ASSERT_EQUAL_STRING(std::to_string(22 * 60).c_str(), state.get("ss").c_str());
    TEST_ASSERT_EQUAL_STRING(std::to_string(6 * 60).c_str(), state.get("se").c_str());
    TEST_ASSERT_EQUAL_STRING("photo", state.step.c_str());
}

void test_photo_sent_as_a_file_is_turned_down() {
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    tap(kGranny, "United Kingdom");
    tap(kGranny, "Keep these");
    sendContent(kGranny, R"("document":{"file_id":"doc1","file_unique_id":"d1","file_name":"me.jpg","mime_type":"image/jpeg"})");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "normal photo"));

    sendPhoto(kGranny);
    ConversationState state;
    bot->conversations().find(kGranny, state);
    TEST_ASSERT_EQUAL_STRING("sent-320", state.get("photo").c_str());  // the size closest to 320 wide
    TEST_ASSERT_EQUAL_STRING("notice", state.step.c_str());
}

void test_no_profile_picture_asks_for_a_photo() {
    net->hasProfilePhoto = false;
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    tap(kGranny, "United Kingdom");
    tap(kGranny, "Keep these");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "Send me a photo of yourself"));
    TEST_ASSERT_EQUAL(0, lastButtons().size());  // a photo is required, so no way to skip

    say(kGranny, "I don't have one");
    TEST_ASSERT_EQUAL_STRING("Please send me a photo of yourself.", lastReply(kGranny).c_str());
    sendPhoto(kGranny);
    tap(kGranny, "I understand");
    TEST_ASSERT_EQUAL_STRING("sent-320", flows->pending().all()[0].photoFileId.c_str());
}

void test_setup_photo_cannot_be_skipped() {
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    tap(kGranny, "United Kingdom");
    tap(kGranny, "Keep these");
    auto buttons = lastButtons();
    TEST_ASSERT_EQUAL(1, buttons.size());
    TEST_ASSERT_EQUAL_STRING("Use my profile picture", buttons[0].first.c_str());

    // Even an old-style skip button does nothing.
    ConversationState state;
    bot->conversations().find(kGranny, state);
    press(kGranny, "#" + std::to_string(state.id) + ":photo:none");
    TEST_ASSERT_EQUAL_STRING("This button no longer works.", popupText().c_str());
    bot->conversations().find(kGranny, state);
    TEST_ASSERT_EQUAL_STRING("photo", state.step.c_str());
}

void test_setup_carries_on_after_a_restart() {
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    restart();
    tap(kGranny, "United Kingdom");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "sleep hours"));
}

// ---------------------------------------------------------------------------
// Links that don't work

void test_made_up_link_is_ignored() {
    openLink(kGranny, "NotARealCode123");
    assertSilent();
}

void test_link_only_works_for_the_first_person() {
    const std::string code = makeInvite();
    openLink(kGranny, code);
    openLink(kCousin, code);
    TEST_ASSERT_TRUE(contains(lastReply(kCousin), "already been used"));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kCousin, state));
}

void test_link_runs_out_after_48_hours() {
    const std::string code = makeInvite();
    hw->clock.unixSeconds += InviteStore::kLifetimeSeconds + 1;
    openLink(kGranny, code);
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "has run out"));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kGranny, state));

    // Long after that it's forgotten, and the person is just a stranger.
    hw->clock.unixSeconds += InviteStore::kKeepUnusedSeconds + kDay;
    openLink(kGranny, code);
    assertSilent();
}

void test_existing_contact_cant_use_a_contact_link() {
    const std::string code = makeInvite();
    openLink(kContact, code);
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "already a contact"));
    TEST_ASSERT_EQUAL(0, flows->invites().all()[0].userId);  // still free for the real invitee
}

// ---------------------------------------------------------------------------
// Coming back after stopping or going quiet

void test_stopped_setup_can_be_started_again_from_the_link() {
    const std::string code = makeInvite();
    openLink(kGranny, code);
    say(kGranny, "/cancel");
    TEST_ASSERT_EQUAL_STRING("Stopped.", lastReply(kGranny).c_str());

    say(kGranny, "hello");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "tap your invite link"));

    openLink(kGranny, code);
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "What does Child call you?"));
}

void test_quiet_for_a_week_is_told_to_ask_for_a_new_link() {
    const std::string code = makeInvite();
    openLink(kGranny, code);
    say(kGranny, "Granny");

    hw->clock.unixSeconds += Bot::kConversationTimeoutSeconds + 1;
    say(kGranny, "sorry, I'm back");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "stopped after a week"));
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "new invite link"));

    // Only told once; after that they're a stranger again.
    say(kGranny, "hello?");
    assertSilent();
}

void test_quiet_for_a_week_while_others_use_the_box() {
    // The conversation is swept away while handling someone else's message,
    // rather than when Granny comes back. She should still be told.
    openLink(kGranny, makeInvite());
    say(kGranny, "Granny");
    hw->clock.unixSeconds += Bot::kConversationTimeoutSeconds + 1;
    say(kContact, "/help");
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kGranny, state));

    hw->clock.unixSeconds += 3 * kDay;
    press(kGranny, "#1:tz:c:GB");  // an old button
    TEST_ASSERT_EQUAL_STRING("This has finished.", popupText().c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "new invite link"));
}

void test_strangers_with_nothing_to_do_with_invites_are_still_ignored() {
    makeInvite();
    say(kCousin, "hello");
    assertSilent();
    press(kCousin, "approve:1:y");
    assertSilent();
}

// ---------------------------------------------------------------------------
// Monitors and admins

void test_monitor_setup_and_approval() {
    const std::string code = makeInvite("/addmonitor");
    TEST_ASSERT_TRUE(contains(replies(kAdmin)[1], "copies of Child's voice notes"));
    openLink(kGranny, code);
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "monitor"));
    tap(kGranny, "Join");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Pat Example (@patexample) wants to be a monitor"));

    press(kAdmin, "approve:" + std::to_string(pendingId()) + ":y");
    TEST_ASSERT_TRUE(storage->people.isMonitor(kGranny));
    TEST_ASSERT_FALSE(storage->people.isContact(kGranny));
    TEST_ASSERT_EQUAL_STRING("Pat", storage->people.monitors()[0].name.c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "switched off at the moment"));
}

void test_admin_setup_and_approval_tells_all_admins() {
    openLink(kGranny, makeInvite("/addadmin"));
    tap(kGranny, "Join");
    press(kOtherAdmin, "approve:" + std::to_string(pendingId()) + ":y");
    TEST_ASSERT_TRUE(storage->people.isAdmin(kGranny));
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "You're now an admin"));
    auto edits = sent("editMessageText");
    TEST_ASSERT_EQUAL(2, edits.size());
    TEST_ASSERT_EQUAL_STRING("Pat Example (@patexample) is now an admin, approved by you.",
                             body(edits[0])["text"].as<std::string>().c_str());
    TEST_ASSERT_EQUAL_STRING("Pat Example (@patexample) is now an admin, approved by Alex.",
                             body(edits[1])["text"].as<std::string>().c_str());
}

void test_no_thanks_ends_monitor_setup() {
    openLink(kGranny, makeInvite("/addmonitor"));
    tap(kGranny, "No thanks");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Nothing has been set up"));
    TEST_ASSERT_EQUAL(0, flows->invites().all().size());
    TEST_ASSERT_EQUAL(0, flows->pending().all().size());
    say(kGranny, "hello");
    assertSilent();
}

void test_a_contact_can_also_become_a_monitor() {
    openLink(kContact, makeInvite("/addmonitor"));
    tap(kContact, "Join");
    press(kAdmin, "approve:" + std::to_string(pendingId()) + ":y");
    TEST_ASSERT_TRUE(storage->people.isMonitor(kContact));
    TEST_ASSERT_TRUE(storage->people.isContact(kContact));
}

// ---------------------------------------------------------------------------
// /me

void test_contact_changes_their_details_with_me() {
    say(kContact, "/me");
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "Name: Uncle"));
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "Time zone: France"));

    tap(kContact, "Name");
    say(kContact, "Uncle Bob");
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "Saved."));

    tap(kContact, "Sleep hours");
    tap(kContact, "Change");
    tap(kContact, "11pm");
    tap(kContact, "7am");
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "Saved."));

    tap(kContact, "Time zone");
    say(kContact, "Spain");
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "Which city"));  // mainland or the Canary Islands
    tap(kContact, "Madrid");
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "Saved."));

    tap(kContact, "Photo");
    sendPhoto(kContact);
    TEST_ASSERT_TRUE(contains(lastReply(kContact), "Saved."));

    tap(kContact, "Done");
    TEST_ASSERT_EQUAL_STRING("All done.", lastEdit().c_str());

    Contact c;
    TEST_ASSERT_TRUE(storage->people.findContactByTelegramId(kContact, c));
    TEST_ASSERT_EQUAL_STRING("Uncle Bob", c.name.c_str());
    TEST_ASSERT_EQUAL(23 * 60, c.sleepStart);
    TEST_ASSERT_EQUAL(7 * 60, c.sleepEnd);
    TEST_ASSERT_EQUAL_STRING("Europe/Madrid", c.timezone.c_str());
    TEST_ASSERT_TRUE(storage->hasContactPhoto(c.number));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kContact, state));
}

void test_me_is_only_for_contacts() {
    say(kAdmin, "/me");
    TEST_ASSERT_EQUAL_STRING("You can't use /me.", lastReply(kAdmin).c_str());
}

void test_contact_removed_during_me_is_dropped() {
    say(kContact, "/me");
    Contact c;
    storage->people.findContactByTelegramId(kContact, c);
    storage->people.removeContact(c.number);
    tap(kContact, "Name");
    // The conversation is dropped, and they're told the box isn't taking their voice notes.
    TEST_ASSERT_EQUAL_STRING("This box isn't taking voice notes from you at the moment.", lastReply(kContact).c_str());
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kContact, state));
}

// ---------------------------------------------------------------------------
// Removed contacts

static const char* kRemovedReply = "This box isn't taking voice notes from you at the moment.";

static void removeUncle() {
    Contact c;
    TEST_ASSERT_TRUE(storage->people.findContactByTelegramId(kContact, c));
    TEST_ASSERT_TRUE(storage->removeContact(c.number));
}

void test_removed_contact_is_told_once_a_day() {
    removeUncle();
    sendContent(kContact, voiceContent("v1", 5));
    auto r = replies(kContact);
    TEST_ASSERT_EQUAL(1, r.size());
    TEST_ASSERT_EQUAL_STRING(kRemovedReply, r[0].c_str());

    // Trying again the same day gets nothing, whatever they send.
    hw->clock.unixSeconds += kDay - 60;
    sendContent(kContact, voiceContent("v2", 5));
    assertSilent();
    say(kContact, "/help");
    assertSilent();
    press(kContact, "#1:name");  // an old button
    assertSilent();

    // A day after they were last told, they're told again.
    hw->clock.unixSeconds += 60;
    say(kContact, "hello?");
    TEST_ASSERT_EQUAL_STRING(kRemovedReply, lastReply(kContact).c_str());
    TEST_ASSERT_EQUAL(1, replies(kContact).size());
}

void test_removed_contact_is_still_told_after_a_restart() {
    removeUncle();
    restart();
    say(kContact, "hello");
    TEST_ASSERT_EQUAL_STRING(kRemovedReply, lastReply(kContact).c_str());
    restart();
    say(kContact, "hello");
    assertSilent();  // the once-a-day limit survives a restart too
}

void test_removed_contact_pressing_a_button_is_told() {
    removeUncle();
    press(kContact, "#1:name");
    TEST_ASSERT_EQUAL_STRING(kRemovedReply, lastReply(kContact).c_str());
}

void test_removed_contact_invited_again_and_approved_is_forgotten() {
    // Granny was a contact once and was removed.
    Contact old;
    old.telegramId = kGranny;
    old.name = "Granny";
    old.timezone = "Europe/London";
    TEST_ASSERT_TRUE(storage->removeContact(storage->people.addContact(old)));
    TEST_ASSERT_TRUE(storage->people.wasRemoved(kGranny));

    // A new invite link takes priority over the removed reply.
    setUpAsGranny(makeInvite());
    tap(kGranny, "I understand");
    TEST_ASSERT_TRUE(contains(lastReply(kGranny), "now need to approve you"));
    press(kAdmin, "approve:" + std::to_string(pendingId()) + ":y");
    TEST_ASSERT_TRUE(storage->people.isContact(kGranny));
    TEST_ASSERT_FALSE(storage->people.wasRemoved(kGranny));
}

void test_removed_contact_turned_down_again_is_still_remembered() {
    Contact old;
    old.telegramId = kGranny;
    old.name = "Granny";
    old.timezone = "Europe/London";
    TEST_ASSERT_TRUE(storage->removeContact(storage->people.addContact(old)));

    setUpAsGranny(makeInvite());
    tap(kGranny, "I understand");
    press(kAdmin, "approve:" + std::to_string(pendingId()) + ":n");
    TEST_ASSERT_TRUE(storage->people.wasRemoved(kGranny));
    say(kGranny, "hello?");
    TEST_ASSERT_EQUAL_STRING(kRemovedReply, lastReply(kGranny).c_str());
}

void test_removed_contact_who_is_still_an_admin_is_not_told() {
    storage->people.addAdmin(Person{kContact, "Uncle"});
    removeUncle();
    sendContent(kContact, voiceContent("v1", 5));
    // Still on the box as an admin, so they get the normal reply instead.
    for (const auto& r : replies(kContact)) TEST_ASSERT_TRUE(r != kRemovedReply);
}

// ---------------------------------------------------------------------------
// The stores and pickers on their own

void test_invite_store_round_trip_and_limits() {
    FakeSettingsStore store;
    InviteStore invites(store);
    Invite i;
    i.code = "abc";
    i.role = InviteRole::Admin;
    i.createdAt = 100;
    i.createdBy = 7;
    TEST_ASSERT_TRUE(invites.save(i));
    i.userId = 9;
    TEST_ASSERT_TRUE(invites.save(i));  // replaces
    Invite out;
    TEST_ASSERT_TRUE(invites.find("abc", out));
    TEST_ASSERT_TRUE(out.role == InviteRole::Admin);
    TEST_ASSERT_EQUAL(9, out.userId);
    TEST_ASSERT_TRUE(invites.findByUser(9, out));
    TEST_ASSERT_FALSE(invites.findByUser(0, out));

    for (size_t n = 1; n < InviteStore::kMaxInvites; ++n) {
        i.code = "code" + std::to_string(n);
        TEST_ASSERT_TRUE(invites.save(i));
    }
    i.code = "onetoomany";
    TEST_ASSERT_FALSE(invites.save(i));
    TEST_ASSERT_TRUE(invites.remove("abc"));
    TEST_ASSERT_FALSE(invites.remove("abc"));
    TEST_ASSERT_TRUE(invites.save(i));
}

void test_invite_store_tidy() {
    FakeSettingsStore store;
    InviteStore invites(store);
    const int64_t now = 100 * kDay;
    auto add = [&](const std::string& code, int64_t created, int64_t user, int64_t timedOut) {
        Invite i;
        i.code = code;
        i.createdAt = created;
        i.userId = user;
        i.timedOutAt = timedOut;
        invites.save(i);
    };
    add("fresh", now - kDay, 0, 0);
    add("unused-recent", now - 10 * kDay, 0, 0);  // ran out, still remembered
    add("unused-old", now - 20 * kDay, 0, 0);
    add("stopped-valid", now - kDay, 5, 0);
    add("stopped-old", now - 3 * kDay, 6, 0);
    add("still-going", now - 9 * kDay, 7, 0);
    add("quiet-recent", now - 20 * kDay, 8, now - 10 * kDay);
    add("quiet-old", now - 50 * kDay, 9, now - 40 * kDay);
    add("clock-unset", 0, 0, 0);

    TEST_ASSERT_EQUAL(0, invites.tidy(0, nullptr));  // clock not set: nothing
    const size_t removed = invites.tidy(now, [](const Invite& i) { return i.userId == 7; });
    TEST_ASSERT_EQUAL(3, removed);
    Invite out;
    TEST_ASSERT_FALSE(invites.find("unused-old", out));
    TEST_ASSERT_FALSE(invites.find("stopped-old", out));
    TEST_ASSERT_FALSE(invites.find("quiet-old", out));
    TEST_ASSERT_TRUE(invites.find("still-going", out));
    TEST_ASSERT_TRUE(invites.find("quiet-recent", out));
    TEST_ASSERT_TRUE(invites.find("clock-unset", out));
    TEST_ASSERT_FALSE(InviteStore::isExpired(out, now));
}

void test_pending_store() {
    FakeSettingsStore store;
    PendingStore pending(store);
    PendingApproval p;
    p.userId = 5;
    p.role = InviteRole::Contact;
    p.telegramName = "Pat\tExample";  // odd characters survive
    p.details.name = "Granny";
    p.details.timezone = "Europe/London";
    p.details.sleepStart = 60;
    p.details.sleepEnd = 120;
    p.photoFileId = "photo";
    p.adminMessages = {{1, 10}, {2, 20}};
    const uint32_t id = pending.save(p);
    TEST_ASSERT_NOT_EQUAL(0, id);

    PendingApproval out;
    TEST_ASSERT_TRUE(pending.find(id, out));
    TEST_ASSERT_EQUAL_STRING("Pat\tExample", out.telegramName.c_str());
    TEST_ASSERT_EQUAL_STRING("Granny", out.details.name.c_str());
    TEST_ASSERT_EQUAL(120, out.details.sleepEnd);
    TEST_ASSERT_EQUAL(2, out.adminMessages.size());
    TEST_ASSERT_EQUAL(20, out.adminMessages[1].second);

    // A newer request from the same person for the same role replaces it.
    p.details.name = "Nana";
    const uint32_t newer = pending.save(p);
    TEST_ASSERT_NOT_EQUAL(id, newer);
    TEST_ASSERT_EQUAL(1, pending.all().size());
    // A different role is kept alongside.
    p.role = InviteRole::Monitor;
    pending.save(p);
    TEST_ASSERT_EQUAL(2, pending.all().size());

    // When full, the oldest goes.
    for (int n = 0; n < 12; ++n) {
        p.userId = 100 + n;
        pending.save(p);
    }
    TEST_ASSERT_EQUAL(PendingStore::kMaxPending, pending.all().size());
    TEST_ASSERT_FALSE(pending.findByUser(5, out));
    TEST_ASSERT_TRUE(pending.findByUser(111, out));
}

void test_country_search() {
    TEST_ASSERT_EQUAL(1, timezonePicker::findCountries("UK").size());
    TEST_ASSERT_EQUAL_STRING("GB", timezonePicker::findCountries("uk")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("GB", timezonePicker::findCountries(" United Kingdom ")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("FR", timezonePicker::findCountries("FRANCE")[0].c_str());
    TEST_ASSERT_TRUE(timezonePicker::findCountries("united").size() > 1);
    TEST_ASSERT_EQUAL(0, timezonePicker::findCountries("Atlantis").size());
    TEST_ASSERT_EQUAL(0, timezonePicker::findCountries("").size());
}

void test_describing_zones_and_times() {
    TEST_ASSERT_EQUAL_STRING("United Kingdom", timezonePicker::describe("Europe/London").c_str());
    TEST_ASSERT_EQUAL_STRING("United States (Chicago)", timezonePicker::describe("America/Chicago").c_str());
    TEST_ASSERT_EQUAL_STRING("", timezonePicker::describe("Mars/Olympus").c_str());
    TEST_ASSERT_EQUAL_STRING("12am", formatTimeOfDay(0).c_str());
    TEST_ASSERT_EQUAL_STRING("9am", formatTimeOfDay(9 * 60).c_str());
    TEST_ASSERT_EQUAL_STRING("12pm", formatTimeOfDay(12 * 60).c_str());
    TEST_ASSERT_EQUAL_STRING("9pm", formatTimeOfDay(21 * 60).c_str());
    TEST_ASSERT_EQUAL_STRING("6:30am", formatTimeOfDay(6 * 60 + 30).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_admin_gets_an_invite_link_to_forward);
    RUN_TEST(test_every_invite_code_is_different);
    RUN_TEST(test_only_admins_can_invite);
    RUN_TEST(test_invite_commands_are_in_admin_help);
    RUN_TEST(test_no_contact_invites_when_the_box_is_full);
    RUN_TEST(test_contact_setup_from_start_to_approval);
    RUN_TEST(test_second_admin_pressing_after_the_first_does_nothing);
    RUN_TEST(test_turned_down_contact_is_told_and_not_added);
    RUN_TEST(test_only_admins_can_approve);
    RUN_TEST(test_someone_waiting_for_approval_is_told_so);
    RUN_TEST(test_photo_failing_to_download_still_adds_the_contact);
    RUN_TEST(test_name_must_be_sensible);
    RUN_TEST(test_typing_a_country_works);
    RUN_TEST(test_country_with_several_zones_asks_for_a_city);
    RUN_TEST(test_changing_sleep_hours);
    RUN_TEST(test_photo_sent_as_a_file_is_turned_down);
    RUN_TEST(test_no_profile_picture_asks_for_a_photo);
    RUN_TEST(test_setup_photo_cannot_be_skipped);
    RUN_TEST(test_setup_carries_on_after_a_restart);
    RUN_TEST(test_made_up_link_is_ignored);
    RUN_TEST(test_link_only_works_for_the_first_person);
    RUN_TEST(test_link_runs_out_after_48_hours);
    RUN_TEST(test_existing_contact_cant_use_a_contact_link);
    RUN_TEST(test_stopped_setup_can_be_started_again_from_the_link);
    RUN_TEST(test_quiet_for_a_week_is_told_to_ask_for_a_new_link);
    RUN_TEST(test_quiet_for_a_week_while_others_use_the_box);
    RUN_TEST(test_strangers_with_nothing_to_do_with_invites_are_still_ignored);
    RUN_TEST(test_monitor_setup_and_approval);
    RUN_TEST(test_admin_setup_and_approval_tells_all_admins);
    RUN_TEST(test_no_thanks_ends_monitor_setup);
    RUN_TEST(test_a_contact_can_also_become_a_monitor);
    RUN_TEST(test_contact_changes_their_details_with_me);
    RUN_TEST(test_me_is_only_for_contacts);
    RUN_TEST(test_contact_removed_during_me_is_dropped);
    RUN_TEST(test_removed_contact_is_told_once_a_day);
    RUN_TEST(test_removed_contact_is_still_told_after_a_restart);
    RUN_TEST(test_removed_contact_pressing_a_button_is_told);
    RUN_TEST(test_removed_contact_invited_again_and_approved_is_forgotten);
    RUN_TEST(test_removed_contact_turned_down_again_is_still_remembered);
    RUN_TEST(test_removed_contact_who_is_still_an_admin_is_not_told);
    RUN_TEST(test_invite_store_round_trip_and_limits);
    RUN_TEST(test_invite_store_tidy);
    RUN_TEST(test_pending_store);
    RUN_TEST(test_country_search);
    RUN_TEST(test_describing_zones_and_times);
    return UNITY_END();
}

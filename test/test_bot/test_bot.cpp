// The conversation engine, against pretend Telegram replies.

#include <ArduinoJson.h>
#include <unity.h>

#include <memory>
#include <string>
#include <vector>

#include "app/bot/Bot.h"
#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;
using namespace fvb::telegram;

static const long long kAdmin = 1001;
static const long long kContact = 2002;
static const long long kMonitor = 3003;
static const long long kEveryRole = 4004;
static const long long kStranger = 9009;
static const long long kBotId = 42;

// A two-question conversation used to test the engine.
class Questions : public ConversationType {
public:
    const char* name() const override { return "questions"; }
    bool allows(const Roles& roles) const override { return anyone || roles.isAdmin(); }

    Outcome begin(Context& ctx, ConversationState& state) override {
        state.step = "colour";
        ctx.reply("Favourite colour?");
        return Outcome::Continue;
    }
    Outcome onMessage(Context& ctx, ConversationState& state, const Message& message) override {
        if (state.step == "colour") {
            state.set("colour", message.text);
            state.step = "sure";
            InlineKeyboard keyboard = {{ctx.button("Yes", "yes"), ctx.button("No", "no")}};
            ctx.reply("Sure?", &keyboard);
            return Outcome::Continue;
        }
        ctx.reply("Press a button");
        return Outcome::Continue;
    }
    Outcome onButton(Context& ctx, ConversationState& state, const std::string& value) override {
        lastButton = value;
        if (value == "yes") {
            answer = state.get("colour") + "/" + state.get("from");
            ctx.popup("Saved");
            ctx.editButtonMessage("Saved " + state.get("colour"));
            return Outcome::Finished;
        }
        state.step = "colour";
        ctx.reply("Favourite colour?");
        return Outcome::Continue;
    }

    bool anyone = false;
    std::string answer;
    std::string lastButton;
};

static FakeHardware* hw;
static std::unique_ptr<Storage> storage;
static std::unique_ptr<TelegramClient> client;
static std::unique_ptr<Bot> bot;
static Questions* questions;
static std::vector<std::string> calls;  // what the registered handlers saw

// A new Bot and client on the same storage, as after a restart.
static void restart() {
    client.reset(new TelegramClient(hw->network, hw->files, hw->log, kTestBotToken));
    bot.reset(new Bot(*client, *storage, hw->settings, hw->clock, hw->log));
    bot->addConversation(*questions);
    bot->addCommand("ask", Roles::kAdmin, "answer some questions",
                    [](Context& ctx, const std::string&) { ctx.startConversation("questions"); });
    bot->addCommand("hello", Roles::kContact, "say hello",
                    [](Context& ctx, const std::string& arg) { calls.push_back("hello " + arg); ctx.reply("Hi"); });
    bot->addButtons("approve", Roles::kAdmin, [](Context& ctx, const std::string& rest) {
        calls.push_back("approve " + rest);
        ctx.popup("Approved");
    });
    bot->setStartLinkHandler([](Context& ctx, const std::string& code) {
        calls.push_back("link " + code);
        if (code != "GOODCODE") return false;
        return ctx.startConversation("questions", {{"from", "invite"}});
    });
}

static void connect() {
    hw->network.responses.push_back(telegramOk(R"({"id":42,"is_bot":true,"first_name":"Box","username":"TestBoxBot"})"));
    TEST_ASSERT_EQUAL(0, bot->step());
    TEST_ASSERT_TRUE(bot->state() == Bot::State::Running);
    hw->network.requests.clear();
}

void setUp() {
    hw = new FakeHardware();
    storage.reset(new Storage(hw->files, hw->settings));
    storage->people.addAdmin(Person{kAdmin, "Parent"});
    storage->people.addAdmin(Person{kEveryRole, "Other parent"});
    Contact granny;
    granny.telegramId = kContact;
    granny.name = "Granny";
    storage->people.addContact(granny);
    Contact parent;
    parent.telegramId = kEveryRole;
    parent.name = "Dad";
    storage->people.addContact(parent);
    storage->people.addMonitor(Person{kMonitor, "Guardian"});
    storage->people.addMonitor(Person{kEveryRole, "Other parent"});
    storage->settings.setChildName("Child");
    questions = new Questions();
    calls.clear();
    restart();
    connect();
}

void tearDown() {
    bot.reset();
    client.reset();
    storage.reset();
    delete questions;
    delete hw;
}

// Polls once with the given updates and handles them. Replies to anything the
// bot sends afterwards aren't queued, so those calls fail quietly; tests look at
// what was asked for, which is still recorded.
static void deliver(const std::string& updatesJson) {
    hw->network.requests.clear();
    hw->network.responses.push_front(telegramOk("[" + updatesJson + "]"));
    bot->step();
}

static long long nextUpdate = 100;

static void sendRaw(long long from, const std::string& content, const std::string& chatType = "private") {
    ++nextUpdate;
    deliver(messageUpdate(nextUpdate, messageJson(nextUpdate, from, content, chatType)));
}

static void say(long long from, const std::string& text) { sendRaw(from, textContent(text)); }

static void press(long long from, const std::string& data) {
    ++nextUpdate;
    deliver(buttonUpdate(nextUpdate, from, 77, data));
}

static JsonDocument body(const HttpRequest& r) {
    JsonDocument doc;
    deserializeJson(doc, r.body);
    return doc;
}

// Text of every message the bot sent during the last deliver().
static std::vector<std::string> replies(long long to = 0) {
    std::vector<std::string> out;
    for (const auto& r : hw->network.requests) {
        if (telegramMethod(r) != "sendMessage") continue;
        JsonDocument b = body(r);
        if (to != 0 && b["chat_id"].as<long long>() != to) continue;
        out.push_back(b["text"].as<std::string>());
    }
    return out;
}

static std::string onlyReply(long long to = 0) {
    auto r = replies(to);
    TEST_ASSERT_EQUAL_MESSAGE(1, r.size(), "expected exactly one reply");
    return r[0];
}

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// The popup text sent when answering the last button press, or "<none>" if it wasn't answered.
static std::string popupText() {
    for (const auto& r : hw->network.requests) {
        if (telegramMethod(r) == "answerCallbackQuery") return body(r)["text"] | "";
    }
    return "<none>";
}

// Only getUpdates was called: the bot didn't answer.
static void assertSilent() {
    for (const auto& r : hw->network.requests) TEST_ASSERT_EQUAL_STRING("getUpdates", telegramMethod(r).c_str());
}

static std::string buttonData(const HttpRequest& r, int row, int col) {
    return body(r)["reply_markup"]["inline_keyboard"][row][col]["callback_data"].as<std::string>();
}

static const HttpRequest& lastSent() {
    for (auto it = hw->network.requests.rbegin(); it != hw->network.requests.rend(); ++it) {
        if (telegramMethod(*it) == "sendMessage") return *it;
    }
    TEST_FAIL_MESSAGE("nothing sent");
    return hw->network.requests.front();
}

// ---------------------------------------------------------------------------
// Connecting and remembering where polling got to

void test_bad_token_stops_the_bot() {
    restart();
    hw->network.responses.push_back(telegramError(401, "Unauthorized"));
    bot->step();
    TEST_ASSERT_TRUE(bot->state() == Bot::State::TokenRejected);
    TEST_ASSERT_TRUE(hw->log.contains("rejected the bot token"));
    TEST_ASSERT_FALSE(hw->log.contains(kTestBotToken));

    hw->network.requests.clear();
    bot->step();
    TEST_ASSERT_EQUAL(0, hw->network.requests.size());  // goes no further
}

void test_checking_the_token_does_not_touch_storage() {
    restart();
    hw->settings.values.clear();
    hw->network.responses.push_back(telegramOk(R"({"id":42,"is_bot":true,"first_name":"Box","username":"TestBoxBot"})"));
    TEST_ASSERT_EQUAL(0, bot->connect());
    TEST_ASSERT_TRUE(bot->state() == Bot::State::Connected);
    TEST_ASSERT_EQUAL(0, hw->settings.values.size());

    bot->resume();
    TEST_ASSERT_TRUE(bot->state() == Bot::State::Running);
    TEST_ASSERT_EQUAL(kBotId, hw->settings.getInt("tg_bot_id", 0));
}

void test_offline_at_start_retries() {
    restart();
    hw->network.responses.push_back(networkDown());
    TEST_ASSERT_EQUAL(Bot::kRetryMs, bot->step());
    TEST_ASSERT_TRUE(bot->state() == Bot::State::NotConnected);
}

void test_position_is_saved_after_each_message() {
    say(kAdmin, "/help");
    TEST_ASSERT_EQUAL(nextUpdate + 1, hw->settings.getInt("tg_offset", 0));
}

void test_messages_are_not_handled_twice_after_a_restart() {
    say(kContact, "/hello one");
    const long long handled = nextUpdate;
    TEST_ASSERT_EQUAL(1, calls.size());

    restart();
    connect();
    deliver("");  // first poll after the restart
    TEST_ASSERT_EQUAL(handled + 1, body(hw->network.requests[0])["offset"].as<long long>());

    // Even if Telegram sent the old one again, it isn't handled again.
    deliver(messageUpdate(handled, messageJson(handled, kContact, textContent("/hello one"))));
    TEST_ASSERT_EQUAL(1, calls.size());
}

void test_whole_batch_is_handled_and_saved() {
    deliver(messageUpdate(201, messageJson(1, kContact, textContent("/hello a"))) + "," +
            messageUpdate(202, messageJson(2, kContact, textContent("/hello b"))));
    TEST_ASSERT_EQUAL(2, calls.size());
    TEST_ASSERT_EQUAL(203, hw->settings.getInt("tg_offset", 0));
}

void test_unreadable_updates_are_moved_past() {
    deliver(R"({"update_id":300,"message":"not a message"})");
    TEST_ASSERT_EQUAL(301, hw->settings.getInt("tg_offset", 0));
}

void test_new_bot_token_starts_from_the_beginning() {
    say(kAdmin, "/help");
    TEST_ASSERT_TRUE(hw->settings.getInt("tg_offset", 0) > 0);

    restart();
    hw->network.responses.push_back(telegramOk(R"({"id":99,"is_bot":true,"first_name":"Box","username":"OtherBot"})"));
    bot->step();
    deliver("");
    TEST_ASSERT_FALSE(body(hw->network.requests[0])["offset"].is<long long>());
    TEST_ASSERT_EQUAL(99, hw->settings.getInt("tg_bot_id", 0));
}

void test_a_message_that_keeps_restarting_the_box_is_skipped() {
    // As if the box restarted twice while handling update 500.
    hw->settings.setString("tg_busy", "500\t2");
    deliver(messageUpdate(500, messageJson(1, kContact, textContent("/hello crash"))));
    TEST_ASSERT_EQUAL(0, calls.size());
    TEST_ASSERT_EQUAL(501, hw->settings.getInt("tg_offset", 0));
    TEST_ASSERT_TRUE(hw->log.contains("Skipped a message"));
}

void test_a_message_is_tried_again_once_after_a_restart() {
    hw->settings.setString("tg_busy", "500\t1");
    deliver(messageUpdate(500, messageJson(1, kContact, textContent("/hello again"))));
    TEST_ASSERT_EQUAL(1, calls.size());
    TEST_ASSERT_EQUAL_STRING("500\t2", hw->settings.getString("tg_busy", "").c_str());
}

void test_telegram_busy_waits_as_asked() {
    hw->network.responses.push_back(telegramError(429, "Too Many Requests", 7));
    TEST_ASSERT_EQUAL(7000, bot->step());
}

// ---------------------------------------------------------------------------
// Who's who

void test_strangers_are_ignored() {
    say(kStranger, "hello");
    assertSilent();
    say(kStranger, "/help");
    assertSilent();
    say(kStranger, "/start");
    assertSilent();
    say(kStranger, "/start WRONGCODE");
    assertSilent();
    sendRaw(kStranger, voiceContent("v1", 3));
    assertSilent();
}

void test_group_chats_are_ignored() {
    sendRaw(kAdmin, textContent("/help"), "group");
    assertSilent();
}

void test_start_greets_known_people() {
    say(kContact, "/start");
    const std::string text = onlyReply(kContact);
    TEST_ASSERT_TRUE(contains(text, "Child's voice box"));
    TEST_ASSERT_TRUE(contains(text, "Send Child a voice note"));
}

void test_help_shows_admins_their_commands() {
    say(kAdmin, "/help");
    const std::string text = onlyReply(kAdmin);
    TEST_ASSERT_TRUE(contains(text, "/ask"));
    TEST_ASSERT_TRUE(contains(text, "/cancel"));
    TEST_ASSERT_FALSE(contains(text, "/hello"));
    TEST_ASSERT_FALSE(contains(text, "/start"));
}

void test_help_shows_contacts_only_their_commands() {
    say(kContact, "/help");
    const std::string text = onlyReply(kContact);
    TEST_ASSERT_TRUE(contains(text, "/hello"));
    TEST_ASSERT_FALSE(contains(text, "/ask"));
}

void test_help_for_monitors() {
    say(kMonitor, "/help");
    const std::string text = onlyReply(kMonitor);
    TEST_ASSERT_TRUE(contains(text, "copies of Child's voice notes"));
    TEST_ASSERT_FALSE(contains(text, "/ask"));
    TEST_ASSERT_FALSE(contains(text, "/hello"));
}

void test_someone_with_every_role_gets_everything() {
    say(kEveryRole, "/help");
    const std::string text = onlyReply(kEveryRole);
    TEST_ASSERT_TRUE(contains(text, "/ask"));
    TEST_ASSERT_TRUE(contains(text, "/hello"));
    TEST_ASSERT_TRUE(contains(text, "copies"));
}

void test_admin_commands_are_turned_down_for_others() {
    say(kContact, "/ask");
    TEST_ASSERT_EQUAL_STRING("Only admins can use /ask.", onlyReply(kContact).c_str());
    TEST_ASSERT_FALSE(questions->allows(Roles::of(storage->people, kContact)));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kContact, state));
}

void test_admin_checked_every_time() {
    say(kEveryRole, "/ask");
    TEST_ASSERT_TRUE(contains(onlyReply(), "Favourite colour"));
    TEST_ASSERT_TRUE(storage->people.removeAdmin(kEveryRole));
    say(kEveryRole, "/ask");
    TEST_ASSERT_EQUAL_STRING("Only admins can use /ask.", onlyReply().c_str());
}

void test_unknown_command() {
    say(kAdmin, "/nonsense");
    TEST_ASSERT_TRUE(contains(onlyReply(), "I don't know /nonsense"));
}

void test_command_with_bot_name_and_argument() {
    say(kContact, "/HELLO@TestBoxBot there");
    TEST_ASSERT_EQUAL(1, calls.size());
    TEST_ASSERT_EQUAL_STRING("hello there", calls[0].c_str());
}

void test_other_messages_from_known_people_point_at_help() {
    say(kContact, "hello");
    TEST_ASSERT_TRUE(contains(onlyReply(), "/help"));
}

void test_message_handler_can_be_replaced() {
    std::string seen;
    bot->setMessageHandler([&](Context& ctx, const Message& m) {
        seen = std::string(toString(m.kind)) + (ctx.sender().roles.isContact() ? " from contact" : "");
    });
    sendRaw(kContact, voiceContent("v1", 3));
    TEST_ASSERT_EQUAL_STRING("voice note from contact", seen.c_str());
    assertSilent();
}

// ---------------------------------------------------------------------------
// Conversations

void test_conversation_runs_to_the_end() {
    say(kAdmin, "/ask");
    TEST_ASSERT_EQUAL_STRING("Favourite colour?", onlyReply().c_str());
    say(kAdmin, "green");
    TEST_ASSERT_EQUAL_STRING("Sure?", onlyReply().c_str());
    const std::string yes = buttonData(lastSent(), 0, 0);
    TEST_ASSERT_TRUE(yes.size() <= TelegramClient::kMaxCallbackDataBytes);

    press(kAdmin, yes);
    TEST_ASSERT_EQUAL_STRING("green/", questions->answer.c_str());
    TEST_ASSERT_EQUAL_STRING("Saved", popupText().c_str());
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kAdmin, state));
}

void test_conversation_survives_a_restart() {
    say(kAdmin, "/ask");
    say(kAdmin, "blue");
    const std::string yes = buttonData(lastSent(), 0, 0);

    restart();
    connect();
    press(kAdmin, yes);
    TEST_ASSERT_EQUAL_STRING("blue/", questions->answer.c_str());
}

void test_each_person_has_their_own_conversation() {
    say(kAdmin, "/ask");
    say(kEveryRole, "/ask");
    say(kAdmin, "red");
    say(kEveryRole, "yellow");
    ConversationState a, b;
    TEST_ASSERT_TRUE(bot->conversations().find(kAdmin, a));
    TEST_ASSERT_TRUE(bot->conversations().find(kEveryRole, b));
    TEST_ASSERT_EQUAL_STRING("red", a.get("colour").c_str());
    TEST_ASSERT_EQUAL_STRING("yellow", b.get("colour").c_str());
}

void test_cancel_stops_a_conversation() {
    say(kAdmin, "/ask");
    say(kAdmin, "/cancel");
    TEST_ASSERT_EQUAL_STRING("Stopped.", onlyReply().c_str());
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kAdmin, state));
    say(kAdmin, "/cancel");
    TEST_ASSERT_EQUAL_STRING("There's nothing to stop.", onlyReply().c_str());
}

void test_another_command_replaces_the_conversation() {
    say(kEveryRole, "/ask");
    say(kEveryRole, "/hello");
    TEST_ASSERT_EQUAL(1, calls.size());
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kEveryRole, state));
}

void test_old_buttons_say_finished() {
    say(kAdmin, "/ask");
    say(kAdmin, "pink");
    const std::string yes = buttonData(lastSent(), 0, 0);
    press(kAdmin, yes);
    questions->lastButton.clear();

    press(kAdmin, yes);
    TEST_ASSERT_EQUAL_STRING("This has finished.", popupText().c_str());
    TEST_ASSERT_TRUE(questions->lastButton.empty());

    // A button from an earlier conversation doesn't reach a newer one.
    say(kAdmin, "/ask");
    press(kAdmin, yes);
    TEST_ASSERT_EQUAL_STRING("This has finished.", popupText().c_str());
    TEST_ASSERT_TRUE(questions->lastButton.empty());
}

void test_losing_the_role_drops_the_conversation() {
    say(kEveryRole, "/ask");
    storage->people.removeAdmin(kEveryRole);
    say(kEveryRole, "green");
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kEveryRole, state));
    TEST_ASSERT_TRUE(contains(onlyReply(), "/help"));  // treated as an ordinary message now
}

void test_conversation_times_out() {
    hw->clock.timeSet = true;
    hw->clock.unixSeconds = 1800000000;
    say(kAdmin, "/ask");
    hw->clock.unixSeconds += Bot::kConversationTimeoutSeconds + 1;
    say(kAdmin, "green");
    TEST_ASSERT_TRUE(contains(onlyReply(), "/help"));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kAdmin, state));
}

void test_conversation_started_before_the_clock_was_set_does_not_time_out() {
    say(kAdmin, "/ask");
    hw->clock.timeSet = true;
    hw->clock.unixSeconds = 1800000000;
    say(kAdmin, "green");
    TEST_ASSERT_EQUAL_STRING("Sure?", onlyReply().c_str());
}

void test_conversation_of_a_removed_kind_is_dropped() {
    ConversationState old;
    old.id = 1;
    old.type = "gone";
    old.userId = kAdmin;
    bot->conversations().save(old);
    say(kAdmin, "hello");
    TEST_ASSERT_TRUE(contains(onlyReply(), "/help"));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kAdmin, state));
}

// ---------------------------------------------------------------------------
// Invite links

void test_stranger_with_a_good_link_gets_a_conversation() {
    questions->anyone = true;
    say(kStranger, "/start GOODCODE");
    TEST_ASSERT_EQUAL_STRING("Favourite colour?", onlyReply(kStranger).c_str());

    // From now on they're answered, but only within the conversation.
    say(kStranger, "orange");
    TEST_ASSERT_EQUAL_STRING("Sure?", onlyReply(kStranger).c_str());
    const std::string yes = buttonData(lastSent(), 0, 0);
    say(kStranger, "/help");  // not on any list, so goes to the conversation
    TEST_ASSERT_EQUAL_STRING("Press a button", onlyReply(kStranger).c_str());
    press(kStranger, yes);
    TEST_ASSERT_EQUAL_STRING("orange/invite", questions->answer.c_str());

    // Once it's over they're a stranger again.
    say(kStranger, "hello");
    assertSilent();
}

void test_stranger_with_a_bad_link_is_ignored() {
    say(kStranger, "/start BADCODE");
    TEST_ASSERT_EQUAL(1, calls.size());
    TEST_ASSERT_EQUAL_STRING("link BADCODE", calls[0].c_str());
    assertSilent();
}

void test_conversation_that_needs_a_role_cannot_start_for_a_stranger() {
    say(kStranger, "/start GOODCODE");  // questions->anyone is false
    assertSilent();
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kStranger, state));
}

void test_known_person_with_an_unknown_link_is_greeted() {
    say(kContact, "/start BADCODE");
    TEST_ASSERT_TRUE(contains(onlyReply(), "Child's voice box"));
}

// ---------------------------------------------------------------------------
// Buttons outside conversations

void test_button_handler_for_admins() {
    press(kAdmin, "approve:7");
    TEST_ASSERT_EQUAL(1, calls.size());
    TEST_ASSERT_EQUAL_STRING("approve 7", calls[0].c_str());
    TEST_ASSERT_EQUAL_STRING("Approved", popupText().c_str());
}

void test_button_handler_turns_down_others() {
    press(kContact, "approve:7");
    TEST_ASSERT_EQUAL(0, calls.size());
    TEST_ASSERT_EQUAL_STRING("Only admins can do that.", popupText().c_str());
}

void test_strangers_buttons_are_ignored() {
    press(kStranger, "approve:7");
    TEST_ASSERT_EQUAL(0, calls.size());
    assertSilent();
}

void test_unknown_button() {
    press(kAdmin, "nothing:1");
    TEST_ASSERT_EQUAL_STRING("This button no longer works.", popupText().c_str());
}

// ---------------------------------------------------------------------------
// The conversation store on its own

void test_store_round_trips_awkward_text() {
    FakeSettingsStore store;
    ConversationStore conversations(store);
    ConversationState s;
    s.id = conversations.nextId();
    s.type = "questions";
    s.step = "name";
    s.userId = 123;
    s.lastActive = 5;
    s.set("name", "Gran\tny\nwith \\ odd bits");
    s.set("empty", "");
    TEST_ASSERT_TRUE(conversations.save(s));

    ConversationState back;
    TEST_ASSERT_TRUE(conversations.find(123, back));
    TEST_ASSERT_EQUAL(s.id, back.id);
    TEST_ASSERT_EQUAL_STRING("name", back.step.c_str());
    TEST_ASSERT_EQUAL(5, back.lastActive);
    TEST_ASSERT_EQUAL_STRING("Gran\tny\nwith \\ odd bits", back.get("name").c_str());
    TEST_ASSERT_TRUE(back.data.count("empty") == 1);
}

void test_store_ids_are_never_reused() {
    FakeSettingsStore store;
    ConversationStore a(store);
    const uint32_t first = a.nextId();
    ConversationStore b(store);
    TEST_ASSERT_EQUAL(first + 1, b.nextId());
}

void test_store_makes_room_by_dropping_the_quietest() {
    FakeSettingsStore store;
    ConversationStore conversations(store);
    for (int i = 1; i <= 10; ++i) {
        ConversationState s;
        s.id = static_cast<uint32_t>(i);
        s.type = "questions";
        s.userId = i;
        s.lastActive = 1000 + i;
        s.set("filler", std::string(500, 'x'));
        TEST_ASSERT_TRUE(conversations.save(s));
    }
    const auto all = conversations.all();
    TEST_ASSERT_TRUE(all.size() < 10);
    ConversationState s;
    TEST_ASSERT_FALSE(conversations.find(1, s));  // the quietest went first
    TEST_ASSERT_TRUE(conversations.find(10, s));  // the newest is always kept
}

void test_store_refuses_one_that_is_too_big() {
    FakeSettingsStore store;
    ConversationStore conversations(store);
    ConversationState s;
    s.id = 1;
    s.type = "questions";
    s.userId = 1;
    s.set("filler", std::string(5000, 'x'));
    TEST_ASSERT_FALSE(conversations.save(s));
}

void test_store_removes_expired() {
    FakeSettingsStore store;
    ConversationStore conversations(store);
    ConversationState s;
    s.type = "questions";
    s.id = 1; s.userId = 1; s.lastActive = 100;
    conversations.save(s);
    s.id = 2; s.userId = 2; s.lastActive = 0;  // clock wasn't set
    conversations.save(s);
    s.id = 3; s.userId = 3; s.lastActive = 950;
    conversations.save(s);

    TEST_ASSERT_EQUAL(0, conversations.removeExpired(0, 500));  // clock not set: nothing
    TEST_ASSERT_EQUAL(1, conversations.removeExpired(1000, 500));
    TEST_ASSERT_EQUAL(2, conversations.all().size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bad_token_stops_the_bot);
    RUN_TEST(test_checking_the_token_does_not_touch_storage);
    RUN_TEST(test_offline_at_start_retries);
    RUN_TEST(test_position_is_saved_after_each_message);
    RUN_TEST(test_messages_are_not_handled_twice_after_a_restart);
    RUN_TEST(test_whole_batch_is_handled_and_saved);
    RUN_TEST(test_unreadable_updates_are_moved_past);
    RUN_TEST(test_new_bot_token_starts_from_the_beginning);
    RUN_TEST(test_a_message_that_keeps_restarting_the_box_is_skipped);
    RUN_TEST(test_a_message_is_tried_again_once_after_a_restart);
    RUN_TEST(test_telegram_busy_waits_as_asked);
    RUN_TEST(test_strangers_are_ignored);
    RUN_TEST(test_group_chats_are_ignored);
    RUN_TEST(test_start_greets_known_people);
    RUN_TEST(test_help_shows_admins_their_commands);
    RUN_TEST(test_help_shows_contacts_only_their_commands);
    RUN_TEST(test_help_for_monitors);
    RUN_TEST(test_someone_with_every_role_gets_everything);
    RUN_TEST(test_admin_commands_are_turned_down_for_others);
    RUN_TEST(test_admin_checked_every_time);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_command_with_bot_name_and_argument);
    RUN_TEST(test_other_messages_from_known_people_point_at_help);
    RUN_TEST(test_message_handler_can_be_replaced);
    RUN_TEST(test_conversation_runs_to_the_end);
    RUN_TEST(test_conversation_survives_a_restart);
    RUN_TEST(test_each_person_has_their_own_conversation);
    RUN_TEST(test_cancel_stops_a_conversation);
    RUN_TEST(test_another_command_replaces_the_conversation);
    RUN_TEST(test_old_buttons_say_finished);
    RUN_TEST(test_losing_the_role_drops_the_conversation);
    RUN_TEST(test_conversation_times_out);
    RUN_TEST(test_conversation_started_before_the_clock_was_set_does_not_time_out);
    RUN_TEST(test_conversation_of_a_removed_kind_is_dropped);
    RUN_TEST(test_stranger_with_a_good_link_gets_a_conversation);
    RUN_TEST(test_stranger_with_a_bad_link_is_ignored);
    RUN_TEST(test_conversation_that_needs_a_role_cannot_start_for_a_stranger);
    RUN_TEST(test_known_person_with_an_unknown_link_is_greeted);
    RUN_TEST(test_button_handler_for_admins);
    RUN_TEST(test_button_handler_turns_down_others);
    RUN_TEST(test_strangers_buttons_are_ignored);
    RUN_TEST(test_unknown_button);
    RUN_TEST(test_store_round_trips_awkward_text);
    RUN_TEST(test_store_ids_are_never_reused);
    RUN_TEST(test_store_makes_room_by_dropping_the_quietest);
    RUN_TEST(test_store_refuses_one_that_is_too_big);
    RUN_TEST(test_store_removes_expired);
    return UNITY_END();
}

// Admin settings, /status, /trash getlast and the moved-country question
// (ticket 1.6), against a pretend Telegram.

#include <ArduinoJson.h>
#include <unity.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "app/bot/Bot.h"
#include "app/invites/InviteFlows.h"
#include "app/settings/AdminSettings.h"
#include "app/settings/MovedCountry.h"
#include "app/time/TimeZones.h"
#include "fakes/FakeRandom.h"
#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;
using namespace fvb::telegram;

static const long long kAdmin = 1001;
static const long long kOtherAdmin = 1002;
static const long long kContact = 2002;
static const long long kMonitor = 3003;
static const int64_t kNow = 1790000000;  // Monday 21 September 2026, 14:13 UTC

// Answers each Bot API method the way Telegram would.
class PretendTelegram : public Network {
public:
    bool isConnected() const override { return true; }
    int8_t signalStrength() const override { return -65; }

    HttpResponse request(const HttpRequest& req) override {
        requests.push_back(req);
        const std::string method = telegramMethod(req);
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
        if (method == "sendVoice") {
            if (failVoice) return telegramError(500, "Internal Server Error");
            return telegramOk(R"({"message_id":900,"chat":{"id":1001,"type":"private"},"date":1790000000,)" +
                              voiceContent("v1", 3) + "}");
        }
        if (method == "editMessageText") return telegramOk(R"({"message_id":1,"chat":{"id":1,"type":"private"},"date":1,"text":"x"})");
        if (method == "answerCallbackQuery") return telegramOk("true");
        return telegramError(400, "Bad Request: not pretended");
    }

    std::vector<HttpRequest> requests;
    std::deque<std::string> batches;
    long long lastMessageId = 500;
    bool failVoice = false;
};

static FakeHardware* hw;
static PretendTelegram* net;
static FakeRandom* rng;
static std::unique_ptr<Storage> storage;
static std::unique_ptr<TelegramClient> client;
static std::unique_ptr<Bot> bot;
static std::unique_ptr<InviteFlows> flows;
static std::unique_ptr<AdminSettings> settings;
static std::unique_ptr<MovedCountry> moved;
static long long nextUpdate = 100;
static uint32_t grannyNumber = 0;

// Everything on the same storage again, as after a restart.
static void restart() {
    moved.reset();
    settings.reset();
    flows.reset();
    bot.reset();
    client.reset(new TelegramClient(*net, hw->files, hw->log, kTestBotToken));
    bot.reset(new Bot(*client, *storage, hw->settings, hw->clock, hw->log));
    flows.reset(new InviteFlows(*bot, *storage, hw->settings, *rng));
    settings.reset(new AdminSettings(*bot, *storage, *flows, hw->files, *net, hw->power));
    moved.reset(new MovedCountry(*bot, *client, *storage, hw->settings, hw->log));
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
    storage->people.addAdmin(Person{kOtherAdmin, "Alex"});
    storage->people.addMonitor(Person{kMonitor, "Pat"});
    Contact granny;
    granny.telegramId = kContact;
    granny.name = "Granny";
    granny.timezone = "Europe/Paris";
    grannyNumber = storage->people.addContact(granny);
    restart();
}

void tearDown() {
    moved.reset();
    settings.reset();
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

// fakes::userJson calls everyone "Granny"; give the admins their own names.
static std::string renamed(std::string json, long long who) {
    const std::string name = who == kAdmin ? R"("first_name":"Parent")"
                             : who == kOtherAdmin ? R"("first_name":"Alex")"
                                                  : R"("first_name":"Granny")";
    const std::string original = R"("first_name":"Granny")";
    for (size_t at = json.find(original); at != std::string::npos; at = json.find(original, at + name.size())) {
        json.replace(at, original.size(), name);
    }
    return json;
}

static void say(long long from, const std::string& text) {
    ++nextUpdate;
    deliver(renamed(messageUpdate(nextUpdate, messageJson(nextUpdate, from, textContent(text))), from));
}

static void press(long long from, const std::string& data) {
    ++nextUpdate;
    deliver(renamed(buttonUpdate(nextUpdate, from, 77, data), from));
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

static std::string lastEdit(long long to = 0) {
    auto e = sent("editMessageText", to);
    TEST_ASSERT_FALSE_MESSAGE(e.empty(), "expected a message to be changed");
    return body(e.back())["text"].as<std::string>();
}

static std::string popupText() {
    auto a = sent("answerCallbackQuery");
    if (a.empty()) return "<none>";
    return body(a.back())["text"] | "";
}

// The text of whatever the bot last sent or changed.
static std::string shown() {
    for (auto it = net->requests.rbegin(); it != net->requests.rend(); ++it) {
        const std::string m = telegramMethod(*it);
        if (m == "sendMessage" || m == "editMessageText") return body(*it)["text"].as<std::string>();
    }
    TEST_FAIL_MESSAGE("nothing was shown");
    return "";
}

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

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

static bool hasButton(const std::string& label) {
    for (const auto& b : lastButtons()) {
        if (b.first == label) return true;
    }
    return false;
}

static std::string buttonNamed(const std::string& label) {
    for (const auto& b : lastButtons()) {
        if (b.first == label) return b.second;
    }
    TEST_FAIL_MESSAGE(("no button called " + label).c_str());
    return "";
}

static void tap(long long from, const std::string& label) { press(from, buttonNamed(label)); }

static LocationGuess guessFor(const char* country, const char* zone = nullptr) {
    LocationGuess g;
    g.ok = true;
    g.country = timezones::findCountry(country);
    g.zone = zone ? timezones::find(zone) : nullptr;
    return g;
}

// ---------------------------------------------------------------------------
// The menu

void test_settings_menu_is_for_admins_only() {
    say(kAdmin, "/settings");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Settings for Child's box"));
    for (const char* label : {"Contacts", "Monitors", "Admins", "Quiet hours", "Time zone", "Voice volume",
                              "Alert volume", "Max recording", "Keep messages", "Child's name", "Status",
                              "Last deleted", "Done"}) {
        TEST_ASSERT_TRUE_MESSAGE(hasButton(label), label);
    }

    say(kContact, "/settings");
    TEST_ASSERT_EQUAL_STRING("Only admins can use /settings.", lastReply(kContact).c_str());
    say(kContact, "/volume 10");
    TEST_ASSERT_EQUAL_STRING("Only admins can use /volume.", lastReply(kContact).c_str());
    TEST_ASSERT_EQUAL(70, storage->settings.voiceVolume());
}

void test_settings_commands_are_in_admin_help_only() {
    say(kAdmin, "/help");
    const std::string help = lastReply(kAdmin);
    for (const char* c : {"/settings", "/status", "/trash", "/quiethours", "/timezone", "/volume", "/alertvolume",
                          "/maxrecording", "/keepmessages", "/childname", "/contacts", "/monitors", "/admins"}) {
        TEST_ASSERT_TRUE_MESSAGE(contains(help, c), c);
    }
    say(kContact, "/help");
    TEST_ASSERT_FALSE(contains(lastReply(kContact), "/settings"));
}

void test_changing_voice_volume_from_the_menu() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Voice volume");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Voice notes play at 70%"));
    TEST_ASSERT_TRUE(hasButton("\xE2\x9C\x93 70%"));  // the current level is ticked
    TEST_ASSERT_FALSE(hasButton("0%"));               // the child would hear nothing
    tap(kAdmin, "60%");
    TEST_ASSERT_EQUAL(60, storage->settings.voiceVolume());
    TEST_ASSERT_TRUE(contains(lastEdit(), "Voice volume is now 60%."));
    TEST_ASSERT_TRUE(hasButton("Voice volume"));  // back on the main page

    tap(kAdmin, "Done");
    TEST_ASSERT_EQUAL_STRING("Settings closed.", lastEdit().c_str());
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kAdmin, state));
}

void test_back_goes_to_the_main_page() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Alert volume");
    tap(kAdmin, "\xE2\x86\x90 Back");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Settings for Child's box"));
}

void test_direct_commands_change_settings() {
    say(kAdmin, "/volume 40");
    TEST_ASSERT_EQUAL_STRING("Voice volume is now 40%.", lastReply(kAdmin).c_str());
    TEST_ASSERT_EQUAL(40, storage->settings.voiceVolume());
    say(kAdmin, "/volume 5");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "from 10 to 100"));
    TEST_ASSERT_EQUAL(40, storage->settings.voiceVolume());

    say(kAdmin, "/alertvolume 0%");
    TEST_ASSERT_EQUAL(0, storage->settings.alertVolume());
    say(kAdmin, "/alertvolume loud");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "from 0 to 100"));

    say(kAdmin, "/maxrecording 120");
    TEST_ASSERT_EQUAL_STRING("The longest voice note is now 2 minutes.", lastReply(kAdmin).c_str());
    say(kAdmin, "/maxrecording 5");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "from 10 to 900"));
    TEST_ASSERT_EQUAL(120, storage->settings.maxRecordingSeconds());

    say(kAdmin, "/keepmessages 0");
    TEST_ASSERT_EQUAL_STRING("Voice notes are now kept on the box for ever.", lastReply(kAdmin).c_str());
    TEST_ASSERT_EQUAL(0, storage->settings.keepDays());
    say(kAdmin, "/keepmessages 4000");
    TEST_ASSERT_EQUAL(0, storage->settings.keepDays());

    say(kAdmin, "/quiethours 21:00 06:30");
    TEST_ASSERT_EQUAL_STRING("Quiet hours are now 9pm to 6:30am.", lastReply(kAdmin).c_str());
    TEST_ASSERT_EQUAL(21 * 60, storage->settings.quietStart());
    TEST_ASSERT_EQUAL(6 * 60 + 30, storage->settings.quietEnd());
    say(kAdmin, "/quiethours 7pm to 7am");
    TEST_ASSERT_EQUAL(19 * 60, storage->settings.quietStart());
    say(kAdmin, "/quiethours off");
    TEST_ASSERT_EQUAL_STRING("Quiet hours are now off.", lastReply(kAdmin).c_str());
    TEST_ASSERT_EQUAL(storage->settings.quietStart(), storage->settings.quietEnd());
    say(kAdmin, "/quiethours bedtime");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "/quiethours 19:00 07:00"));

    say(kAdmin, "/childname Sam");
    TEST_ASSERT_EQUAL_STRING("The bot now calls the child Sam.", lastReply(kAdmin).c_str());
    TEST_ASSERT_EQUAL_STRING("Sam", storage->settings.childName().c_str());

    say(kAdmin, "/monitors all");
    TEST_ASSERT_TRUE(storage->settings.monitorMode() == MonitorMode::All);
    say(kAdmin, "/monitors sometimes");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "/monitors off"));
    TEST_ASSERT_TRUE(storage->settings.monitorMode() == MonitorMode::All);
}

void test_direct_command_without_a_value_opens_that_page() {
    say(kAdmin, "/maxrecording");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "The longest voice note Child can record is 6 minutes"));
    say(kAdmin, "90");  // typed instead of tapped
    TEST_ASSERT_EQUAL(90, storage->settings.maxRecordingSeconds());
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "1 minute 30 seconds"));
}

void test_quiet_hours_with_the_hour_pickers() {
    say(kAdmin, "/quiethours");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Quiet hours: 7pm to 7am"));
    tap(kAdmin, "Change");
    TEST_ASSERT_EQUAL_STRING("When should quiet hours start?", lastEdit().c_str());
    tap(kAdmin, "8pm");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Quiet hours start at 8pm. When should they end?"));
    tap(kAdmin, "6am");
    TEST_ASSERT_EQUAL(20 * 60, storage->settings.quietStart());
    TEST_ASSERT_EQUAL(6 * 60, storage->settings.quietEnd());
    TEST_ASSERT_TRUE(contains(lastEdit(), "Quiet hours are now 8pm to 6am."));

    // The same start and end turns them off.
    tap(kAdmin, "Quiet hours");
    tap(kAdmin, "Change");
    tap(kAdmin, "9pm");
    tap(kAdmin, "9pm");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Quiet hours are now off."));
    TEST_ASSERT_EQUAL(storage->settings.quietStart(), storage->settings.quietEnd());

    tap(kAdmin, "Quiet hours");
    TEST_ASSERT_EQUAL_STRING("Quiet hours are off.", lastEdit().c_str());
    TEST_ASSERT_FALSE(hasButton("Turn off"));
}

void test_turning_quiet_hours_off() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Quiet hours");
    tap(kAdmin, "Turn off");
    TEST_ASSERT_EQUAL(storage->settings.quietStart(), storage->settings.quietEnd());
}

void test_keep_messages_and_monitor_mode_from_the_menu() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Keep messages");
    TEST_ASSERT_TRUE(contains(lastEdit(), "removed from the box after 60 days"));
    tap(kAdmin, "1 year");
    TEST_ASSERT_EQUAL(365, storage->settings.keepDays());

    tap(kAdmin, "Monitors");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Monitoring: Off."));
    TEST_ASSERT_TRUE(contains(lastEdit(), "Pat"));
    tap(kAdmin, "Change what monitors get");
    tap(kAdmin, "Received only");
    TEST_ASSERT_TRUE(storage->settings.monitorMode() == MonitorMode::ReceivedOnly);
    TEST_ASSERT_TRUE(contains(lastEdit(), "Monitors now get copies of voice notes Child receives."));
}

void test_changing_the_childs_name() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Child's name");
    say(kAdmin, "A name that is far too long to be sensible here");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "a bit long"));
    TEST_ASSERT_EQUAL_STRING("Child", storage->settings.childName().c_str());
    say(kAdmin, "Robin");
    TEST_ASSERT_EQUAL_STRING("Robin", storage->settings.childName().c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "The bot now calls the child Robin."));
}

void test_changing_the_time_zone_from_the_menu() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Time zone");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Time zone: United Kingdom."));
    TEST_ASSERT_TRUE(contains(lastEdit(), "The time there now is 15:13."));  // BST in September
    tap(kAdmin, "Change");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Which country is the box in?"));
    // The box's own country and the contacts' countries come first.
    TEST_ASSERT_EQUAL_STRING("United Kingdom", lastButtons()[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("France", lastButtons()[1].first.c_str());
    tap(kAdmin, "France");
    TEST_ASSERT_EQUAL_STRING("Europe/Paris", storage->settings.timezone().c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "The box is now on France time."));
}

void test_timezone_command_with_a_country_with_several_zones() {
    say(kAdmin, "/timezone United States");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Which city has the same time as you?"));
    tap(kAdmin, "Chicago");
    TEST_ASSERT_EQUAL_STRING("America/Chicago", storage->settings.timezone().c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "United States (Chicago) time"));
}

void test_menu_carries_on_after_a_restart() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Child's name");
    restart();
    say(kAdmin, "Robin");
    TEST_ASSERT_EQUAL_STRING("Robin", storage->settings.childName().c_str());
}

// ---------------------------------------------------------------------------
// People

void test_removing_a_contact_needs_confirming() {
    hw->files.writeFile("/contacts/" + std::to_string(grannyNumber) + "/photo.jpg", "JPEG");
    say(kAdmin, "/contacts");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Granny" " \xE2\x80\x93 France"));
    tap(kAdmin, "Remove a contact");
    tap(kAdmin, "Granny");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Remove Granny from Child's box?"));
    tap(kAdmin, "No");
    TEST_ASSERT_TRUE(storage->people.isContact(kContact));

    tap(kAdmin, "Remove a contact");
    tap(kAdmin, "Granny");
    tap(kAdmin, "Yes, remove");
    TEST_ASSERT_FALSE(storage->people.isContact(kContact));
    TEST_ASSERT_FALSE(storage->hasContactPhoto(grannyNumber));
    TEST_ASSERT_TRUE(contains(lastEdit(), "Granny has been removed from Child's box."));
}

void test_removing_a_monitor() {
    say(kAdmin, "/monitors");
    tap(kAdmin, "Remove a monitor");
    tap(kAdmin, "Pat");
    tap(kAdmin, "Yes, remove");
    TEST_ASSERT_FALSE(storage->people.isMonitor(kMonitor));
    TEST_ASSERT_TRUE(contains(lastEdit(), "Pat is no longer a monitor."));
}

void test_removing_an_admin_tells_everyone() {
    storage->people.addAdmin(Person{4004, "Sam"});
    say(kAdmin, "/admins");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Parent (you)"));
    tap(kAdmin, "Remove an admin");
    tap(kAdmin, "Alex");
    TEST_ASSERT_TRUE(contains(lastEdit(), "Remove Alex as an admin?"));
    tap(kAdmin, "Yes, remove");
    TEST_ASSERT_FALSE(storage->people.isAdmin(kOtherAdmin));
    TEST_ASSERT_EQUAL_STRING("Parent removed you as an admin of Child's box.", lastReply(kOtherAdmin).c_str());
    TEST_ASSERT_EQUAL_STRING("Parent removed Alex as an admin of Child's box.", lastReply(4004).c_str());
    TEST_ASSERT_TRUE(contains(lastEdit(), "Alex is no longer an admin."));
}

void test_admin_removing_themselves() {
    say(kAdmin, "/admins");
    tap(kAdmin, "Remove an admin");
    tap(kAdmin, "Parent (you)");
    tap(kAdmin, "Yes, remove");
    TEST_ASSERT_FALSE(storage->people.isAdmin(kAdmin));
    TEST_ASSERT_EQUAL_STRING("You're no longer an admin of Child's box.", lastEdit().c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kOtherAdmin), "Parent is no longer an admin of Child's box."));
    ConversationState state;
    TEST_ASSERT_FALSE(bot->conversations().find(kAdmin, state));
}

void test_last_admin_cannot_be_removed() {
    storage->people.removeAdmin(kOtherAdmin);
    say(kAdmin, "/admins");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "last one can't be removed"));
    TEST_ASSERT_FALSE(hasButton("Remove an admin"));
    // Even with a button from an older message in the same menu.
    std::string old = buttonNamed("Invite an admin");  // "#<conversation>:inv:a"
    old.replace(old.find("inv:a"), 5, "ra:" + std::to_string(kAdmin));
    press(kAdmin, old);
    tap(kAdmin, "Yes, remove");
    TEST_ASSERT_TRUE(storage->people.isAdmin(kAdmin));
    TEST_ASSERT_TRUE(contains(lastEdit(), "needs at least one admin"));
}

void test_inviting_from_the_menu() {
    say(kAdmin, "/settings");
    tap(kAdmin, "Contacts");
    tap(kAdmin, "Invite a contact");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "https://t.me/TestBoxBot?start="));
    TEST_ASSERT_EQUAL(1, flows->invites().all().size());
    TEST_ASSERT_TRUE(flows->invites().all()[0].role == InviteRole::Contact);
}

// ---------------------------------------------------------------------------
// /status and /trash getlast

void test_status() {
    hw->power.battery = 85;
    hw->power.charging = true;
    storage->outbox.add(grannyNumber, 1, kNow);
    say(kAdmin, "/status");
    const std::string s = lastReply(kAdmin);
    TEST_ASSERT_TRUE_MESSAGE(contains(s, "Battery: 85%, charging"), s.c_str());
    TEST_ASSERT_TRUE(contains(s, "Wi-Fi: good (-65 dBm)"));
    TEST_ASSERT_TRUE(contains(s, "MB of 17,179 MB used (0%)"));
    TEST_ASSERT_TRUE(contains(s, "Waiting to send: 1 voice notes"));
    TEST_ASSERT_TRUE(contains(s, "Firmware: "));

    hw->power.battery = -1;
    hw->files.mounted = false;
    TEST_ASSERT_TRUE(contains(settings->statusText(), "Battery: can't be read"));
    TEST_ASSERT_TRUE(contains(settings->statusText(), "SD card not found"));
}

void test_trash_getlast_sends_the_newest_recording() {
    say(kAdmin, "/trash getlast");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "nothing in the bin"));

    hw->files.writeFile("/rec1.mp3", "OLD");
    storage->binRecording(grannyNumber, "/rec1.mp3", kNow - 3600);
    hw->files.writeFile("/rec2.mp3", "NEW");
    storage->binRecording(grannyNumber, "/rec2.mp3", kNow);

    say(kAdmin, "/trash getlast");
    auto voice = sent("sendVoice");
    TEST_ASSERT_EQUAL(1, voice.size());
    TEST_ASSERT_TRUE(contains(voice[0].body, "NEW"));
    TEST_ASSERT_FALSE(contains(voice[0].body, "OLD"));
    TEST_ASSERT_TRUE(contains(voice[0].body, "Recorded for Granny. Deleted Monday 21 September at 15:13."));

    say(kAdmin, "/trash");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "/trash getlast"));

    net->failVoice = true;
    say(kAdmin, "/trash getlast");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "couldn't send it"));
}

void test_last_deleted_from_the_menu() {
    hw->files.writeFile("/rec.mp3", "AUDIO");
    storage->binRecording(grannyNumber, "/rec.mp3", kNow);
    say(kAdmin, "/settings");
    tap(kAdmin, "Last deleted");
    TEST_ASSERT_EQUAL(1, sent("sendVoice").size());
}

// ---------------------------------------------------------------------------
// The moved-country question

void test_only_checks_on_a_new_network_once_the_time_zone_is_set() {
    TEST_ASSERT_FALSE(moved->shouldCheck(""));
    TEST_ASSERT_TRUE(moved->shouldCheck("Home"));
    moved->check("Home", guessFor("GB", "Europe/London"));
    TEST_ASSERT_FALSE(moved->shouldCheck("Home"));
    TEST_ASSERT_TRUE(moved->shouldCheck("Holiday flat"));

    hw->settings.remove("timezone");
    TEST_ASSERT_FALSE(moved->shouldCheck("Holiday flat"));  // setup hasn't set it yet
}

void test_a_failed_location_check_is_tried_again_next_time() {
    LocationGuess failed;
    failed.error = "timed out";
    net->requests.clear();
    TEST_ASSERT_FALSE(moved->check("Holiday flat", failed));
    TEST_ASSERT_TRUE(moved->shouldCheck("Holiday flat"));
    TEST_ASSERT_EQUAL(0, sent("sendMessage").size());
}

void test_same_country_asks_nothing() {
    net->requests.clear();
    TEST_ASSERT_FALSE(moved->check("Granny's house", guessFor("GB", "Europe/London")));
    TEST_ASSERT_EQUAL(0, sent("sendMessage").size());
}

void test_moving_country_asks_admins_and_yes_switches() {
    net->requests.clear();
    TEST_ASSERT_TRUE(moved->check("Holiday flat", guessFor("FR", "Europe/Paris")));
    for (long long admin : {kAdmin, kOtherAdmin}) {
        const std::string ask = lastReply(admin);
        TEST_ASSERT_TRUE_MESSAGE(contains(ask, "Looks like Child's box is now in France. Switch to France time?"),
                                 ask.c_str());
        TEST_ASSERT_TRUE(contains(ask, "At the moment it's on United Kingdom time."));
    }
    TEST_ASSERT_EQUAL(0, replies(kContact).size());
    TEST_ASSERT_EQUAL_STRING("FR", moved->askingAbout().c_str());

    tap(kOtherAdmin, "Yes");
    TEST_ASSERT_EQUAL_STRING("Europe/Paris", storage->settings.timezone().c_str());
    TEST_ASSERT_EQUAL_STRING("Switched to France time.", lastEdit(kOtherAdmin).c_str());
    TEST_ASSERT_EQUAL_STRING("Alex switched the box to France time.", lastEdit(kAdmin).c_str());
    TEST_ASSERT_EQUAL_STRING("", moved->askingAbout().c_str());

    // The other admin's button no longer does anything.
    press(kAdmin, "moved:FR:n");
    TEST_ASSERT_EQUAL_STRING("This has already been answered.", popupText().c_str());
    TEST_ASSERT_EQUAL_STRING("Europe/Paris", storage->settings.timezone().c_str());
}

void test_no_means_never_ask_about_that_country_again() {
    moved->check("Holiday flat", guessFor("FR", "Europe/Paris"));
    tap(kAdmin, "No");
    TEST_ASSERT_EQUAL_STRING("Europe/London", storage->settings.timezone().c_str());
    TEST_ASSERT_TRUE(contains(lastEdit(kAdmin), "Kept United Kingdom time. I won't ask about France again."));
    TEST_ASSERT_TRUE(contains(lastEdit(kOtherAdmin), "Parent chose to keep United Kingdom time."));
    TEST_ASSERT_EQUAL(1, moved->declinedCountries().size());

    net->requests.clear();
    TEST_ASSERT_FALSE(moved->check("Another flat", guessFor("FR", "Europe/Paris")));
    TEST_ASSERT_EQUAL(0, sent("sendMessage").size());

    // A different country is still asked about, even after a restart.
    restart();
    net->requests.clear();
    TEST_ASSERT_TRUE(moved->check("Airport", guessFor("ES", "Europe/Madrid")));
}

void test_country_with_several_zones_asks_for_a_city() {
    TEST_ASSERT_TRUE(moved->check("Holiday flat", guessFor("US")));
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Switch to United States time? You'll be asked which city is nearest."));
    tap(kAdmin, "Yes");
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Which city has the same time as you?"));
    TEST_ASSERT_TRUE(contains(lastEdit(kOtherAdmin), "Parent is choosing the box's new time zone."));
    tap(kAdmin, "Denver");
    TEST_ASSERT_EQUAL_STRING("America/Denver", storage->settings.timezone().c_str());
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "The box is now on United States (Denver) time."));
}

void test_suggested_city_is_used_when_known() {
    moved->check("Holiday flat", guessFor("US", "America/Chicago"));
    TEST_ASSERT_TRUE(contains(lastReply(kAdmin), "Switch to United States (Chicago) time?"));
    tap(kAdmin, "Yes");
    TEST_ASSERT_EQUAL_STRING("America/Chicago", storage->settings.timezone().c_str());
}

void test_going_home_before_anyone_answers() {
    moved->check("Holiday flat", guessFor("FR", "Europe/Paris"));
    net->requests.clear();
    TEST_ASSERT_FALSE(moved->check("Home", guessFor("GB", "Europe/London")));
    TEST_ASSERT_TRUE(contains(lastEdit(kAdmin), "back in United Kingdom"));
    TEST_ASSERT_EQUAL_STRING("", moved->askingAbout().c_str());
}

void test_moving_again_before_anyone_answers() {
    moved->check("Holiday flat", guessFor("FR", "Europe/Paris"));
    net->requests.clear();
    TEST_ASSERT_TRUE(moved->check("Next flat", guessFor("ES", "Europe/Madrid")));
    TEST_ASSERT_TRUE(contains(lastEdit(kAdmin), "no longer applies"));
    TEST_ASSERT_EQUAL_STRING("ES", moved->askingAbout().c_str());

    // Asking about the same place twice sends nothing new.
    net->requests.clear();
    TEST_ASSERT_FALSE(moved->check("Yet another flat", guessFor("ES", "Europe/Madrid")));
    TEST_ASSERT_EQUAL(0, sent("sendMessage").size());
}

void test_only_admins_can_answer() {
    moved->check("Holiday flat", guessFor("FR", "Europe/Paris"));
    press(kContact, "moved:FR:y");
    TEST_ASSERT_EQUAL_STRING("Only admins can do that.", popupText().c_str());
    TEST_ASSERT_EQUAL_STRING("Europe/London", storage->settings.timezone().c_str());
}

// ---------------------------------------------------------------------------
// Small helpers

void test_reading_typed_times() {
    uint16_t m = 0;
    TEST_ASSERT_TRUE(parseTimeOfDay("19:00", m));
    TEST_ASSERT_EQUAL(19 * 60, m);
    TEST_ASSERT_TRUE(parseTimeOfDay("7:30", m));
    TEST_ASSERT_EQUAL(7 * 60 + 30, m);
    TEST_ASSERT_TRUE(parseTimeOfDay("7pm", m));
    TEST_ASSERT_EQUAL(19 * 60, m);
    TEST_ASSERT_TRUE(parseTimeOfDay("12am", m));
    TEST_ASSERT_EQUAL(0, m);
    TEST_ASSERT_TRUE(parseTimeOfDay("12 PM", m));
    TEST_ASSERT_EQUAL(12 * 60, m);
    TEST_ASSERT_TRUE(parseTimeOfDay("6.45am", m));
    TEST_ASSERT_EQUAL(6 * 60 + 45, m);
    TEST_ASSERT_TRUE(parseTimeOfDay("0", m));
    TEST_ASSERT_EQUAL(0, m);
    TEST_ASSERT_FALSE(parseTimeOfDay("24:00", m));
    TEST_ASSERT_FALSE(parseTimeOfDay("13pm", m));
    TEST_ASSERT_FALSE(parseTimeOfDay("7:5", m));
    TEST_ASSERT_FALSE(parseTimeOfDay("7:60", m));
    TEST_ASSERT_FALSE(parseTimeOfDay("bedtime", m));
    TEST_ASSERT_FALSE(parseTimeOfDay("", m));
}

void test_describing_seconds() {
    TEST_ASSERT_EQUAL_STRING("30 seconds", describeSeconds(30).c_str());
    TEST_ASSERT_EQUAL_STRING("1 minute", describeSeconds(60).c_str());
    TEST_ASSERT_EQUAL_STRING("6 minutes", describeSeconds(360).c_str());
    TEST_ASSERT_EQUAL_STRING("2 minutes 30 seconds", describeSeconds(150).c_str());
    TEST_ASSERT_EQUAL_STRING("1 second", describeSeconds(1).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_settings_menu_is_for_admins_only);
    RUN_TEST(test_settings_commands_are_in_admin_help_only);
    RUN_TEST(test_changing_voice_volume_from_the_menu);
    RUN_TEST(test_back_goes_to_the_main_page);
    RUN_TEST(test_direct_commands_change_settings);
    RUN_TEST(test_direct_command_without_a_value_opens_that_page);
    RUN_TEST(test_quiet_hours_with_the_hour_pickers);
    RUN_TEST(test_turning_quiet_hours_off);
    RUN_TEST(test_keep_messages_and_monitor_mode_from_the_menu);
    RUN_TEST(test_changing_the_childs_name);
    RUN_TEST(test_changing_the_time_zone_from_the_menu);
    RUN_TEST(test_timezone_command_with_a_country_with_several_zones);
    RUN_TEST(test_menu_carries_on_after_a_restart);
    RUN_TEST(test_removing_a_contact_needs_confirming);
    RUN_TEST(test_removing_a_monitor);
    RUN_TEST(test_removing_an_admin_tells_everyone);
    RUN_TEST(test_admin_removing_themselves);
    RUN_TEST(test_last_admin_cannot_be_removed);
    RUN_TEST(test_inviting_from_the_menu);
    RUN_TEST(test_status);
    RUN_TEST(test_trash_getlast_sends_the_newest_recording);
    RUN_TEST(test_last_deleted_from_the_menu);
    RUN_TEST(test_only_checks_on_a_new_network_once_the_time_zone_is_set);
    RUN_TEST(test_a_failed_location_check_is_tried_again_next_time);
    RUN_TEST(test_same_country_asks_nothing);
    RUN_TEST(test_moving_country_asks_admins_and_yes_switches);
    RUN_TEST(test_no_means_never_ask_about_that_country_again);
    RUN_TEST(test_country_with_several_zones_asks_for_a_city);
    RUN_TEST(test_suggested_city_is_used_when_known);
    RUN_TEST(test_going_home_before_anyone_answers);
    RUN_TEST(test_moving_again_before_anyone_answers);
    RUN_TEST(test_only_admins_can_answer);
    RUN_TEST(test_reading_typed_times);
    RUN_TEST(test_describing_seconds);
    return UNITY_END();
}

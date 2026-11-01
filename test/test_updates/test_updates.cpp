// Software updates (ticket 1.10): the checksum, version numbers, firmware.json,
// /update, the daily check, installing and reporting back after a restart.
// Against a pretend Telegram and a pretend GitHub.

#include <ArduinoJson.h>
#include <unity.h>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/Version.h"
#include "app/bot/Bot.h"
#include "app/updates/Sha256.h"
#include "app/updates/Updates.h"
#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;
using namespace fvb::telegram;

static const long long kAdmin = 1001;
static const long long kOtherAdmin = 1002;
static const long long kContact = 2002;
static const int64_t kNow = 1790000000;  // Monday 21 September 2026, 14:13 UTC

static const std::string kLatestUrl =
    "https://github.com/FionaKateM/family-voice-note/releases/latest/download/firmware.json";
static std::string firmwareUrl(const std::string& v) {
    return "https://github.com/FionaKateM/family-voice-note/releases/download/v" + v + "/firmware.bin";
}

struct Sent {
    long long chat;
    std::string text;
    bool hasButtons;
};

// Answers Telegram's methods, and GitHub addresses from `web`.
class PretendInternet : public Network {
public:
    bool isConnected() const override { return true; }
    int8_t signalStrength() const override { return -60; }

    HttpResponse request(const HttpRequest& req) override {
        requests.push_back(req);
        if (req.url.find("api.telegram.org") == std::string::npos) return web(req);

        const std::string method = telegramMethod(req);
        if (method == "getUpdates") {
            if (batches.empty()) return telegramOk("[]");
            std::string batch = batches.front();
            batches.pop_front();
            return telegramOk("[" + batch + "]");
        }
        if (method == "getMe") return telegramOk(R"({"id":42,"is_bot":true,"first_name":"Box","username":"TestBoxBot"})");
        if (method == "sendMessage" || method == "editMessageText") {
            JsonDocument b;
            deserializeJson(b, req.body);
            const long long chat = b["chat_id"].as<long long>();
            Sent s{chat, b["text"].as<std::string>(), !b["reply_markup"].isNull()};
            if (method == "sendMessage") sent.push_back(s);
            else edited.push_back(s);
            return telegramOk(R"({"message_id":)" + std::to_string(++lastMessageId) + R"(,"chat":{"id":)" +
                              std::to_string(chat) + R"(,"type":"private"},"date":1790000000,"text":"x"})");
        }
        if (method == "answerCallbackQuery") return telegramOk("true");
        return telegramError(400, "Bad Request: not pretended");
    }

    HttpResponse web(const HttpRequest& req) {
        if (webDown) return networkDown();
        auto it = pages.find(req.url);
        if (it == pages.end()) return HttpResponse{404, "Not Found", ""};
        HttpResponse r{200, it->second, ""};
        if (req.onBodyChunk) {
            for (size_t i = 0; i < r.body.size(); i += 5) {
                if (!req.onBodyChunk(r.body.data() + i, std::min<size_t>(5, r.body.size() - i))) break;
            }
            r.body.clear();
        }
        return r;
    }

    // Messages sent to `chat` so far.
    std::vector<Sent> to(long long chat) const {
        std::vector<Sent> out;
        for (const auto& s : sent) {
            if (s.chat == chat) out.push_back(s);
        }
        return out;
    }
    int webRequests() const {
        int n = 0;
        for (const auto& r : requests) n += r.url.find("api.telegram.org") == std::string::npos;
        return n;
    }

    std::vector<HttpRequest> requests;
    std::deque<std::string> batches;
    std::map<std::string, std::string> pages;
    std::vector<Sent> sent;
    std::vector<Sent> edited;
    long long lastMessageId = 500;
    bool webDown = false;
};

class FakeInstaller : public FirmwareInstaller {
public:
    bool begin(size_t size, const std::string& version, std::string& error) override {
        if (refuse) {
            error = "no room";
            return false;
        }
        expected = size;
        beganVersion = version;
        received.clear();
        open = true;
        return true;
    }
    bool write(const uint8_t* data, size_t length) override {
        if (!open) return false;
        received.append(reinterpret_cast<const char*>(data), length);
        return true;
    }
    bool finish(std::string&) override {
        finished = open;
        open = false;
        return finished;
    }
    void abort() override {
        open = false;
        ++aborts;
    }
    void restart() override { ++restarts; }
    void markGood() override { ++markedGood; }

    bool refuse = false;
    bool open = false;
    bool finished = false;
    size_t expected = 0;
    std::string beganVersion;
    std::string received;
    int aborts = 0;
    int restarts = 0;
    int markedGood = 0;
};

static FakeHardware* hw;
static PretendInternet* net;
static FakeInstaller* installer;
static std::unique_ptr<Storage> storage;
static std::unique_ptr<TelegramClient> client;
static std::unique_ptr<Bot> bot;
static std::unique_ptr<Updates> updates;
static long long nextUpdate = 100;

static const std::string kFirmware = "pretend firmware: not really for an ESP32, but the right size and checksum";

// Publishes `v` on the pretend GitHub.
static void publish(const std::string& v, const std::string& notes = "Louder voices",
                    const std::string& firmware = kFirmware) {
    net->pages[kLatestUrl] = R"({"version":")" + v + R"(","notes":")" + notes + R"(","size":)" +
                             std::to_string(firmware.size()) + R"(,"sha256":")" + Sha256::of(firmware) + "\"}";
    net->pages[firmwareUrl(v)] = firmware;
}

static void restart() {
    updates.reset();
    bot.reset();
    client.reset(new TelegramClient(*net, hw->files, hw->log, kTestBotToken));
    bot.reset(new Bot(*client, *storage, hw->settings, hw->clock, hw->log));
    updates.reset(new Updates(*bot, *client, *storage, hw->settings, hw->clock, hw->log, *net, *installer));
    bot->step();  // connect
    TEST_ASSERT_TRUE(bot->state() == Bot::State::Running);
}

// One go round the network loop's update step. Returns what it did.
static Updates::Task::Kind runUpdates() {
    Updates::Task task = updates->prepare();
    if (task.kind != Updates::Task::Kind::None) updates->finish(task, updates->run(task));
    return task.kind;
}

static void say(long long from, const std::string& text) {
    const long long id = nextUpdate++;
    net->batches.push_back(messageUpdate(id, messageJson(id, from, textContent(text))));
    bot->step();
}

static void press(long long from, const std::string& data) {
    net->batches.push_back(buttonUpdate(nextUpdate++, from, 77, data));
    bot->step();
}

void setUp() {
    setPretendFirmwareVersion("");
    hw = new FakeHardware();
    net = new PretendInternet();
    installer = new FakeInstaller();
    hw->clock.timeSet = true;
    hw->clock.unixSeconds = kNow;
    storage.reset(new Storage(hw->files, hw->settings));
    storage->settings.setChildName("Child");
    storage->people.addAdmin(Person{kAdmin, "Parent"});
    storage->people.addAdmin(Person{kOtherAdmin, "Alex"});
    Contact granny;
    granny.telegramId = kContact;
    granny.name = "Granny";
    storage->people.addContact(granny);
    restart();
    // Nothing to report from before this "start".
    updates->afterStart();
    net->sent.clear();
}

void tearDown() {
    updates.reset();
    bot.reset();
    client.reset();
    storage.reset();
    delete installer;
    delete net;
    delete hw;
    setPretendFirmwareVersion("");
}

// ---------------------------------------------------------------------------
// SHA-256

static void test_sha256_known_answers() {
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", Sha256::of("").c_str());
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                             Sha256::of("abc").c_str());
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                             Sha256::of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
}

static void test_sha256_in_uneven_pieces() {
    // A million 'a's, added 997 bytes at a time, so pieces straddle the 64-byte blocks.
    const std::string piece(997, 'a');
    Sha256 sha;
    size_t total = 0;
    while (total + piece.size() <= 1000000) {
        sha.add(piece);
        total += piece.size();
    }
    sha.add(std::string(1000000 - total, 'a'));
    TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", sha.finishHex().c_str());
}

// ---------------------------------------------------------------------------
// Version numbers and firmware.json

static void test_version_numbers() {
    std::vector<int> parts;
    TEST_ASSERT_TRUE(version::parse("1.3", parts));
    TEST_ASSERT_TRUE(version::parse("v1.3.0", parts));
    TEST_ASSERT_EQUAL(3, parts.size());
    TEST_ASSERT_FALSE(version::parse("", parts));
    TEST_ASSERT_FALSE(version::parse("v", parts));
    TEST_ASSERT_FALSE(version::parse("1..3", parts));
    TEST_ASSERT_FALSE(version::parse("1.3-beta", parts));
    TEST_ASSERT_FALSE(version::parse("1.3.", parts));
    TEST_ASSERT_FALSE(version::parse("1.2.3.4.5", parts));

    TEST_ASSERT_TRUE(version::isNewer("1.3", "1.2.9"));
    TEST_ASSERT_TRUE(version::isNewer("0.10.0", "0.9.0"));  // compared as numbers, not text
    TEST_ASSERT_TRUE(version::isNewer("1.3.1", "1.3"));
    TEST_ASSERT_FALSE(version::isNewer("1.3", "1.3.0"));
    TEST_ASSERT_FALSE(version::isNewer("1.2", "1.3"));
    TEST_ASSERT_FALSE(version::isNewer("rubbish", "1.3"));
}

static void test_pretend_version() {
    TEST_ASSERT_EQUAL_STRING(kFirmwareVersion, firmwareVersion().c_str());
    setPretendFirmwareVersion("9.9.9");
    TEST_ASSERT_EQUAL_STRING("9.9.9", firmwareVersion().c_str());
    setPretendFirmwareVersion("");
    TEST_ASSERT_EQUAL_STRING(kFirmwareVersion, firmwareVersion().c_str());
}

static void test_reading_firmware_json() {
    Release r;
    std::string error;
    const std::string sha = Sha256::of("x");
    // The format scripts/make-release.sh writes (Python's json.dump, with spaces).
    TEST_ASSERT_TRUE(parseRelease(R"({"version": "1.3.0", "notes": " Louder\nvoices ", "size": 1234, "sha256": ")" +
                                      sha + "\"}",
                                  r, error));
    TEST_ASSERT_EQUAL_STRING("1.3.0", r.version.c_str());
    TEST_ASSERT_EQUAL_STRING("Louder\nvoices", r.notes.c_str());
    TEST_ASSERT_EQUAL(1234, r.size);
    TEST_ASSERT_EQUAL_STRING(sha.c_str(), r.sha256.c_str());

    // A checksum in capitals is fine.
    std::string upper = sha;
    for (char& c : upper) c = static_cast<char>(toupper(c));
    TEST_ASSERT_TRUE(parseRelease(R"({"version":"1.3","size":5,"sha256":")" + upper + "\"}", r, error));
    TEST_ASSERT_EQUAL_STRING(sha.c_str(), r.sha256.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.notes.c_str());

    TEST_ASSERT_FALSE(parseRelease("not json", r, error));
    TEST_ASSERT_FALSE(parseRelease(R"({"version":"1.3","size":5})", r, error));
    TEST_ASSERT_FALSE(parseRelease(R"({"version":"1.3","size":5,"sha256":"abc"})", r, error));
    TEST_ASSERT_FALSE(parseRelease(R"({"version":"beta","size":5,"sha256":")" + sha + "\"}", r, error));
    TEST_ASSERT_FALSE(parseRelease(R"({"version":"1.3","size":0,"sha256":")" + sha + "\"}", r, error));
    TEST_ASSERT_FALSE(parseRelease(R"({"version":"1.3","size":99999999,"sha256":")" + sha + "\"}", r, error));
}

static void test_github_location() {
    const ReleaseLocation where = ReleaseLocation::github();
    TEST_ASSERT_EQUAL_STRING(kLatestUrl.c_str(), where.latestUrl.c_str());
    const ReleaseLocation folder = ReleaseLocation::folder("/tmp/test-release/");
    TEST_ASSERT_EQUAL_STRING("file:///tmp/test-release/firmware.json", folder.latestUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("file:///tmp/test-release/firmware.bin", folder.firmwareUrl.c_str());
}

// ---------------------------------------------------------------------------
// /update

static void test_update_with_no_releases_yet() {
    say(kAdmin, "/update");
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Check);
    const auto replies = net->to(kAdmin);
    TEST_ASSERT_EQUAL(1, replies.size());
    TEST_ASSERT_EQUAL_STRING((std::string("Already up to date (version ") + kFirmwareVersion + ").").c_str(),
                             replies[0].text.c_str());
}

static void test_update_asks_github_with_redirects() {
    say(kAdmin, "/update");
    runUpdates();
    const HttpRequest& asked = net->requests.back().url.find("telegram") == std::string::npos
                                   ? net->requests.back()
                                   : net->requests[net->requests.size() - 2];
    TEST_ASSERT_EQUAL_STRING(kLatestUrl.c_str(), asked.url.c_str());
    TEST_ASSERT_TRUE(asked.followRedirects);
}

static void test_update_when_same_or_older_version() {
    publish(kFirmwareVersion);
    say(kAdmin, "/update");
    runUpdates();
    TEST_ASSERT_TRUE(net->to(kAdmin).back().text.find("Already up to date") == 0);

    publish("0.0.1");
    say(kAdmin, "/update");
    runUpdates();
    TEST_ASSERT_TRUE(net->to(kAdmin).back().text.find("Already up to date") == 0);
}

static void test_update_offers_new_version() {
    publish("9.1", "Louder voices and a new chime");
    say(kAdmin, "/update");
    runUpdates();
    const auto replies = net->to(kAdmin);
    TEST_ASSERT_EQUAL(1, replies.size());
    TEST_ASSERT_TRUE(replies[0].hasButtons);
    TEST_ASSERT_TRUE(replies[0].text.find("Version 9.1 is available: Louder voices and a new chime. Install?") == 0);
    TEST_ASSERT_TRUE(replies[0].text.find(std::string("on version ") + kFirmwareVersion) != std::string::npos);
    // Only the admin who asked.
    TEST_ASSERT_EQUAL(0, net->to(kOtherAdmin).size());
}

static void test_long_notes_are_cut() {
    publish("9.1", std::string(500, 'x'));
    say(kAdmin, "/update");
    runUpdates();
    const std::string text = net->to(kAdmin)[0].text;
    TEST_ASSERT_TRUE(text.find(std::string(Updates::kMaxNotesLength, 'x') + "… Install?") != std::string::npos);
    TEST_ASSERT_TRUE(text.find(std::string(Updates::kMaxNotesLength + 1, 'x')) == std::string::npos);
}

static void test_update_when_github_cant_be_reached() {
    net->webDown = true;
    say(kAdmin, "/update");
    runUpdates();
    TEST_ASSERT_TRUE(net->to(kAdmin)[0].text.find("I couldn't check for updates just now") == 0);
}

static void test_contacts_cant_update() {
    publish("9.1");
    say(kContact, "/update");
    runUpdates();  // only the daily check, which tells admins
    for (const auto& s : net->to(kContact)) TEST_ASSERT_TRUE(s.text.find("available") == std::string::npos);
    for (const auto& s : net->to(kAdmin)) TEST_ASSERT_TRUE(s.text.find("Install?") == std::string::npos);
}

static void test_no_keeps_the_old_version() {
    publish("9.1");
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:n");
    TEST_ASSERT_TRUE(net->edited.back().text.find("OK, staying on version") == 0);
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::None);
    TEST_ASSERT_EQUAL(0, installer->restarts);
}

static void test_yes_installs_and_restarts() {
    publish("9.1");
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:y");
    TEST_ASSERT_TRUE(net->edited.back().text.find("Downloading version 9.1") == 0);

    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Install);
    TEST_ASSERT_EQUAL_STRING("9.1", installer->beganVersion.c_str());
    TEST_ASSERT_EQUAL(kFirmware.size(), installer->expected);
    TEST_ASSERT_TRUE(installer->received == kFirmware);
    TEST_ASSERT_TRUE(installer->finished);
    TEST_ASSERT_EQUAL(1, installer->restarts);
    TEST_ASSERT_TRUE(net->to(kAdmin).back().text.find("Installing version 9.1. The box will restart") == 0);

    // The firmware was asked for at the version's own address, not "latest".
    bool askedForFirmware = false;
    for (const auto& r : net->requests) askedForFirmware |= r.url == firmwareUrl("9.1") && r.followRedirects;
    TEST_ASSERT_TRUE(askedForFirmware);
}

static void test_after_restart_says_updated() {
    publish("9.1");
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:y");
    runUpdates();
    net->sent.clear();

    // The new firmware starts.
    setPretendFirmwareVersion("9.1");
    restart();
    updates->afterStart();
    TEST_ASSERT_EQUAL(1, installer->markedGood - 1);  // once in setUp, once now
    TEST_ASSERT_EQUAL(1, net->to(kAdmin).size());
    TEST_ASSERT_EQUAL_STRING("Updated to 9.1.", net->to(kAdmin)[0].text.c_str());
    TEST_ASSERT_EQUAL(1, net->to(kOtherAdmin).size());
    TEST_ASSERT_EQUAL_STRING("Granny updated the box to version 9.1.", net->to(kOtherAdmin)[0].text.c_str());

    // Only said once.
    restart();
    updates->afterStart();
    TEST_ASSERT_EQUAL(1, net->to(kAdmin).size());
}

static void test_after_rollback_tells_every_admin() {
    publish("9.1");
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:y");
    runUpdates();
    net->sent.clear();

    // The new firmware didn't start, so the old one is running again.
    restart();
    updates->afterStart();
    const std::string expected = std::string("The update to version 9.1 didn't start properly, so the box went back to version ") +
                                 kFirmwareVersion + " by itself. Everything else is as it was.";
    TEST_ASSERT_EQUAL(1, net->to(kAdmin).size());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), net->to(kAdmin)[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), net->to(kOtherAdmin)[0].text.c_str());
    TEST_ASSERT_EQUAL(0, net->to(kContact).size());
}

static void test_damaged_download_isnt_installed() {
    publish("9.1");
    std::string damaged = kFirmware;
    damaged[3] = '!';
    net->pages[firmwareUrl("9.1")] = damaged;
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:y");
    runUpdates();
    TEST_ASSERT_FALSE(installer->finished);
    TEST_ASSERT_EQUAL(1, installer->aborts);
    TEST_ASSERT_EQUAL(0, installer->restarts);
    const std::string text = net->to(kAdmin).back().text;
    TEST_ASSERT_TRUE(text.find("didn't work: the download was damaged on the way") != std::string::npos);
    TEST_ASSERT_TRUE(text.find("Nothing has changed") != std::string::npos);

    // Nothing to report after a restart, and they can try again.
    net->sent.clear();
    restart();
    updates->afterStart();
    TEST_ASSERT_EQUAL(0, net->sent.size());
}

static void test_wrong_size_or_missing_download() {
    publish("9.1");
    net->pages[firmwareUrl("9.1")] = kFirmware + "extra";
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:y");
    runUpdates();
    TEST_ASSERT_TRUE(net->to(kAdmin).back().text.find("the download was the wrong size") != std::string::npos);
    TEST_ASSERT_FALSE(installer->finished);

    net->pages.erase(firmwareUrl("9.1"));
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:y");
    runUpdates();
    TEST_ASSERT_TRUE(net->to(kAdmin).back().text.find("GitHub answered with error 404") != std::string::npos);
    TEST_ASSERT_EQUAL(0, installer->restarts);
}

static void test_installer_refusing() {
    installer->refuse = true;
    publish("9.1");
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.1:y");
    runUpdates();
    TEST_ASSERT_TRUE(net->to(kAdmin).back().text.find("couldn't get ready to install it (no room)") != std::string::npos);
}

static void test_old_buttons_expire() {
    publish("9.1");
    // A Yes from before a restart, or for a version that's no longer the newest.
    press(kAdmin, "update:9.1:y");
    TEST_ASSERT_TRUE(net->edited.back().text.find("This has expired") == 0);
    say(kAdmin, "/update");
    runUpdates();
    press(kAdmin, "update:9.0:y");
    TEST_ASSERT_TRUE(net->edited.back().text.find("This has expired") == 0);
    TEST_ASSERT_TRUE(runUpdates() != Updates::Task::Kind::Install);
}

static void test_second_yes_while_installing() {
    publish("9.1");
    say(kAdmin, "/update");
    say(kOtherAdmin, "/update");
    runUpdates();
    TEST_ASSERT_TRUE(net->to(kOtherAdmin).back().hasButtons);
    press(kAdmin, "update:9.1:y");
    press(kOtherAdmin, "update:9.1:y");
    TEST_ASSERT_TRUE(net->edited.back().text.find("An update is already being installed") == 0);
    say(kOtherAdmin, "/update");
    TEST_ASSERT_TRUE(net->to(kOtherAdmin).back().text.find("An update is already being installed") == 0);
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Install);
    TEST_ASSERT_EQUAL(1, installer->restarts);
}

// ---------------------------------------------------------------------------
// The daily check

static void test_daily_check_tells_admins_once() {
    publish("9.1");
    TEST_ASSERT_TRUE(updates->dailyCheckDue());
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Check);
    TEST_ASSERT_EQUAL_STRING("Version 9.1 is available. Send /update to install.", net->to(kAdmin)[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("Version 9.1 is available. Send /update to install.",
                             net->to(kOtherAdmin)[0].text.c_str());
    TEST_ASSERT_EQUAL(0, net->to(kContact).size());

    // Not due again for a day.
    TEST_ASSERT_FALSE(updates->dailyCheckDue());
    hw->clock.unixSeconds += Updates::kCheckEverySeconds - 1;
    TEST_ASSERT_FALSE(updates->dailyCheckDue());
    hw->clock.unixSeconds += 1;
    TEST_ASSERT_TRUE(updates->dailyCheckDue());

    // The next day, the same version isn't announced again...
    runUpdates();
    TEST_ASSERT_EQUAL(1, net->to(kAdmin).size());

    // ...but a newer one is.
    publish("9.2");
    hw->clock.unixSeconds += Updates::kCheckEverySeconds;
    runUpdates();
    TEST_ASSERT_EQUAL(2, net->to(kAdmin).size());
    TEST_ASSERT_EQUAL_STRING("Version 9.2 is available. Send /update to install.", net->to(kAdmin)[1].text.c_str());
}

static void test_daily_check_survives_restart() {
    publish("9.1");
    runUpdates();
    restart();
    TEST_ASSERT_FALSE(updates->dailyCheckDue());
    hw->clock.unixSeconds += Updates::kCheckEverySeconds;
    restart();
    runUpdates();
    TEST_ASSERT_EQUAL(1, net->to(kAdmin).size());  // still only told once
}

static void test_daily_check_says_nothing_when_up_to_date() {
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Check);  // no releases
    publish(kFirmwareVersion);
    hw->clock.unixSeconds += Updates::kCheckEverySeconds;
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Check);
    TEST_ASSERT_EQUAL(0, net->sent.size());
}

static void test_daily_check_waits_after_failure() {
    net->webDown = true;
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Check);
    TEST_ASSERT_EQUAL(0, net->sent.size());  // nobody's bothered
    const int asked = net->webRequests();
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::None);
    TEST_ASSERT_EQUAL(asked, net->webRequests());

    net->webDown = false;
    publish("9.1");
    hw->clock.bootMs += Updates::kRetryAfterFailureMs;
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::Check);
    TEST_ASSERT_EQUAL(1, net->to(kAdmin).size());
}

static void test_daily_check_needs_the_clock() {
    hw->clock.timeSet = false;
    TEST_ASSERT_FALSE(updates->dailyCheckDue());
    TEST_ASSERT_TRUE(runUpdates() == Updates::Task::Kind::None);
}

static void test_version_found_by_update_isnt_announced_again() {
    publish("9.1");
    say(kAdmin, "/update");
    runUpdates();  // the admin's check counts as the day's check too
    TEST_ASSERT_EQUAL_STRING("9.1", updates->toldAbout().c_str());
    hw->clock.unixSeconds += Updates::kCheckEverySeconds;
    runUpdates();
    TEST_ASSERT_EQUAL(1, net->to(kAdmin).size());
    TEST_ASSERT_EQUAL(0, net->to(kOtherAdmin).size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_known_answers);
    RUN_TEST(test_sha256_in_uneven_pieces);
    RUN_TEST(test_version_numbers);
    RUN_TEST(test_pretend_version);
    RUN_TEST(test_reading_firmware_json);
    RUN_TEST(test_github_location);
    RUN_TEST(test_update_with_no_releases_yet);
    RUN_TEST(test_update_asks_github_with_redirects);
    RUN_TEST(test_update_when_same_or_older_version);
    RUN_TEST(test_update_offers_new_version);
    RUN_TEST(test_long_notes_are_cut);
    RUN_TEST(test_update_when_github_cant_be_reached);
    RUN_TEST(test_contacts_cant_update);
    RUN_TEST(test_no_keeps_the_old_version);
    RUN_TEST(test_yes_installs_and_restarts);
    RUN_TEST(test_after_restart_says_updated);
    RUN_TEST(test_after_rollback_tells_every_admin);
    RUN_TEST(test_damaged_download_isnt_installed);
    RUN_TEST(test_wrong_size_or_missing_download);
    RUN_TEST(test_installer_refusing);
    RUN_TEST(test_old_buttons_expire);
    RUN_TEST(test_second_yes_while_installing);
    RUN_TEST(test_daily_check_tells_admins_once);
    RUN_TEST(test_daily_check_survives_restart);
    RUN_TEST(test_daily_check_says_nothing_when_up_to_date);
    RUN_TEST(test_daily_check_waits_after_failure);
    RUN_TEST(test_daily_check_needs_the_clock);
    RUN_TEST(test_version_found_by_update_isnt_announced_again);
    return UNITY_END();
}

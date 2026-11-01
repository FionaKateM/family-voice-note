// The storage running on the desktop build's real folders, including a restart.

#include <unity.h>

#include <filesystem>
#include <random>
#include <string>

#include "app/storage/Paths.h"
#include "app/storage/Storage.h"
#include "platform/desktop/DesktopFileStorage.h"
#include "platform/desktop/DesktopSettingsStore.h"

namespace fs = std::filesystem;
using namespace fvb;

static fs::path tempRoot;

void setUp() {
    std::random_device rd;
    tempRoot = fs::temp_directory_path() / ("fvb-test-" + std::to_string(rd()));
    fs::remove_all(tempRoot);
}

void tearDown() { fs::remove_all(tempRoot); }

constexpr int64_t kNow = 1793543525;

void test_everything_survives_a_restart() {
    uint32_t granny = 0;
    {
        DesktopFileStorage card(tempRoot / "sdcard");
        DesktopSettingsStore nvs(tempRoot / "nvs");
        Storage storage(card, nvs);

        card.writeFile(paths::kSetupFile, std::string("BOT_TOKEN=1234:") + std::string(35, 'A') +
                                              "\nCHILD_NAME=Child\nADMIN_ID=111\nWIFI_SSID=Home\n");
        TEST_ASSERT_TRUE(storage.applySetupFile().outcome == SetupResult::Outcome::Applied);

        Contact c;
        c.telegramId = 500;
        c.name = "Granny";
        granny = storage.people.addContact(c);

        card.writeFile(paths::kDownload, "voice note");
        StoredMessage m;
        m.time = kNow;
        storage.messages.add(granny, m, paths::kDownload);

        card.writeFile(paths::kRecording, "reply");
        TEST_ASSERT_NOT_EQUAL(0, storage.queueRecording(granny, paths::kRecording, 3000, kNow));

        card.writeFile(paths::kRecording, "oops");
        TEST_ASSERT_NOT_EQUAL("", storage.binRecording(granny, paths::kRecording, kNow));
    }

    // "Power cycle": everything reopened from disk.
    DesktopFileStorage card(tempRoot / "sdcard");
    DesktopSettingsStore nvs(tempRoot / "nvs");
    Storage storage(card, nvs);

    TEST_ASSERT_FALSE(fs::exists(tempRoot / "sdcard" / "setup.txt"));
    TEST_ASSERT_TRUE(storage.settings.hasBotToken());
    TEST_ASSERT_EQUAL_STRING("Home", storage.settings.wifiNetworks()[0].ssid.c_str());
    TEST_ASSERT_TRUE(storage.people.isAdmin(111));
    TEST_ASSERT_TRUE(storage.people.isContact(500));

    const auto history = storage.messages.messages(granny);
    TEST_ASSERT_EQUAL(2, history.size());
    TEST_ASSERT_TRUE(history[0].status == StoredMessage::Status::Unread);
    TEST_ASSERT_TRUE(history[1].status == StoredMessage::Status::Waiting);
    std::string audio;
    TEST_ASSERT_TRUE(card.readFile(storage.messages.audioPath(granny, history[0]), audio));
    TEST_ASSERT_EQUAL_STRING("voice note", audio.c_str());

    TEST_ASSERT_EQUAL(1, storage.outbox.size());
    BinEntry binned;
    TEST_ASSERT_TRUE(storage.bin.newest(binned));
    TEST_ASSERT_EQUAL(granny, binned.contact);
}

void test_index_rewrite_replaces_old_file() {
    DesktopFileStorage card(tempRoot);
    MessageStore store(card);
    card.writeFile(paths::kDownload, "a");
    StoredMessage m;
    const uint32_t id = store.add(1, m, paths::kDownload);
    TEST_ASSERT_TRUE(store.markHeard(1, id));
    TEST_ASSERT_EQUAL(0, store.unreadCount(1));
    TEST_ASSERT_FALSE(card.exists("/contacts/1/index.tmp"));
}

void test_remove_contact_deletes_real_folder() {
    DesktopFileStorage card(tempRoot / "sdcard");
    DesktopSettingsStore nvs(tempRoot / "nvs");
    Storage storage(card, nvs);
    Contact c;
    c.telegramId = 500;
    const uint32_t n = storage.people.addContact(c);
    card.writeFile(paths::contactPhoto(n), "jpg");

    TEST_ASSERT_TRUE(storage.removeContact(n));
    TEST_ASSERT_FALSE(fs::exists(tempRoot / "sdcard" / "contacts" / std::to_string(n)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_everything_survives_a_restart);
    RUN_TEST(test_index_rewrite_replaces_old_file);
    RUN_TEST(test_remove_contact_deletes_real_folder);
    return UNITY_END();
}

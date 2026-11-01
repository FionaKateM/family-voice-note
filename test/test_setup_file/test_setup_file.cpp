// Reading, saving and deleting setup.txt.

#include <unity.h>

#include <string>

#include "app/App.h"
#include "app/storage/Paths.h"
#include "app/storage/SetupFile.h"
#include "fakes/Fakes.h"

using namespace fvb;
using fvb::fakes::FakeFileStorage;
using fvb::fakes::FakeHardware;
using fvb::fakes::FakeSettingsStore;

void setUp() {}
void tearDown() {}

// Built at runtime so nothing token-shaped is written in the repo (the secret
// check would rightly complain).
std::string fakeToken(char fill = 'A') { return std::string("1234") + ":" + std::string(35, fill); }

std::string goodFile() {
    return "WIFI_SSID=Home\n"
           "WIFI_PASSWORD=secret pw\n"
           "BOT_TOKEN=" +
           fakeToken() +
           "\n"
           "CHILD_NAME=Child\n"
           "ADMIN_ID=111\n";
}

bool hasProblemContaining(const SetupResult& r, const std::string& text) {
    for (const auto& p : r.problems) {
        if (p.find(text) != std::string::npos) return true;
    }
    return false;
}

bool anyMessageContains(const SetupResult& r, const std::string& text) {
    for (const auto& p : r.problems) {
        if (p.find(text) != std::string::npos) return true;
    }
    for (const auto& w : r.warnings) {
        if (w.find(text) != std::string::npos) return true;
    }
    return false;
}

struct Setup {
    FakeFileStorage files;
    FakeSettingsStore nvs;
    DeviceSettings settings{nvs};
    People people{nvs};
    SetupResult apply() { return applySetupFile(files, settings, people); }
};

// ---- Parsing ----

void test_parses_good_file() {
    SetupValues v;
    std::vector<std::string> problems, warnings;
    TEST_ASSERT_TRUE(parseSetupText(goodFile(), v, problems, warnings));
    TEST_ASSERT_EQUAL_STRING("Home", v.wifiSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("secret pw", v.wifiPassword.c_str());
    TEST_ASSERT_EQUAL_STRING(fakeToken().c_str(), v.botToken.c_str());
    TEST_ASSERT_EQUAL_STRING("Child", v.childName.c_str());
    TEST_ASSERT_EQUAL(1, v.adminIds.size());
    TEST_ASSERT_TRUE(v.adminIds[0] == 111);
    TEST_ASSERT_EQUAL(0, warnings.size());
}

void test_forgiving_about_text_editors() {
    const std::string text = "\xEF\xBB\xBF# a comment\r\n"
                             "\r\n"
                             "  wifi_ssid = \"My Home \"  \r\n"
                             "Bot_Token='" +
                             fakeToken() +
                             "'\r\n"
                             "child_name=Child\r\n"
                             "admin_id = 111, 7000000000\r\n";
    SetupValues v;
    std::vector<std::string> problems, warnings;
    TEST_ASSERT_TRUE(parseSetupText(text, v, problems, warnings));
    TEST_ASSERT_EQUAL_STRING("My Home ", v.wifiSsid.c_str());  // quotes keep the space
    TEST_ASSERT_EQUAL_STRING(fakeToken().c_str(), v.botToken.c_str());
    TEST_ASSERT_EQUAL(2, v.adminIds.size());
    TEST_ASSERT_TRUE(v.adminIds[1] == 7000000000LL);
}

void test_password_can_contain_equals_sign() {
    SetupValues v;
    std::vector<std::string> problems, warnings;
    parseSetupText("WIFI_SSID=Home\nWIFI_PASSWORD=a=b=c\n", v, problems, warnings);
    TEST_ASSERT_EQUAL_STRING("a=b=c", v.wifiPassword.c_str());
}

void test_warns_about_unknown_and_repeated_lines() {
    SetupValues v;
    std::vector<std::string> problems, warnings;
    TEST_ASSERT_TRUE(parseSetupText("CHILD_NAME=A\nCHILD_NAME=B\nFAVOURITE_COLOUR=blue\n", v, problems, warnings));
    TEST_ASSERT_EQUAL_STRING("B", v.childName.c_str());
    TEST_ASSERT_EQUAL(2, warnings.size());
}

void test_problems_with_bad_lines() {
    SetupValues v;
    std::vector<std::string> problems, warnings;
    TEST_ASSERT_FALSE(parseSetupText("just some words\nADMIN_ID=abc\nBOT_TOKEN=nope\nWIFI_PASSWORD=x\n", v,
                                     problems, warnings));
    TEST_ASSERT_EQUAL(4, problems.size());
}

void test_bot_token_shape() {
    TEST_ASSERT_TRUE(looksLikeBotToken(fakeToken()));
    TEST_ASSERT_TRUE(looksLikeBotToken(std::string("1:") + std::string(30, '-')));
    TEST_ASSERT_FALSE(looksLikeBotToken(":" + std::string(35, 'A')));
    TEST_ASSERT_FALSE(looksLikeBotToken("12a4:" + std::string(35, 'A')));
    TEST_ASSERT_FALSE(looksLikeBotToken("1234:" + std::string(29, 'A')));
    TEST_ASSERT_FALSE(looksLikeBotToken("1234:" + std::string(34, 'A') + " "));
    TEST_ASSERT_FALSE(looksLikeBotToken(std::string(40, 'A')));
}

void test_unedited_example_file_is_rejected() {
    // The same values as secrets/setup.example.txt.
    Setup s;
    s.files.writeFile(paths::kSetupFile,
                      "WIFI_SSID=YourWifiName\nWIFI_PASSWORD=YourWifiPassword\n"
                      "BOT_TOKEN=0000000000:FAKE-TOKEN-REPLACE-ME\nCHILD_NAME=Child\nADMIN_ID=000000000\n");
    const auto r = s.apply();
    TEST_ASSERT_TRUE(r.outcome == SetupResult::Outcome::Rejected);
    TEST_ASSERT_TRUE(hasProblemContaining(r, "BOT_TOKEN"));
    TEST_ASSERT_TRUE(hasProblemContaining(r, "ADMIN_ID"));
}

// ---- Applying ----

void test_no_file_does_nothing() {
    Setup s;
    const auto r = s.apply();
    TEST_ASSERT_TRUE(r.outcome == SetupResult::Outcome::NoFile);
    TEST_ASSERT_EQUAL(0, s.nvs.values.size());
}

void test_good_file_is_saved_then_deleted() {
    Setup s;
    s.files.writeFile(paths::kSetupFile, goodFile());
    const auto r = s.apply();

    TEST_ASSERT_TRUE(r.outcome == SetupResult::Outcome::Applied);
    TEST_ASSERT_TRUE(r.fileDeleted);
    TEST_ASSERT_FALSE(s.files.exists(paths::kSetupFile));
    TEST_ASSERT_EQUAL_STRING(fakeToken().c_str(), s.settings.botToken().c_str());
    TEST_ASSERT_EQUAL_STRING("Child", s.settings.childName().c_str());
    TEST_ASSERT_EQUAL(1, s.settings.wifiNetworks().size());
    TEST_ASSERT_EQUAL_STRING("secret pw", s.settings.wifiNetworks()[0].password.c_str());
    TEST_ASSERT_TRUE(s.people.isAdmin(111));
}

// Checks the file is blanked before being deleted, by using a card that can't delete.
class NoDeleteStorage : public FakeFileStorage {
public:
    bool removeFile(const std::string&) override { return false; }
};

void test_file_is_blanked_before_delete() {
    NoDeleteStorage files;
    FakeSettingsStore nvs;
    DeviceSettings settings(nvs);
    People people(nvs);
    files.writeFile(paths::kSetupFile, goodFile());

    const auto r = applySetupFile(files, settings, people);
    TEST_ASSERT_TRUE(r.outcome == SetupResult::Outcome::Applied);
    TEST_ASSERT_FALSE(r.fileDeleted);
    std::string left;
    files.readFile(paths::kSetupFile, left);
    TEST_ASSERT_EQUAL(std::string::npos, left.find(fakeToken()));
    TEST_ASSERT_EQUAL(std::string::npos, left.find("secret"));
    TEST_ASSERT_EQUAL(1, r.warnings.size());
}

void test_first_setup_needs_token_name_and_admin() {
    Setup s;
    s.files.writeFile(paths::kSetupFile, "WIFI_SSID=Home\nWIFI_PASSWORD=pw\n");
    const auto r = s.apply();
    TEST_ASSERT_TRUE(r.outcome == SetupResult::Outcome::Rejected);
    TEST_ASSERT_TRUE(hasProblemContaining(r, "BOT_TOKEN is missing"));
    TEST_ASSERT_TRUE(hasProblemContaining(r, "CHILD_NAME is missing"));
    TEST_ASSERT_TRUE(hasProblemContaining(r, "ADMIN_ID is missing"));
    // Nothing saved, file kept so it can be fixed.
    TEST_ASSERT_EQUAL(0, s.settings.wifiNetworks().size());
    TEST_ASSERT_TRUE(s.files.exists(paths::kSetupFile));
}

void test_wifi_is_optional() {
    // The setup hotspot (ticket 2.4) can add Wi-Fi instead.
    Setup s;
    s.files.writeFile(paths::kSetupFile, "BOT_TOKEN=" + fakeToken() + "\nCHILD_NAME=Child\nADMIN_ID=111\n");
    TEST_ASSERT_TRUE(s.apply().outcome == SetupResult::Outcome::Applied);
    TEST_ASSERT_EQUAL(0, s.settings.wifiNetworks().size());
}

void test_later_file_only_needs_what_changes() {
    Setup s;
    s.files.writeFile(paths::kSetupFile, goodFile());
    s.apply();

    s.files.writeFile(paths::kSetupFile, "WIFI_SSID=Granny's house\nWIFI_PASSWORD=pw2\n");
    const auto r = s.apply();
    TEST_ASSERT_TRUE(r.outcome == SetupResult::Outcome::Applied);
    const auto wifi = s.settings.wifiNetworks();
    TEST_ASSERT_EQUAL(2, wifi.size());
    TEST_ASSERT_EQUAL_STRING("Granny's house", wifi[0].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(fakeToken().c_str(), s.settings.botToken().c_str());  // unchanged
    TEST_ASSERT_TRUE(s.people.isAdmin(111));
}

void test_later_file_can_replace_token_and_add_admin() {
    Setup s;
    s.files.writeFile(paths::kSetupFile, goodFile());
    s.apply();

    s.files.writeFile(paths::kSetupFile, "BOT_TOKEN=" + fakeToken('B') + "\nADMIN_ID=222\n");
    TEST_ASSERT_TRUE(s.apply().outcome == SetupResult::Outcome::Applied);
    TEST_ASSERT_EQUAL_STRING(fakeToken('B').c_str(), s.settings.botToken().c_str());
    TEST_ASSERT_TRUE(s.people.isAdmin(111));
    TEST_ASSERT_TRUE(s.people.isAdmin(222));
}

void test_bad_later_file_changes_nothing() {
    Setup s;
    s.files.writeFile(paths::kSetupFile, goodFile());
    s.apply();

    s.files.writeFile(paths::kSetupFile, "BOT_TOKEN=broken\nCHILD_NAME=Other\n");
    TEST_ASSERT_TRUE(s.apply().outcome == SetupResult::Outcome::Rejected);
    TEST_ASSERT_EQUAL_STRING(fakeToken().c_str(), s.settings.botToken().c_str());
    TEST_ASSERT_EQUAL_STRING("Child", s.settings.childName().c_str());
}

void test_file_kept_if_internal_memory_fails() {
    Setup s;
    s.nvs.failWrites = true;
    s.files.writeFile(paths::kSetupFile, goodFile());
    const auto r = s.apply();
    TEST_ASSERT_TRUE(r.outcome == SetupResult::Outcome::Rejected);
    TEST_ASSERT_TRUE(s.files.exists(paths::kSetupFile));
}

void test_messages_never_contain_secrets() {
    Setup s;
    s.files.writeFile(paths::kSetupFile,
                      "WIFI_PASSWORD=hunter2\nBOT_TOKEN=bad-token-value\nADMIN_ID=555x\nOOPS=hunter3\n");
    const auto r = s.apply();
    TEST_ASSERT_FALSE(anyMessageContains(r, "hunter2"));
    TEST_ASSERT_FALSE(anyMessageContains(r, "hunter3"));
    TEST_ASSERT_FALSE(anyMessageContains(r, "bad-token-value"));
    TEST_ASSERT_FALSE(anyMessageContains(r, "555"));
}

// ---- At start-up ----

void test_app_reads_setup_file_on_start() {
    FakeHardware hw;
    hw.files.writeFile(paths::kSetupFile, goodFile());
    App app(hw.hardware);
    app.begin();

    TEST_ASSERT_FALSE(hw.files.exists(paths::kSetupFile));
    TEST_ASSERT_TRUE(app.storage().settings.hasBotToken());
    TEST_ASSERT_TRUE(hw.log.contains("setup.txt read and saved"));
    TEST_ASSERT_FALSE(hw.log.contains(fakeToken()));
    TEST_ASSERT_FALSE(hw.log.contains("No bot token"));
}

void test_app_reports_bad_setup_file() {
    FakeHardware hw;
    hw.files.writeFile(paths::kSetupFile, "CHILD_NAME=Child\n");
    App app(hw.hardware);
    app.begin();
    TEST_ASSERT_TRUE(hw.log.contains("BOT_TOKEN is missing"));
    TEST_ASSERT_TRUE(hw.log.contains("No bot token"));
    TEST_ASSERT_TRUE(hw.files.exists(paths::kSetupFile));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_good_file);
    RUN_TEST(test_forgiving_about_text_editors);
    RUN_TEST(test_password_can_contain_equals_sign);
    RUN_TEST(test_warns_about_unknown_and_repeated_lines);
    RUN_TEST(test_problems_with_bad_lines);
    RUN_TEST(test_bot_token_shape);
    RUN_TEST(test_unedited_example_file_is_rejected);
    RUN_TEST(test_no_file_does_nothing);
    RUN_TEST(test_good_file_is_saved_then_deleted);
    RUN_TEST(test_file_is_blanked_before_delete);
    RUN_TEST(test_first_setup_needs_token_name_and_admin);
    RUN_TEST(test_wifi_is_optional);
    RUN_TEST(test_later_file_only_needs_what_changes);
    RUN_TEST(test_later_file_can_replace_token_and_add_admin);
    RUN_TEST(test_bad_later_file_changes_nothing);
    RUN_TEST(test_file_kept_if_internal_memory_fails);
    RUN_TEST(test_messages_never_contain_secrets);
    RUN_TEST(test_app_reads_setup_file_on_start);
    RUN_TEST(test_app_reports_bad_setup_file);
    return UNITY_END();
}

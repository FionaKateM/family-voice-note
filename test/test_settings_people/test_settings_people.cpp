// Settings and the admin, monitor and contact lists.

#include <unity.h>

#include <string>

#include "app/storage/DeviceSettings.h"
#include "app/storage/People.h"
#include "fakes/Fakes.h"

using namespace fvb;
using fvb::fakes::FakeSettingsStore;

void setUp() {}
void tearDown() {}

// ---- Settings ----

void test_defaults_match_requirements() {
    FakeSettingsStore store;
    DeviceSettings s(store);
    TEST_ASSERT_EQUAL(70, s.voiceVolume());
    TEST_ASSERT_EQUAL(50, s.alertVolume());
    TEST_ASSERT_EQUAL(19 * 60, s.quietStart());
    TEST_ASSERT_EQUAL(7 * 60, s.quietEnd());
    TEST_ASSERT_EQUAL(360, s.maxRecordingSeconds());
    TEST_ASSERT_EQUAL(60, s.keepDays());
    TEST_ASSERT_TRUE(s.monitorMode() == MonitorMode::Off);
    TEST_ASSERT_FALSE(s.hasBotToken());
    TEST_ASSERT_EQUAL_STRING("", s.timezone().c_str());
    TEST_ASSERT_EQUAL(0, s.wifiNetworks().size());
}

void test_settings_are_saved() {
    FakeSettingsStore store;
    {
        DeviceSettings s(store);
        TEST_ASSERT_TRUE(s.setVoiceVolume(40));
        TEST_ASSERT_TRUE(s.setAlertVolume(0));
        TEST_ASSERT_TRUE(s.setQuietHours(20 * 60 + 30, 6 * 60));
        TEST_ASSERT_TRUE(s.setMaxRecordingSeconds(120));
        TEST_ASSERT_TRUE(s.setKeepDays(0));
        TEST_ASSERT_TRUE(s.setMonitorMode(MonitorMode::SentOnly));
        TEST_ASSERT_TRUE(s.setTimezone("Europe/London"));
        TEST_ASSERT_TRUE(s.setChildName("  Child  "));
    }
    DeviceSettings again(store);
    TEST_ASSERT_EQUAL(40, again.voiceVolume());
    TEST_ASSERT_EQUAL(0, again.alertVolume());
    TEST_ASSERT_EQUAL(20 * 60 + 30, again.quietStart());
    TEST_ASSERT_EQUAL(6 * 60, again.quietEnd());
    TEST_ASSERT_EQUAL(120, again.maxRecordingSeconds());
    TEST_ASSERT_EQUAL(0, again.keepDays());
    TEST_ASSERT_TRUE(again.monitorMode() == MonitorMode::SentOnly);
    TEST_ASSERT_EQUAL_STRING("Europe/London", again.timezone().c_str());
    TEST_ASSERT_EQUAL_STRING("Child", again.childName().c_str());
}

void test_settings_refuse_nonsense() {
    FakeSettingsStore store;
    DeviceSettings s(store);
    TEST_ASSERT_FALSE(s.setVoiceVolume(101));
    TEST_ASSERT_FALSE(s.setQuietHours(24 * 60, 0));
    TEST_ASSERT_FALSE(s.setQuietHours(0, 24 * 60));
    TEST_ASSERT_FALSE(s.setMaxRecordingSeconds(5));
    TEST_ASSERT_FALSE(s.setMaxRecordingSeconds(16 * 60));
    TEST_ASSERT_FALSE(s.setKeepDays(5000));
    TEST_ASSERT_FALSE(s.setChildName("   "));
    TEST_ASSERT_FALSE(s.setBotToken(""));
    TEST_ASSERT_EQUAL(70, s.voiceVolume());
    TEST_ASSERT_EQUAL(19 * 60, s.quietStart());
}

void test_quiet_hours_can_cross_midnight_or_not() {
    FakeSettingsStore store;
    DeviceSettings s(store);
    TEST_ASSERT_TRUE(s.setQuietHours(13 * 60, 15 * 60));
    TEST_ASSERT_EQUAL(13 * 60, s.quietStart());
    TEST_ASSERT_EQUAL(15 * 60, s.quietEnd());
}

void test_unknown_monitor_mode_reads_as_off() {
    FakeSettingsStore store;
    store.values["monitor_mode"] = "9";
    DeviceSettings s(store);
    TEST_ASSERT_TRUE(s.monitorMode() == MonitorMode::Off);
}

void test_wifi_newest_first_and_updates_password() {
    FakeSettingsStore store;
    DeviceSettings s(store);
    TEST_ASSERT_TRUE(s.addWifiNetwork({"Home", "one"}));
    TEST_ASSERT_TRUE(s.addWifiNetwork({"Granny's\tWiFi", "pass\nword"}));
    TEST_ASSERT_TRUE(s.addWifiNetwork({"Home", "two"}));

    const auto list = s.wifiNetworks();
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL_STRING("Home", list[0].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("two", list[0].password.c_str());
    TEST_ASSERT_EQUAL_STRING("Granny's\tWiFi", list[1].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("pass\nword", list[1].password.c_str());
}

void test_wifi_open_network_and_remove() {
    FakeSettingsStore store;
    DeviceSettings s(store);
    TEST_ASSERT_TRUE(s.addWifiNetwork({"Cafe", ""}));
    TEST_ASSERT_EQUAL(1, s.wifiNetworks().size());
    TEST_ASSERT_EQUAL_STRING("", s.wifiNetworks()[0].password.c_str());
    TEST_ASSERT_FALSE(s.addWifiNetwork({"", "x"}));
    TEST_ASSERT_TRUE(s.removeWifiNetwork("Cafe"));
    TEST_ASSERT_FALSE(s.removeWifiNetwork("Cafe"));
    TEST_ASSERT_EQUAL(0, s.wifiNetworks().size());
}

void test_wifi_forgets_oldest_when_full() {
    FakeSettingsStore store;
    DeviceSettings s(store);
    for (int i = 1; i <= 6; ++i) s.addWifiNetwork({"Net" + std::to_string(i), "pw"});
    const auto list = s.wifiNetworks();
    TEST_ASSERT_EQUAL(DeviceSettings::kMaxWifiNetworks, list.size());
    TEST_ASSERT_EQUAL_STRING("Net6", list.front().ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("Net2", list.back().ssid.c_str());
}

// ---- Admins and monitors ----

void test_admins_add_find_and_rename() {
    FakeSettingsStore store;
    People p(store);
    TEST_ASSERT_TRUE(p.addAdmin({111, ""}));
    TEST_ASSERT_TRUE(p.addAdmin({222, "Parent"}));
    TEST_ASSERT_TRUE(p.isAdmin(111));
    TEST_ASSERT_FALSE(p.isAdmin(333));

    // Adding again fills in the name rather than duplicating.
    TEST_ASSERT_TRUE(p.addAdmin({111, "Maintainer"}));
    const auto list = p.admins();
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL_STRING("Maintainer", list[0].name.c_str());

    // Adding again without a name keeps the old name.
    TEST_ASSERT_TRUE(p.addAdmin({111, ""}));
    TEST_ASSERT_EQUAL_STRING("Maintainer", p.admins()[0].name.c_str());
    TEST_ASSERT_FALSE(p.addAdmin({0, "Nobody"}));
}

void test_last_admin_cannot_be_removed() {
    FakeSettingsStore store;
    People p(store);
    p.addAdmin({111, "A"});
    p.addAdmin({222, "B"});
    TEST_ASSERT_TRUE(p.removeAdmin(111));
    TEST_ASSERT_FALSE(p.removeAdmin(222));
    TEST_ASSERT_TRUE(p.isAdmin(222));
    TEST_ASSERT_FALSE(p.removeAdmin(999));
}

void test_admin_list_is_capped() {
    FakeSettingsStore store;
    People p(store);
    for (int64_t i = 1; i <= static_cast<int64_t>(People::kMaxAdmins); ++i) TEST_ASSERT_TRUE(p.addAdmin({i, "x"}));
    TEST_ASSERT_FALSE(p.addAdmin({999, "one too many"}));
}

void test_monitors_are_separate_from_admins() {
    FakeSettingsStore store;
    People p(store);
    p.addAdmin({111, "Parent"});
    TEST_ASSERT_TRUE(p.addMonitor({111, "Parent"}));
    TEST_ASSERT_TRUE(p.addMonitor({222, "Other parent"}));
    TEST_ASSERT_TRUE(p.isMonitor(222));
    TEST_ASSERT_FALSE(p.isAdmin(222));
    TEST_ASSERT_TRUE(p.removeMonitor(111));
    TEST_ASSERT_TRUE(p.isAdmin(111));
    TEST_ASSERT_FALSE(p.isMonitor(111));
    // Monitors can all be removed; only admins have the last-one rule.
    TEST_ASSERT_TRUE(p.removeMonitor(222));
    TEST_ASSERT_EQUAL(0, p.monitors().size());
}

void test_big_telegram_ids_survive() {
    FakeSettingsStore store;
    People p(store);
    const int64_t big = 7000000000LL;  // newer Telegram IDs go past 32 bits
    p.addAdmin({big, ""});
    TEST_ASSERT_TRUE(p.isAdmin(big));
    TEST_ASSERT_TRUE(p.admins()[0].telegramId == big);
}

// ---- Contacts ----

Contact makeContact(int64_t id, const std::string& name) {
    Contact c;
    c.telegramId = id;
    c.name = name;
    c.timezone = "Europe/Madrid";
    return c;
}

void test_contacts_get_numbers_in_order() {
    FakeSettingsStore store;
    People p(store);
    TEST_ASSERT_EQUAL(1, p.addContact(makeContact(500, "Granny")));
    TEST_ASSERT_EQUAL(2, p.addContact(makeContact(600, "Grandad")));

    const auto list = p.contacts();
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL_STRING("Granny", list[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Europe/Madrid", list[0].timezone.c_str());
    TEST_ASSERT_EQUAL(Contact::kDefaultSleepStart, list[0].sleepStart);
    TEST_ASSERT_EQUAL(Contact::kDefaultSleepEnd, list[0].sleepEnd);
    TEST_ASSERT_EQUAL(2, list[1].number);
}

void test_contact_numbers_are_never_reused() {
    FakeSettingsStore store;
    People p(store);
    p.addContact(makeContact(500, "Granny"));
    const uint32_t second = p.addContact(makeContact(600, "Grandad"));
    TEST_ASSERT_TRUE(p.removeContact(second));
    TEST_ASSERT_EQUAL(3, p.addContact(makeContact(700, "Auntie")));
}

void test_same_person_cannot_be_added_twice() {
    FakeSettingsStore store;
    People p(store);
    p.addContact(makeContact(500, "Granny"));
    TEST_ASSERT_EQUAL(0, p.addContact(makeContact(500, "Granny again")));
    TEST_ASSERT_EQUAL(0, p.addContact(makeContact(0, "No ID")));
}

void test_find_and_update_contact() {
    FakeSettingsStore store;
    People p(store);
    const uint32_t n = p.addContact(makeContact(500, "Granny"));

    Contact c;
    TEST_ASSERT_TRUE(p.findContactByTelegramId(500, c));
    TEST_ASSERT_EQUAL(n, c.number);
    c.name = "Nana\twith tab";
    c.sleepStart = 22 * 60;
    c.sleepEnd = 8 * 60;
    TEST_ASSERT_TRUE(p.updateContact(c));

    Contact back;
    TEST_ASSERT_TRUE(p.findContact(n, back));
    TEST_ASSERT_EQUAL_STRING("Nana\twith tab", back.name.c_str());
    TEST_ASSERT_EQUAL(22 * 60, back.sleepStart);
    TEST_ASSERT_TRUE(p.isContact(500));
    TEST_ASSERT_FALSE(p.isContact(501));

    Contact missing = back;
    missing.number = 99;
    TEST_ASSERT_FALSE(p.updateContact(missing));
}

void test_remove_contact() {
    FakeSettingsStore store;
    People p(store);
    const uint32_t n = p.addContact(makeContact(500, "Granny"));
    TEST_ASSERT_TRUE(p.removeContact(n));
    TEST_ASSERT_FALSE(p.isContact(500));
    TEST_ASSERT_FALSE(store.has("ct1"));
    TEST_ASSERT_FALSE(p.removeContact(n));
}

void test_removed_contacts_are_remembered() {
    FakeSettingsStore store;
    People p(store);
    const uint32_t n = p.addContact(makeContact(500, "Granny"));
    TEST_ASSERT_FALSE(p.wasRemoved(500));
    TEST_ASSERT_TRUE(p.removeContact(n));
    TEST_ASSERT_TRUE(p.wasRemoved(500));
    TEST_ASSERT_FALSE(p.wasRemoved(501));
    TEST_ASSERT_FALSE(p.shouldTellRemoved(501, 1000));  // a stranger

    // Approved as a contact again: forgotten.
    TEST_ASSERT_TRUE(p.addContact(makeContact(500, "Granny")) != 0);
    TEST_ASSERT_FALSE(p.wasRemoved(500));
    TEST_ASSERT_FALSE(store.has("removed"));
}

void test_removed_reply_is_at_most_once_a_day() {
    FakeSettingsStore store;
    People p(store);
    p.removeContact(p.addContact(makeContact(500, "Granny")));
    const int64_t day = People::kRemovedReplyGapSeconds;
    TEST_ASSERT_TRUE(p.shouldTellRemoved(500, 1000000));
    TEST_ASSERT_FALSE(p.shouldTellRemoved(500, 1000000));
    TEST_ASSERT_FALSE(p.shouldTellRemoved(500, 1000000 + day - 1));
    TEST_ASSERT_TRUE(p.shouldTellRemoved(500, 1000000 + day));
    // A clock that jumped backwards doesn't keep them waiting.
    TEST_ASSERT_TRUE(p.shouldTellRemoved(500, 5000));
}

void test_removed_reply_without_a_clock() {
    FakeSettingsStore store;
    People p(store);
    p.removeContact(p.addContact(makeContact(500, "Granny")));
    TEST_ASSERT_TRUE(p.shouldTellRemoved(500, 0));   // told once
    TEST_ASSERT_FALSE(p.shouldTellRemoved(500, 0));  // and not again until the clock is set
    TEST_ASSERT_TRUE(p.shouldTellRemoved(500, 1000000));
    TEST_ASSERT_FALSE(p.shouldTellRemoved(500, 0));
}

void test_removed_list_is_capped_and_forgets_the_oldest() {
    FakeSettingsStore store;
    People p(store);
    for (size_t i = 0; i <= People::kMaxRemoved; ++i) {
        p.removeContact(p.addContact(makeContact(1000 + static_cast<int64_t>(i), "Person " + std::to_string(i))));
    }
    TEST_ASSERT_FALSE(p.wasRemoved(1000));  // the oldest
    TEST_ASSERT_TRUE(p.wasRemoved(1001));
    TEST_ASSERT_TRUE(p.wasRemoved(1000 + static_cast<int64_t>(People::kMaxRemoved)));
}

void test_removed_reply_is_not_sent_if_it_cannot_be_saved() {
    FakeSettingsStore store;
    People p(store);
    p.removeContact(p.addContact(makeContact(500, "Granny")));
    store.failWrites = true;
    TEST_ASSERT_FALSE(p.shouldTellRemoved(500, 1000000));
}

void test_contact_list_ignores_missing_records() {
    // As if the power went between saving the list and saving the contact.
    FakeSettingsStore store;
    People p(store);
    p.addContact(makeContact(500, "Granny"));
    store.values["contacts"] = "1,2";
    TEST_ASSERT_EQUAL(1, p.contacts().size());
}

void test_contact_list_is_capped() {
    FakeSettingsStore store;
    People p(store);
    for (size_t i = 1; i <= People::kMaxContacts; ++i) {
        TEST_ASSERT_NOT_EQUAL(0, p.addContact(makeContact(static_cast<int64_t>(1000 + i), "C")));
    }
    TEST_ASSERT_EQUAL(0, p.addContact(makeContact(5000, "one too many")));
}

void test_failed_save_is_reported() {
    FakeSettingsStore store;
    store.failWrites = true;
    People p(store);
    DeviceSettings s(store);
    TEST_ASSERT_FALSE(p.addAdmin({111, ""}));
    TEST_ASSERT_EQUAL(0, p.addContact(makeContact(500, "Granny")));
    TEST_ASSERT_FALSE(s.setVoiceVolume(10));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_match_requirements);
    RUN_TEST(test_settings_are_saved);
    RUN_TEST(test_settings_refuse_nonsense);
    RUN_TEST(test_quiet_hours_can_cross_midnight_or_not);
    RUN_TEST(test_unknown_monitor_mode_reads_as_off);
    RUN_TEST(test_wifi_newest_first_and_updates_password);
    RUN_TEST(test_wifi_open_network_and_remove);
    RUN_TEST(test_wifi_forgets_oldest_when_full);
    RUN_TEST(test_admins_add_find_and_rename);
    RUN_TEST(test_last_admin_cannot_be_removed);
    RUN_TEST(test_admin_list_is_capped);
    RUN_TEST(test_monitors_are_separate_from_admins);
    RUN_TEST(test_big_telegram_ids_survive);
    RUN_TEST(test_contacts_get_numbers_in_order);
    RUN_TEST(test_contact_numbers_are_never_reused);
    RUN_TEST(test_same_person_cannot_be_added_twice);
    RUN_TEST(test_find_and_update_contact);
    RUN_TEST(test_remove_contact);
    RUN_TEST(test_removed_contacts_are_remembered);
    RUN_TEST(test_removed_reply_is_at_most_once_a_day);
    RUN_TEST(test_removed_reply_without_a_clock);
    RUN_TEST(test_removed_list_is_capped_and_forgets_the_oldest);
    RUN_TEST(test_removed_reply_is_not_sent_if_it_cannot_be_saved);
    RUN_TEST(test_contact_list_ignores_missing_records);
    RUN_TEST(test_contact_list_is_capped);
    RUN_TEST(test_failed_save_is_reported);
    return UNITY_END();
}

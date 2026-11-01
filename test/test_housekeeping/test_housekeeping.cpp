// Housekeeping (ticket 1.9): clearing the bin and old messages, and the storage,
// offline and low battery alerts.

#include <unity.h>

#include <string>
#include <vector>

#include "app/App.h"
#include "app/housekeeping/Housekeeping.h"
#include "app/storage/Paths.h"
#include "app/storage/Storage.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;

void setUp() {}
void tearDown() {}

constexpr int64_t kNow = 1793543525;  // Sunday 2026-11-01 14:32:05 UTC, the same in London
constexpr int64_t kHour = 60 * 60;
constexpr int64_t kDay = 24 * kHour;
constexpr int64_t kSunday10am = 1793527200;  // 2026-11-01 10:00 UTC
constexpr int64_t kParent = 1001;
constexpr int64_t kOtherParent = 1002;

struct Box {
    FakeFileStorage files;
    FakeSettingsStore store;
    FakeClock clock;
    FakePower power;
    FakeLogger log;
    Storage storage{files, store};
    Housekeeping hk{storage, store, clock, power, log};
    uint32_t granny = 0;

    Box() {
        clock.timeSet = true;
        clock.unixSeconds = kNow;
        clock.bootMs = 1000;
        storage.settings.setChildName("Child");
        storage.settings.setTimezone("Europe/London");
        storage.people.addAdmin(Person{kParent, "Parent"});
        storage.people.addAdmin(Person{kOtherParent, "Other parent"});
        Contact c;
        c.telegramId = 5005;
        c.name = "Granny";
        c.timezone = "Europe/London";
        granny = storage.people.addContact(c);
    }

    // Text of every notice waiting to go, in order.
    std::vector<Job> notices() const {
        std::vector<Job> out;
        for (const auto& j : const_cast<Storage&>(storage).jobs.jobs()) {
            if (j.kind == Job::Kind::Notice) out.push_back(j);
        }
        return out;
    }

    uint32_t addMessage(int64_t time, StoredMessage::Status status) {
        StoredMessage m;
        m.time = time;
        m.status = status;
        const bool sent = status == StoredMessage::Status::Waiting || status == StoredMessage::Status::Delivered;
        m.direction = sent ? StoredMessage::Direction::Sent : StoredMessage::Direction::Received;
        const char* source = sent ? paths::kRecording : paths::kDownload;
        files.writeFile(source, "audio");
        return storage.messages.add(granny, m, source);
    }

    bool hasMessage(uint32_t id) {
        StoredMessage m;
        return storage.messages.find(granny, id, m);
    }
};

// ---------------------------------------------------------------------------
// The bin

void test_bin_is_cleared_after_30_days() {
    Box box;
    box.files.writeFile(paths::kRecording, "old");
    const std::string oldPath = box.storage.bin.add(paths::kRecording, box.granny, "Granny", kNow - 31 * kDay);
    box.files.writeFile(paths::kRecording, "new");
    const std::string newPath = box.storage.bin.add(paths::kRecording, box.granny, "Granny", kNow - 29 * kDay);

    box.hk.cleanUp();
    TEST_ASSERT_FALSE(box.files.exists(oldPath));
    TEST_ASSERT_TRUE(box.files.exists(newPath));
}

void test_nothing_dated_is_removed_before_the_clock_is_set() {
    Box box;
    box.files.writeFile(paths::kRecording, "old");
    const std::string binned = box.storage.bin.add(paths::kRecording, box.granny, "Granny", kNow - 31 * kDay);
    const uint32_t heard = box.addMessage(kNow - 90 * kDay, StoredMessage::Status::Heard);

    box.clock.timeSet = false;
    box.hk.cleanUp();
    TEST_ASSERT_TRUE(box.files.exists(binned));
    TEST_ASSERT_TRUE(box.hasMessage(heard));
}

// ---------------------------------------------------------------------------
// Old messages

void test_messages_older_than_keep_days_are_removed() {
    Box box;  // keep messages defaults to 60 days
    const uint32_t oldHeard = box.addMessage(kNow - 61 * kDay, StoredMessage::Status::Heard);
    const uint32_t oldSent = box.addMessage(kNow - 61 * kDay, StoredMessage::Status::Delivered);
    const uint32_t recent = box.addMessage(kNow - 59 * kDay, StoredMessage::Status::Heard);

    StoredMessage m;
    box.storage.messages.find(box.granny, oldHeard, m);
    const std::string audio = box.storage.messages.audioPath(box.granny, m);

    TEST_ASSERT_EQUAL(2, box.hk.removeOldMessages(kNow));
    TEST_ASSERT_FALSE(box.hasMessage(oldHeard));
    TEST_ASSERT_FALSE(box.hasMessage(oldSent));
    TEST_ASSERT_TRUE(box.hasMessage(recent));
    TEST_ASSERT_FALSE(box.files.exists(audio));
}

void test_unread_and_waiting_messages_are_kept_however_old() {
    Box box;
    const uint32_t unread = box.addMessage(kNow - 100 * kDay, StoredMessage::Status::Unread);
    const uint32_t waiting = box.addMessage(kNow - 100 * kDay, StoredMessage::Status::Waiting);

    TEST_ASSERT_EQUAL(0, box.hk.removeOldMessages(kNow));
    TEST_ASSERT_TRUE(box.hasMessage(unread));
    TEST_ASSERT_TRUE(box.hasMessage(waiting));
}

void test_messages_from_before_the_clock_was_set_are_kept() {
    Box box;
    const uint32_t undated = box.addMessage(1000, StoredMessage::Status::Heard);
    TEST_ASSERT_EQUAL(0, box.hk.removeOldMessages(kNow));
    TEST_ASSERT_TRUE(box.hasMessage(undated));
}

void test_keep_days_of_zero_keeps_everything() {
    Box box;
    box.storage.settings.setKeepDays(0);
    const uint32_t ancient = box.addMessage(kNow - 1000 * kDay, StoredMessage::Status::Heard);
    TEST_ASSERT_EQUAL(0, box.hk.removeOldMessages(kNow));
    TEST_ASSERT_TRUE(box.hasMessage(ancient));
}

void test_shorter_keep_days_takes_effect_at_the_next_clear_out() {
    Box box;
    const uint32_t tenDays = box.addMessage(kNow - 10 * kDay, StoredMessage::Status::Heard);
    box.hk.cleanUp();
    TEST_ASSERT_TRUE(box.hasMessage(tenDays));

    box.storage.settings.setKeepDays(7);
    box.hk.cleanUp();
    TEST_ASSERT_FALSE(box.hasMessage(tenDays));
}

// ---------------------------------------------------------------------------
// Storage alert

void test_storage_alert_goes_to_every_admin_at_80_percent() {
    Box box;
    box.files.capacity = 1000ULL * 1000 * 1000;  // 1,000 MB
    box.files.extraUsed = 790ULL * 1000 * 1000;
    box.hk.checkStorage();
    TEST_ASSERT_EQUAL(0, box.notices().size());

    box.files.extraUsed = 805ULL * 1000 * 1000;
    box.hk.checkStorage();
    const auto notices = box.notices();
    TEST_ASSERT_EQUAL(2, notices.size());
    TEST_ASSERT_TRUE(notices[0].chatId == kParent);
    TEST_ASSERT_TRUE(notices[1].chatId == kOtherParent);
    TEST_ASSERT_TRUE(notices[0].text.find("Child's box is 80% full (805 MB of 1,000 MB).") == 0);
    TEST_ASSERT_TRUE(notices[0].text.find("older than 60 days") != std::string::npos);
    TEST_ASSERT_TRUE(notices[0].text.find("/keepmessages") != std::string::npos);
}

void test_storage_alert_is_sent_once_until_space_is_freed() {
    Box box;
    box.files.capacity = 1000ULL * 1000 * 1000;
    box.files.extraUsed = 850ULL * 1000 * 1000;
    box.hk.checkStorage();
    box.hk.checkStorage();
    TEST_ASSERT_EQUAL(2, box.notices().size());  // one per admin

    // A restart doesn't send it again.
    Housekeeping again(box.storage, box.store, box.clock, box.power, box.log);
    again.checkStorage();
    TEST_ASSERT_EQUAL(2, box.notices().size());

    // Dropping to 78% isn't enough to send it again.
    box.files.extraUsed = 780ULL * 1000 * 1000;
    box.hk.checkStorage();
    box.files.extraUsed = 850ULL * 1000 * 1000;
    box.hk.checkStorage();
    TEST_ASSERT_EQUAL(2, box.notices().size());

    // Below 75% and back up is.
    box.files.extraUsed = 500ULL * 1000 * 1000;
    box.hk.checkStorage();
    box.files.extraUsed = 850ULL * 1000 * 1000;
    box.hk.checkStorage();
    TEST_ASSERT_EQUAL(4, box.notices().size());
}

void test_storage_alert_when_messages_are_kept_forever() {
    Box box;
    box.storage.settings.setKeepDays(0);
    box.files.capacity = 1000;
    box.files.writeFile("/big", std::string(900, 'x'));
    box.hk.checkStorage();
    TEST_ASSERT_TRUE(box.notices()[0].text.find("kept forever") != std::string::npos);
}

void test_no_storage_alert_without_a_card() {
    Box box;
    box.files.capacity = 1000;
    box.files.writeFile("/big", std::string(900, 'x'));
    box.files.mounted = false;
    box.hk.checkStorage();
    TEST_ASSERT_EQUAL(0, box.notices().size());
}

// ---------------------------------------------------------------------------
// Offline alert

void test_offline_alert_after_more_than_a_day() {
    Box box;
    box.clock.unixSeconds = kSunday10am;
    box.hk.setLastOnline(kSunday10am - 40 * kHour);  // Friday 18:00
    box.hk.checkOffline(true);

    const auto notices = box.notices();
    TEST_ASSERT_EQUAL(2, notices.size());
    TEST_ASSERT_EQUAL_STRING(
        "Child's box was offline from Friday 18:00 to Sunday 10:00. Voice notes sent before Saturday 10:00 may not "
        "have arrived. Ask family to resend if needed.",
        notices[0].text.c_str());
    TEST_ASSERT_TRUE(box.hk.lastOnline() == kSunday10am);
}

void test_no_offline_alert_for_less_than_a_day() {
    Box box;
    box.hk.setLastOnline(kNow - 23 * kHour);
    box.hk.checkOffline(true);
    TEST_ASSERT_EQUAL(0, box.notices().size());
}

void test_no_offline_alert_the_very_first_time() {
    Box box;
    box.hk.checkOffline(true);
    TEST_ASSERT_EQUAL(0, box.notices().size());
    TEST_ASSERT_TRUE(box.hk.lastOnline() == kNow);
}

void test_offline_alert_waits_until_back_online_with_the_clock_set() {
    Box box;
    box.hk.setLastOnline(kNow - 2 * kDay);
    box.hk.checkOffline(false);
    box.clock.timeSet = false;
    box.hk.checkOffline(true);
    TEST_ASSERT_EQUAL(0, box.notices().size());

    box.clock.timeSet = true;
    box.hk.checkOffline(true);
    TEST_ASSERT_EQUAL(2, box.notices().size());
}

void test_offline_alert_when_wifi_drops_while_running() {
    Box box;
    box.hk.checkOffline(true);
    box.clock.unixSeconds += 60;
    box.hk.checkOffline(true);

    box.clock.unixSeconds += 30 * kHour;
    box.hk.checkOffline(false);
    TEST_ASSERT_EQUAL(0, box.notices().size());
    box.hk.checkOffline(true);
    TEST_ASSERT_EQUAL(2, box.notices().size());

    // Staying online doesn't send it again.
    box.clock.unixSeconds += 10 * 60;
    box.hk.checkOffline(true);
    TEST_ASSERT_EQUAL(2, box.notices().size());
}

void test_last_online_is_saved_every_five_minutes_not_every_loop() {
    Box box;
    box.hk.checkOffline(true);
    box.clock.unixSeconds = kNow + 4 * 60;
    box.hk.checkOffline(true);
    TEST_ASSERT_TRUE(box.hk.lastOnline() == kNow);
    box.clock.unixSeconds = kNow + 5 * 60;
    box.hk.checkOffline(true);
    TEST_ASSERT_TRUE(box.hk.lastOnline() == kNow + 5 * 60);
}

void test_offline_text_includes_dates_after_a_week() {
    Box box;
    const std::string text = box.hk.offlineText(kSunday10am - 10 * kDay, kSunday10am);
    TEST_ASSERT_EQUAL_STRING(
        "Child's box was offline from Thursday 22 October 11:00 to Sunday 1 November 10:00. Voice notes sent before "
        "Saturday 31 October 10:00 may not have arrived. Ask family to resend if needed.",
        text.c_str());
}

void test_offline_text_says_utc_without_a_timezone() {
    FakeFileStorage files;
    FakeSettingsStore store;
    FakeClock clock;
    FakePower power;
    FakeLogger log;
    Storage storage(files, store);
    storage.settings.setChildName("Child");
    Housekeeping hk(storage, store, clock, power, log);
    const std::string text = hk.offlineText(kSunday10am - 40 * kHour, kSunday10am);
    TEST_ASSERT_TRUE(text.find("from Friday 18:00 UTC to Sunday 10:00 UTC") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Low battery

void onBattery(Box& box, int percent) {
    box.power.pluggedIn = false;
    box.power.charging = false;
    box.power.battery = static_cast<int8_t>(percent);
    box.hk.checkBattery();
}

void test_battery_alerts_at_20_10_and_5_percent() {
    Box box;
    onBattery(box, 21);
    TEST_ASSERT_EQUAL(0, box.notices().size());

    onBattery(box, 20);
    TEST_ASSERT_EQUAL(2, box.notices().size());
    TEST_ASSERT_EQUAL_STRING("Child's box battery is at 20%. Please charge it soon.", box.notices()[0].text.c_str());

    onBattery(box, 15);
    TEST_ASSERT_EQUAL(2, box.notices().size());

    onBattery(box, 10);
    TEST_ASSERT_EQUAL(4, box.notices().size());

    onBattery(box, 4);
    TEST_ASSERT_EQUAL(6, box.notices().size());
    TEST_ASSERT_EQUAL_STRING("Child's box battery is at 4%. It will switch off soon unless it's charged.",
                             box.notices()[4].text.c_str());

    onBattery(box, 2);
    TEST_ASSERT_EQUAL(6, box.notices().size());
}

void test_switching_on_with_a_low_battery_sends_one_alert() {
    Box box;
    onBattery(box, 8);
    TEST_ASSERT_EQUAL(2, box.notices().size());
    TEST_ASSERT_TRUE(box.notices()[0].text.find("at 8%") != std::string::npos);
    onBattery(box, 6);
    TEST_ASSERT_EQUAL(2, box.notices().size());
    onBattery(box, 5);
    TEST_ASSERT_EQUAL(4, box.notices().size());
}

void test_no_battery_alert_while_plugged_in_or_charging() {
    Box box;
    box.power.battery = 15;
    box.power.pluggedIn = true;
    box.hk.checkBattery();
    box.power.pluggedIn = false;
    box.power.charging = true;
    box.hk.checkBattery();
    TEST_ASSERT_EQUAL(0, box.notices().size());
}

void test_small_wobbles_in_the_reading_dont_repeat_an_alert() {
    Box box;
    onBattery(box, 20);
    onBattery(box, 22);
    onBattery(box, 24);
    onBattery(box, 20);
    TEST_ASSERT_EQUAL(2, box.notices().size());
}

void test_battery_alerts_come_again_after_charging() {
    Box box;
    onBattery(box, 9);
    TEST_ASSERT_EQUAL(2, box.notices().size());

    // Charged to 16%: 10% can alert again, 20% can't yet.
    onBattery(box, 16);
    onBattery(box, 10);
    TEST_ASSERT_EQUAL(4, box.notices().size());

    // Fully charged, then run down again.
    onBattery(box, 90);
    onBattery(box, 20);
    TEST_ASSERT_EQUAL(6, box.notices().size());
}

void test_battery_alert_survives_a_restart() {
    Box box;
    onBattery(box, 18);
    Housekeeping again(box.storage, box.store, box.clock, box.power, box.log);
    again.checkBattery();
    TEST_ASSERT_EQUAL(2, box.notices().size());
}

void test_unreadable_battery_is_ignored() {
    Box box;
    onBattery(box, -1);
    TEST_ASSERT_EQUAL(0, box.notices().size());
}

void test_battery_sound_waits_for_quiet_hours_to_end() {
    Box box;
    // 14:32 in London. Quiet hours 14:00 to 15:00.
    box.storage.settings.setQuietHours(14 * 60, 15 * 60);
    onBattery(box, 19);
    int level = 0;
    TEST_ASSERT_FALSE(box.hk.takeBatteryAlert(level));
    TEST_ASSERT_EQUAL(2, box.notices().size());  // admins are still told straight away

    box.clock.unixSeconds += 30 * 60;  // 15:02
    TEST_ASSERT_TRUE(box.hk.takeBatteryAlert(level));
    TEST_ASSERT_EQUAL(20, level);
    TEST_ASSERT_FALSE(box.hk.takeBatteryAlert(level));
}

// ---------------------------------------------------------------------------
// tick()

void test_clear_out_waits_for_the_clock_then_runs_hourly() {
    Box box;
    box.clock.timeSet = false;
    const uint32_t old = box.addMessage(kNow - 61 * kDay, StoredMessage::Status::Heard);
    box.hk.tick(false);
    TEST_ASSERT_TRUE(box.hasMessage(old));

    box.clock.timeSet = true;
    box.hk.tick(true);
    TEST_ASSERT_FALSE(box.hasMessage(old));

    const uint32_t next = box.addMessage(kNow - 61 * kDay, StoredMessage::Status::Heard);
    box.clock.bootMs += Housekeeping::kCleanUpEveryMs - 1;
    box.hk.tick(true);
    TEST_ASSERT_TRUE(box.hasMessage(next));
    box.clock.bootMs += 1;
    box.hk.tick(true);
    TEST_ASSERT_FALSE(box.hasMessage(next));
}

void test_app_runs_housekeeping_and_logs_the_battery_sound() {
    FakeHardware hw;
    hw.clock.timeSet = true;
    hw.clock.unixSeconds = kNow;
    hw.power.pluggedIn = false;
    hw.power.battery = 10;
    App app(hw.hardware);
    app.storage().people.addAdmin(Person{kParent, "Parent"});
    app.begin();
    app.tick();

    TEST_ASSERT_EQUAL(1, app.storage().jobs.size());
    TEST_ASSERT_TRUE(hw.log.contains("Low battery sound and lights (10%)"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bin_is_cleared_after_30_days);
    RUN_TEST(test_nothing_dated_is_removed_before_the_clock_is_set);
    RUN_TEST(test_messages_older_than_keep_days_are_removed);
    RUN_TEST(test_unread_and_waiting_messages_are_kept_however_old);
    RUN_TEST(test_messages_from_before_the_clock_was_set_are_kept);
    RUN_TEST(test_keep_days_of_zero_keeps_everything);
    RUN_TEST(test_shorter_keep_days_takes_effect_at_the_next_clear_out);
    RUN_TEST(test_storage_alert_goes_to_every_admin_at_80_percent);
    RUN_TEST(test_storage_alert_is_sent_once_until_space_is_freed);
    RUN_TEST(test_storage_alert_when_messages_are_kept_forever);
    RUN_TEST(test_no_storage_alert_without_a_card);
    RUN_TEST(test_offline_alert_after_more_than_a_day);
    RUN_TEST(test_no_offline_alert_for_less_than_a_day);
    RUN_TEST(test_no_offline_alert_the_very_first_time);
    RUN_TEST(test_offline_alert_waits_until_back_online_with_the_clock_set);
    RUN_TEST(test_offline_alert_when_wifi_drops_while_running);
    RUN_TEST(test_last_online_is_saved_every_five_minutes_not_every_loop);
    RUN_TEST(test_offline_text_includes_dates_after_a_week);
    RUN_TEST(test_offline_text_says_utc_without_a_timezone);
    RUN_TEST(test_battery_alerts_at_20_10_and_5_percent);
    RUN_TEST(test_switching_on_with_a_low_battery_sends_one_alert);
    RUN_TEST(test_no_battery_alert_while_plugged_in_or_charging);
    RUN_TEST(test_small_wobbles_in_the_reading_dont_repeat_an_alert);
    RUN_TEST(test_battery_alerts_come_again_after_charging);
    RUN_TEST(test_battery_alert_survives_a_restart);
    RUN_TEST(test_unreadable_battery_is_ignored);
    RUN_TEST(test_battery_sound_waits_for_quiet_hours_to_end);
    RUN_TEST(test_clear_out_waits_for_the_clock_then_runs_hourly);
    RUN_TEST(test_app_runs_housekeeping_and_logs_the_battery_sound);
    return UNITY_END();
}

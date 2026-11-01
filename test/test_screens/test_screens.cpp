// The rough child screens (ticket 1.11): moving between contacts, recording,
// review, history and playing. Checks the rules, not the look.

#include <unity.h>

#include <string>

#include "app/screens/Screens.h"
#include "app/storage/Paths.h"
#include "app/storage/Storage.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;
using Screen = Screens::Screen;

void setUp() {}
void tearDown() {}

namespace {

constexpr int64_t kNow = 1793543525;  // 2026-11-01 14:32:05 UTC

struct Box {
    FakeHardware hw;
    Storage storage{hw.files, hw.settings};
    Screens screens{hw.hardware, storage};

    Box() {
        hw.clock.timeSet = true;
        hw.clock.unixSeconds = kNow;
    }

    uint32_t addContact(const std::string& name) {
        Contact c;
        c.telegramId = 1000 + static_cast<int64_t>(storage.people.contacts().size());
        c.name = name;
        return storage.people.addContact(c);
    }

    uint32_t addMessage(uint32_t contact, StoredMessage::Direction direction, StoredMessage::Status status,
                        int64_t time = kNow) {
        hw.files.writeFile(paths::kDownload, "audio");
        StoredMessage m;
        m.direction = direction;
        m.status = status;
        m.time = time;
        m.durationMs = 5000;
        return storage.messages.add(contact, m, paths::kDownload);
    }

    uint32_t addUnread(uint32_t contact, int64_t time = kNow) {
        return addMessage(contact, StoredMessage::Direction::Received, StoredMessage::Status::Unread, time);
    }

    // Starts up and waits for the home screen.
    void start() {
        screens.begin();
        wait(Screens::kStartingScreenMs);
    }

    void wait(uint32_t ms) {
        hw.clock.bootMs += ms;
        screens.tick();
    }

    void press(Button button) {
        hw.input.press(button);
        deliver();
    }
    void tap(int16_t x, int16_t y) {
        hw.input.tap(x, y);
        deliver();
    }
    void swipe(SwipeDirection direction) {
        hw.input.swipe(direction);
        deliver();
    }
    void deliver() {
        InputEvent e;
        while (hw.input.pollEvent(e)) screens.handle(e);
        screens.tick();
    }

    // Presses record, "says" something for `ms`, and presses stop.
    void record(uint32_t ms) {
        press(Button::Middle);
        hw.files.writeFile(paths::kRecording, "mp3");  // what the mic would have written
        hw.audio.nextRecordingMs = ms;
        wait(ms);
        press(Button::Middle);
    }

    // The playing message reaches its end.
    void finishPlaying() {
        hw.audio.playing.clear();
        screens.tick();
    }

    size_t unread(uint32_t contact) const { return storage.messages.unreadCount(contact); }
};

constexpr int16_t kPhotoX = 160;
constexpr int16_t kPhotoY = 92;

}  // namespace

// ---- Starting up and moving between contacts ----

void test_starts_with_name_and_version_then_home() {
    Box box;
    box.addContact("Granny");
    box.screens.begin();
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Starting);
    TEST_ASSERT_TRUE(box.hw.display.shows("Family Voice Box"));

    box.wait(Screens::kStartingScreenMs - 1);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Starting);
    box.wait(1);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_TRUE(box.hw.display.shows("Granny"));
}

void test_no_contacts_screen_until_one_is_added() {
    Box box;
    box.start();
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::NoContacts);
    TEST_ASSERT_TRUE(box.hw.display.shows("No contacts yet"));

    // The bot adds a contact; the screen notices on its next look.
    const uint32_t granny = box.addContact("Granny");
    box.wait(Screens::kRefreshMs);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_EQUAL(granny, box.screens.currentContact());
}

void test_buttons_and_swipes_move_between_contacts_and_wrap_round() {
    Box box;
    const uint32_t a = box.addContact("Granny");
    const uint32_t b = box.addContact("Grandad");
    const uint32_t c = box.addContact("Auntie");
    box.start();
    TEST_ASSERT_EQUAL(a, box.screens.currentContact());

    box.press(Button::Right);
    TEST_ASSERT_EQUAL(b, box.screens.currentContact());
    TEST_ASSERT_TRUE(box.hw.display.shows("Grandad"));
    box.swipe(SwipeDirection::Left);
    TEST_ASSERT_EQUAL(c, box.screens.currentContact());
    box.press(Button::Right);
    TEST_ASSERT_EQUAL(a, box.screens.currentContact());
    box.press(Button::Left);
    TEST_ASSERT_EQUAL(c, box.screens.currentContact());
    box.swipe(SwipeDirection::Right);
    TEST_ASSERT_EQUAL(b, box.screens.currentContact());
}

void test_new_contact_does_not_move_the_child_along() {
    Box box;
    box.addContact("Granny");
    const uint32_t b = box.addContact("Grandad");
    box.start();
    box.press(Button::Right);

    box.addContact("Auntie");
    box.wait(Screens::kRefreshMs);
    TEST_ASSERT_EQUAL(b, box.screens.currentContact());
}

void test_removed_contact_falls_back_to_the_first() {
    Box box;
    const uint32_t a = box.addContact("Granny");
    const uint32_t b = box.addContact("Grandad");
    box.start();
    box.press(Button::Right);

    box.storage.removeContact(b);
    box.wait(Screens::kRefreshMs);
    TEST_ASSERT_EQUAL(a, box.screens.currentContact());
}

void test_input_ignored_while_starting() {
    Box box;
    box.addContact("Granny");
    box.screens.begin();
    box.press(Button::Middle);
    TEST_ASSERT_TRUE(box.hw.audio.recording.empty());
}

// ---- Recording and review ----

void test_middle_button_starts_recording() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.press(Button::Middle);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Recording);
    TEST_ASSERT_EQUAL_STRING(paths::kRecording, box.hw.audio.recording.c_str());
}

void test_recording_that_will_not_start_stays_home() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.hw.audio.canRecord = false;
    box.press(Button::Middle);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_TRUE(box.hw.log.contains("Couldn't start recording"));
}

void test_short_recording_is_thrown_away() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.record(Screens::kShortestRecordingMs - 1);

    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_FALSE(box.hw.files.exists(paths::kRecording));
    TEST_ASSERT_TRUE(box.storage.bin.entries().empty());  // not worth binning
    TEST_ASSERT_TRUE(box.storage.outbox.empty());
}

void test_recording_long_enough_goes_to_review() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.record(Screens::kShortestRecordingMs);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
    TEST_ASSERT_EQUAL(Screens::kShortestRecordingMs, box.screens.reviewDurationMs());
}

void test_tapping_anywhere_stops_recording() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.press(Button::Middle);
    box.hw.audio.nextRecordingMs = 3000;
    box.tap(10, 10);
    TEST_ASSERT_TRUE(box.hw.audio.recording.empty());
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
}

void test_recording_stops_at_the_time_limit() {
    Box box;
    box.addContact("Granny");
    box.storage.settings.setMaxRecordingSeconds(10);
    box.start();
    box.press(Button::Middle);
    box.hw.audio.nextRecordingMs = 10000;

    box.wait(9900);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Recording);
    box.wait(100);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
    TEST_ASSERT_TRUE(box.hw.audio.recording.empty());
}

void test_send_puts_recording_in_outbox_and_history_then_goes_home() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.start();
    box.record(4000);
    box.press(Button::Middle);  // send

    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_EQUAL(1, box.storage.outbox.size());
    const auto messages = box.storage.messages.messages(granny);
    TEST_ASSERT_EQUAL(1, messages.size());
    TEST_ASSERT_TRUE(messages[0].direction == StoredMessage::Direction::Sent);
    TEST_ASSERT_TRUE(messages[0].status == StoredMessage::Status::Waiting);
    TEST_ASSERT_EQUAL(4000, messages[0].durationMs);
    TEST_ASSERT_FALSE(box.hw.files.exists(paths::kRecording));
}

void test_send_goes_to_the_contact_on_screen() {
    Box box;
    box.addContact("Granny");
    const uint32_t grandad = box.addContact("Grandad");
    box.start();
    box.press(Button::Right);
    box.record(4000);
    box.tap(160, 200);  // the send icon

    TEST_ASSERT_EQUAL(1, box.storage.outbox.size());
    TEST_ASSERT_EQUAL(grandad, box.storage.outbox.entries()[0].contact);
}

void test_send_failure_stays_on_review() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.record(4000);
    box.hw.files.readOnly = true;
    box.press(Button::Middle);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
    TEST_ASSERT_TRUE(box.hw.log.contains("Couldn't save the recording"));
}

void test_listen_plays_the_recording_back() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.start();
    box.record(4000);
    box.press(Button::Left);
    TEST_ASSERT_EQUAL_STRING(paths::kRecording, box.hw.audio.playing.c_str());
    TEST_ASSERT_TRUE(box.screens.isPlayingRecording());

    box.press(Button::Left);  // stop
    TEST_ASSERT_TRUE(box.hw.audio.playing.empty());

    box.press(Button::Left);
    box.finishPlaying();
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
    TEST_ASSERT_TRUE(box.storage.messages.messages(granny).empty());
}

void test_delete_asks_first_and_no_goes_back() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.record(4000);
    box.press(Button::Right);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::ConfirmDelete);

    box.press(Button::Left);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
    TEST_ASSERT_TRUE(box.hw.files.exists(paths::kRecording));

    box.press(Button::Right);
    box.press(Button::Middle);  // middle is no as well
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
}

void test_delete_yes_moves_recording_to_bin() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.record(4000);
    box.press(Button::Right);
    box.press(Button::Right);

    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_EQUAL(1, box.storage.bin.entries().size());
    TEST_ASSERT_FALSE(box.hw.files.exists(paths::kRecording));
    TEST_ASSERT_TRUE(box.storage.outbox.empty());
}

void test_confirm_by_tapping_tick_and_cross() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.record(4000);
    box.tap(267, 200);  // delete icon
    box.tap(50, 190);   // cross
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Review);
    box.tap(267, 200);
    box.tap(270, 190);  // tick
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_EQUAL(1, box.storage.bin.entries().size());
}

void test_rerecord_asks_first_then_bins_and_records_again() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.record(4000);
    box.tap(280, 40);  // re-record icon
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::ConfirmRerecord);
    TEST_ASSERT_TRUE(box.hw.audio.recording.empty());

    box.press(Button::Right);
    TEST_ASSERT_EQUAL(1, box.storage.bin.entries().size());
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Recording);
    TEST_ASSERT_EQUAL_STRING(paths::kRecording, box.hw.audio.recording.c_str());
}

// ---- Playing on the home screen ----

void test_tapping_photo_plays_oldest_unread_and_marks_it_heard_at_the_end() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    const uint32_t first = box.addUnread(granny, kNow - 60);
    box.addUnread(granny, kNow);
    box.start();
    TEST_ASSERT_TRUE(box.hw.display.shows("2"));  // unread badge

    box.tap(kPhotoX, kPhotoY);
    TEST_ASSERT_EQUAL(first, box.screens.playingMessage());
    TEST_ASSERT_EQUAL(2, box.unread(granny));  // not heard until the end

    box.finishPlaying();
    TEST_ASSERT_EQUAL(0, box.screens.playingMessage());
    TEST_ASSERT_EQUAL(1, box.unread(granny));
    StoredMessage m;
    box.storage.messages.find(granny, first, m);
    TEST_ASSERT_TRUE(m.status == StoredMessage::Status::Heard);
}

void test_stopping_early_does_not_mark_heard() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.addUnread(granny);
    box.start();
    box.tap(kPhotoX, kPhotoY);
    box.tap(kPhotoX, kPhotoY);  // stop
    TEST_ASSERT_TRUE(box.hw.audio.playing.empty());
    box.screens.tick();
    TEST_ASSERT_EQUAL(1, box.unread(granny));
}

void test_changing_contact_stops_playing_without_marking_heard() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.addContact("Grandad");
    box.addUnread(granny);
    box.start();
    box.tap(kPhotoX, kPhotoY);
    box.press(Button::Right);
    TEST_ASSERT_TRUE(box.hw.audio.playing.empty());
    TEST_ASSERT_EQUAL(1, box.unread(granny));
}

void test_with_nothing_unread_plays_newest() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.addMessage(granny, StoredMessage::Direction::Received, StoredMessage::Status::Heard, kNow - 60);
    const uint32_t newest = box.addMessage(granny, StoredMessage::Direction::Sent, StoredMessage::Status::Delivered);
    box.start();
    box.tap(kPhotoX, kPhotoY);
    TEST_ASSERT_EQUAL(newest, box.screens.playingMessage());

    box.finishPlaying();  // a sent message stays as it was
    StoredMessage m;
    box.storage.messages.find(granny, newest, m);
    TEST_ASSERT_TRUE(m.status == StoredMessage::Status::Delivered);
}

void test_message_that_will_not_play_is_left_unread() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.addUnread(granny);
    box.start();
    box.hw.audio.canPlay = false;
    box.tap(kPhotoX, kPhotoY);
    TEST_ASSERT_EQUAL(0, box.screens.playingMessage());
    box.screens.tick();
    TEST_ASSERT_EQUAL(1, box.unread(granny));
    TEST_ASSERT_TRUE(box.hw.log.contains("Couldn't play"));
}

void test_recording_stops_playback_first() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.addUnread(granny);
    box.start();
    box.tap(kPhotoX, kPhotoY);
    box.press(Button::Middle);
    TEST_ASSERT_TRUE(box.hw.audio.playing.empty());
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Recording);
    TEST_ASSERT_EQUAL(1, box.unread(granny));
}

// ---- History ----

void test_history_lists_newest_first_and_plays_the_highlighted_message() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    const uint32_t older = box.addUnread(granny, kNow - 3600);
    const uint32_t newer = box.addMessage(granny, StoredMessage::Direction::Sent, StoredMessage::Status::Waiting);
    box.start();

    box.swipe(SwipeDirection::Up);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::History);
    TEST_ASSERT_EQUAL(0, box.screens.historySelection());

    box.press(Button::Middle);
    TEST_ASSERT_EQUAL(newer, box.screens.playingMessage());

    box.press(Button::Right);  // down to the older one
    TEST_ASSERT_EQUAL(1, box.screens.historySelection());
    box.press(Button::Middle);
    TEST_ASSERT_EQUAL(older, box.screens.playingMessage());
    box.finishPlaying();
    TEST_ASSERT_EQUAL(0, box.unread(granny));

    box.press(Button::Right);  // already at the bottom
    TEST_ASSERT_EQUAL(1, box.screens.historySelection());
}

void test_history_tap_plays_that_row_and_back_goes_home() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    const uint32_t older = box.addUnread(granny, kNow - 3600);
    box.addUnread(granny, kNow);
    box.start();
    box.tap(20, 20);  // the list icon
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::History);

    box.tap(160, 40 + 40 + 20);  // second row
    TEST_ASSERT_EQUAL(1, box.screens.historySelection());
    TEST_ASSERT_EQUAL(older, box.screens.playingMessage());

    box.tap(20, 20);  // back arrow
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
    TEST_ASSERT_TRUE(box.hw.audio.playing.empty());
}

void test_history_scrolls_to_keep_the_selection_in_view() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    for (int i = 0; i < 8; ++i) box.addUnread(granny, kNow - i);
    box.start();
    box.swipe(SwipeDirection::Up);
    for (int i = 0; i < 6; ++i) box.press(Button::Right);
    TEST_ASSERT_EQUAL(6, box.screens.historySelection());

    // Row 3 on screen is now message 6 - 5 + 1 + 3 = 5 in the list.
    box.tap(160, 40 + 3 * 40 + 20);
    TEST_ASSERT_EQUAL(5, box.screens.historySelection());

    box.swipe(SwipeDirection::Right);
    TEST_ASSERT_TRUE(box.screens.screen() == Screen::Home);
}

void test_history_shows_times_in_the_box_timezone() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.addUnread(granny, kNow);  // 14:32 UTC
    box.storage.settings.setTimezone("Asia/Tokyo");
    box.start();
    box.swipe(SwipeDirection::Up);
    TEST_ASSERT_TRUE(box.hw.display.shows("1 Nov 23:32"));
    TEST_ASSERT_TRUE(box.hw.display.shows("0:05"));
}

void test_empty_history() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.swipe(SwipeDirection::Up);
    TEST_ASSERT_TRUE(box.hw.display.shows("No messages yet"));
    box.press(Button::Middle);  // nothing to play, nothing breaks
    TEST_ASSERT_TRUE(box.hw.audio.playing.empty());
}

// ---- LEDs ----

void test_leds_follow_the_voice_while_recording() {
    Box box;
    box.addContact("Granny");
    box.start();
    box.press(Button::Middle);
    box.hw.audio.level = 50;
    box.wait(Screens::kFrameMs);

    int lit = 0;
    for (const Colour& c : box.hw.leds.shown) lit += c != Colour{} ? 1 : 0;
    TEST_ASSERT_EQUAL(5, lit);
}

void test_leds_pulse_when_something_is_unread_and_go_off_once_heard() {
    Box box;
    const uint32_t granny = box.addContact("Granny");
    box.addUnread(granny);
    box.start();
    box.wait(Screens::kFrameMs);
    box.wait(500);
    TEST_ASSERT_FALSE(box.hw.leds.allShown(Colour{}));

    box.tap(kPhotoX, kPhotoY);
    box.finishPlaying();
    box.wait(Screens::kFrameMs);
    TEST_ASSERT_TRUE(box.hw.leds.allShown(Colour{}));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_with_name_and_version_then_home);
    RUN_TEST(test_no_contacts_screen_until_one_is_added);
    RUN_TEST(test_buttons_and_swipes_move_between_contacts_and_wrap_round);
    RUN_TEST(test_new_contact_does_not_move_the_child_along);
    RUN_TEST(test_removed_contact_falls_back_to_the_first);
    RUN_TEST(test_input_ignored_while_starting);
    RUN_TEST(test_middle_button_starts_recording);
    RUN_TEST(test_recording_that_will_not_start_stays_home);
    RUN_TEST(test_short_recording_is_thrown_away);
    RUN_TEST(test_recording_long_enough_goes_to_review);
    RUN_TEST(test_tapping_anywhere_stops_recording);
    RUN_TEST(test_recording_stops_at_the_time_limit);
    RUN_TEST(test_send_puts_recording_in_outbox_and_history_then_goes_home);
    RUN_TEST(test_send_goes_to_the_contact_on_screen);
    RUN_TEST(test_send_failure_stays_on_review);
    RUN_TEST(test_listen_plays_the_recording_back);
    RUN_TEST(test_delete_asks_first_and_no_goes_back);
    RUN_TEST(test_delete_yes_moves_recording_to_bin);
    RUN_TEST(test_confirm_by_tapping_tick_and_cross);
    RUN_TEST(test_rerecord_asks_first_then_bins_and_records_again);
    RUN_TEST(test_tapping_photo_plays_oldest_unread_and_marks_it_heard_at_the_end);
    RUN_TEST(test_stopping_early_does_not_mark_heard);
    RUN_TEST(test_changing_contact_stops_playing_without_marking_heard);
    RUN_TEST(test_with_nothing_unread_plays_newest);
    RUN_TEST(test_message_that_will_not_play_is_left_unread);
    RUN_TEST(test_recording_stops_playback_first);
    RUN_TEST(test_history_lists_newest_first_and_plays_the_highlighted_message);
    RUN_TEST(test_history_tap_plays_that_row_and_back_goes_home);
    RUN_TEST(test_history_scrolls_to_keep_the_selection_in_view);
    RUN_TEST(test_history_shows_times_in_the_box_timezone);
    RUN_TEST(test_empty_history);
    RUN_TEST(test_leds_follow_the_voice_while_recording);
    RUN_TEST(test_leds_pulse_when_something_is_unread_and_go_off_once_heard);
    return UNITY_END();
}

// Per-contact message folders and index files, the outbox, the bin, and the
// jobs in Storage that use several of them at once.

#include <unity.h>

#include <string>

#include "app/storage/Paths.h"
#include "app/storage/Storage.h"
#include "fakes/Fakes.h"

using namespace fvb;
using fvb::fakes::FakeFileStorage;
using fvb::fakes::FakeSettingsStore;

void setUp() {}
void tearDown() {}

constexpr int64_t kNow = 1793543525;  // 2026-11-01 14:32:05 UTC
constexpr int64_t kDay = 24 * 60 * 60;

StoredMessage received(int64_t time = kNow, uint32_t durationMs = 4000) {
    StoredMessage m;
    m.direction = StoredMessage::Direction::Received;
    m.status = StoredMessage::Status::Unread;
    m.time = time;
    m.durationMs = durationMs;
    m.telegramMessageId = 77;
    m.telegramFileId = "file-abc";
    return m;
}

// ---- MessageStore ----

void test_add_moves_file_into_contact_folder() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile(paths::kDownload, "opus audio");

    const uint32_t id = store.add(3, received(), paths::kDownload);
    TEST_ASSERT_EQUAL(1, id);
    TEST_ASSERT_FALSE(files.exists(paths::kDownload));
    TEST_ASSERT_TRUE(files.exists("/contacts/3/0001.ogg"));
    TEST_ASSERT_TRUE(files.exists("/contacts/3/index.txt"));

    StoredMessage m;
    TEST_ASSERT_TRUE(store.find(3, id, m));
    TEST_ASSERT_EQUAL_STRING("0001.ogg", m.fileName.c_str());
    TEST_ASSERT_EQUAL_STRING("/contacts/3/0001.ogg", store.audioPath(3, m).c_str());
    TEST_ASSERT_EQUAL(4000, m.durationMs);
    TEST_ASSERT_TRUE(m.time == kNow);
    TEST_ASSERT_TRUE(m.telegramMessageId == 77);
    TEST_ASSERT_EQUAL_STRING("file-abc", m.telegramFileId.c_str());
}

void test_sent_messages_are_mp3() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile(paths::kRecording, "mp3");
    StoredMessage m;
    m.direction = StoredMessage::Direction::Sent;
    m.status = StoredMessage::Status::Waiting;
    const uint32_t id = store.add(1, m, paths::kRecording);
    TEST_ASSERT_TRUE(files.exists("/contacts/1/0001.mp3"));
    StoredMessage back;
    store.find(1, id, back);
    TEST_ASSERT_TRUE(back.direction == StoredMessage::Direction::Sent);
    TEST_ASSERT_TRUE(back.status == StoredMessage::Status::Waiting);
}

void test_messages_come_back_oldest_first_with_unread_count() {
    FakeFileStorage files;
    MessageStore store(files);
    for (int i = 0; i < 3; ++i) {
        files.writeFile(paths::kDownload, "a");
        store.add(2, received(kNow + i), paths::kDownload);
    }
    const auto list = store.messages(2);
    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_EQUAL(1, list[0].id);
    TEST_ASSERT_EQUAL(3, list[2].id);
    TEST_ASSERT_EQUAL(3, store.unreadCount(2));
    TEST_ASSERT_EQUAL(0, store.unreadCount(5));  // other contacts are separate
}

void test_mark_heard() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile(paths::kDownload, "a");
    const uint32_t id = store.add(2, received(), paths::kDownload);
    TEST_ASSERT_TRUE(store.markHeard(2, id));
    TEST_ASSERT_TRUE(store.markHeard(2, id));  // twice is fine
    TEST_ASSERT_EQUAL(0, store.unreadCount(2));
    TEST_ASSERT_FALSE(store.markHeard(2, 99));
}

void test_mark_delivered_only_for_sent() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile(paths::kDownload, "a");
    const uint32_t in = store.add(2, received(), paths::kDownload);
    TEST_ASSERT_FALSE(store.markDelivered(2, in, 1, "x"));

    files.writeFile(paths::kRecording, "b");
    StoredMessage sent;
    sent.direction = StoredMessage::Direction::Sent;
    sent.status = StoredMessage::Status::Waiting;
    const uint32_t out = store.add(2, sent, paths::kRecording);
    TEST_ASSERT_TRUE(store.markDelivered(2, out, 555, "tg-file"));
    StoredMessage back;
    store.find(2, out, back);
    TEST_ASSERT_TRUE(back.status == StoredMessage::Status::Delivered);
    TEST_ASSERT_TRUE(back.telegramMessageId == 555);
    TEST_ASSERT_EQUAL_STRING("tg-file", back.telegramFileId.c_str());
    TEST_ASSERT_FALSE(store.markHeard(2, out));
}

void test_remove_deletes_audio_and_ids_are_not_reused() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile(paths::kDownload, "a");
    store.add(2, received(), paths::kDownload);
    files.writeFile(paths::kDownload, "b");
    const uint32_t second = store.add(2, received(), paths::kDownload);

    TEST_ASSERT_TRUE(store.remove(2, second));
    TEST_ASSERT_FALSE(files.exists("/contacts/2/0002.ogg"));
    TEST_ASSERT_EQUAL(1, store.messages(2).size());
    TEST_ASSERT_FALSE(store.remove(2, second));

    files.writeFile(paths::kDownload, "c");
    TEST_ASSERT_EQUAL(3, store.add(2, received(), paths::kDownload));
}

void test_add_fails_cleanly() {
    FakeFileStorage files;
    MessageStore store(files);
    TEST_ASSERT_EQUAL(0, store.add(2, received(), "/work/missing.ogg"));
    files.writeFile(paths::kDownload, "a");
    TEST_ASSERT_EQUAL(0, store.add(0, received(), paths::kDownload));

    files.readOnly = true;
    TEST_ASSERT_EQUAL(0, store.add(2, received(), paths::kDownload));
    TEST_ASSERT_TRUE(files.exists(paths::kDownload));  // left where it was
    TEST_ASSERT_EQUAL(0, store.messages(2).size());
}

void test_index_recovered_from_temp_file_after_power_cut() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile(paths::kDownload, "a");
    store.add(2, received(), paths::kDownload);

    // Power went after index.tmp was written but before it replaced index.txt
    // (on a FAT card, the old file is removed first).
    files.renameFile("/contacts/2/index.txt", "/contacts/2/index.tmp");
    TEST_ASSERT_EQUAL(1, store.messages(2).size());
}

void test_damaged_index_lines_are_skipped() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile("/contacts/2/index.txt",
                    "next\t3\n"
                    "msg\t1\tR\tU\t100\t4000\t0001.ogg\t0\t\n"
                    "rubbish line\n"
                    "msg\t2\tR\tU\t200\t40");  // cut off mid-write
    const auto list = store.messages(2);
    TEST_ASSERT_EQUAL(1, list.size());
    TEST_ASSERT_EQUAL(1, list[0].id);
}

void test_next_id_never_clashes_even_if_next_line_lost() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile("/contacts/2/index.txt", "msg\t5\tR\tU\t100\t4000\t0005.ogg\t0\t\n");
    files.writeFile("/contacts/2/0005.ogg", "old");
    files.writeFile(paths::kDownload, "new");
    TEST_ASSERT_EQUAL(6, store.add(2, received(), paths::kDownload));

    std::string old;
    files.readFile("/contacts/2/0005.ogg", old);
    TEST_ASSERT_EQUAL_STRING("old", old.c_str());
}

void test_add_does_not_overwrite_untracked_file() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile("/contacts/2/0001.ogg", "orphan");  // index lost entirely
    files.writeFile(paths::kDownload, "new");
    TEST_ASSERT_EQUAL(2, store.add(2, received(), paths::kDownload));
    std::string orphan;
    files.readFile("/contacts/2/0001.ogg", orphan);
    TEST_ASSERT_EQUAL_STRING("orphan", orphan.c_str());
}

void test_remove_all_clears_folder() {
    FakeFileStorage files;
    MessageStore store(files);
    files.writeFile(paths::kDownload, "a");
    store.add(2, received(), paths::kDownload);
    files.writeFile(paths::contactPhoto(2), "jpg");
    files.writeFile("/contacts/20/keep", "x");  // a folder whose name starts the same

    TEST_ASSERT_TRUE(store.removeAll(2));
    TEST_ASSERT_EQUAL(0, files.listDir("/contacts/2").size());
    TEST_ASSERT_TRUE(files.exists("/contacts/20/keep"));
}

// ---- Outbox ----

void test_outbox_keeps_order() {
    FakeFileStorage files;
    Outbox outbox(files);
    TEST_ASSERT_TRUE(outbox.empty());
    TEST_ASSERT_EQUAL(1, outbox.add(2, 10, kNow));
    TEST_ASSERT_EQUAL(2, outbox.add(1, 4, kNow + 1));
    TEST_ASSERT_EQUAL(3, outbox.add(2, 11, kNow + 2));

    const auto list = outbox.entries();
    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_EQUAL(10, list[0].messageId);
    TEST_ASSERT_EQUAL(1, list[1].contact);
    TEST_ASSERT_TRUE(list[2].queuedAt == kNow + 2);
    TEST_ASSERT_EQUAL(0, list[0].attempts);
}

void test_outbox_sorts_numerically_not_alphabetically() {
    FakeFileStorage files;
    Outbox outbox(files);
    for (int i = 0; i < 11; ++i) outbox.add(1, static_cast<uint32_t>(i + 1), kNow);
    const auto list = outbox.entries();
    TEST_ASSERT_EQUAL(11, list.size());
    TEST_ASSERT_EQUAL(9, list[8].sequence);
    TEST_ASSERT_EQUAL(10, list[9].sequence);
    TEST_ASSERT_EQUAL(11, list[10].sequence);
}

void test_outbox_new_entries_go_after_existing_ones() {
    FakeFileStorage files;
    Outbox outbox(files);
    outbox.add(1, 1, kNow);
    const uint32_t second = outbox.add(1, 2, kNow);
    outbox.remove(1);
    TEST_ASSERT_TRUE(outbox.add(1, 3, kNow) > second);
}

void test_outbox_records_failures() {
    FakeFileStorage files;
    Outbox outbox(files);
    const uint32_t seq = outbox.add(2, 10, kNow);
    TEST_ASSERT_TRUE(outbox.recordFailure(seq, 5000));
    TEST_ASSERT_TRUE(outbox.recordFailure(seq, 15000));
    const auto e = outbox.entries()[0];
    TEST_ASSERT_EQUAL(2, e.attempts);
    TEST_ASSERT_TRUE(e.nextAttemptAtMs == 15000);
    TEST_ASSERT_FALSE(outbox.recordFailure(99, 1));
}

void test_outbox_reset_retry_times_after_restart() {
    FakeFileStorage files;
    Outbox outbox(files);
    const uint32_t seq = outbox.add(2, 10, kNow);
    outbox.recordFailure(seq, 60000);
    outbox.resetRetryTimes();
    const auto e = outbox.entries()[0];
    TEST_ASSERT_TRUE(e.nextAttemptAtMs == 0);
    TEST_ASSERT_EQUAL(1, e.attempts);  // the count is kept
}

void test_outbox_remove_and_remove_for_contact() {
    FakeFileStorage files;
    Outbox outbox(files);
    outbox.add(2, 10, kNow);
    outbox.add(3, 1, kNow);
    outbox.add(2, 11, kNow);
    TEST_ASSERT_EQUAL(2, outbox.removeForContact(2));
    TEST_ASSERT_EQUAL(1, outbox.size());
    TEST_ASSERT_EQUAL(3, outbox.entries()[0].contact);
    TEST_ASSERT_TRUE(outbox.remove(2));
    TEST_ASSERT_FALSE(outbox.remove(2));
    TEST_ASSERT_TRUE(outbox.empty());
}

void test_outbox_ignores_half_written_and_stray_files() {
    FakeFileStorage files;
    Outbox outbox(files);
    outbox.add(2, 10, kNow);
    files.writeFile("/outbox/2.tmp", "2\t11\t");
    files.writeFile("/outbox/3.txt", "garbage");
    files.writeFile("/outbox/notes.txt", "hello");
    TEST_ASSERT_EQUAL(1, outbox.size());
}

void test_outbox_add_refuses_bad_entries() {
    FakeFileStorage files;
    Outbox outbox(files);
    TEST_ASSERT_EQUAL(0, outbox.add(0, 1, kNow));
    TEST_ASSERT_EQUAL(0, outbox.add(1, 0, kNow));
    files.readOnly = true;
    TEST_ASSERT_EQUAL(0, outbox.add(1, 1, kNow));
}

// ---- Bin ----

void test_bin_names_files_with_date_time_and_contact() {
    FakeFileStorage files;
    Bin bin(files);
    files.writeFile(paths::kRecording, "mp3");
    const std::string path = bin.add(paths::kRecording, 3, "Granny", kNow);
    TEST_ASSERT_EQUAL_STRING("/bin/20261101-143205-c3-Granny.mp3", path.c_str());
    TEST_ASSERT_FALSE(files.exists(paths::kRecording));
    TEST_ASSERT_TRUE(files.exists(path));
}

void test_bin_handles_awkward_names() {
    FakeFileStorage files;
    Bin bin(files);
    files.writeFile(paths::kRecording, "a");
    TEST_ASSERT_EQUAL_STRING("/bin/20261101-143205-c3-GrannyJo.mp3",
                             bin.add(paths::kRecording, 3, "Granny / Jo!", kNow).c_str());
    files.writeFile(paths::kRecording, "b");
    // A name with no Latin letters still works: the contact number is enough.
    TEST_ASSERT_EQUAL_STRING("/bin/20261101-143206-c4.mp3",
                             bin.add(paths::kRecording, 4, "\xD0\x91\xD0\xB0\xD0\xB1\xD1\x83\xD1\x88\xD0\xBA\xD0\xB0",
                                     kNow + 1)
                                 .c_str());
}

void test_bin_never_overwrites_in_the_same_second() {
    FakeFileStorage files;
    Bin bin(files);
    files.writeFile(paths::kRecording, "first");
    const std::string a = bin.add(paths::kRecording, 3, "Granny", kNow);
    files.writeFile(paths::kRecording, "second");
    const std::string b = bin.add(paths::kRecording, 3, "Granny", kNow);
    TEST_ASSERT_EQUAL_STRING("/bin/20261101-143205-c3-Granny-2.mp3", b.c_str());

    BinEntry newest;
    TEST_ASSERT_TRUE(bin.newest(newest));
    TEST_ASSERT_EQUAL_STRING(b.c_str(), newest.path().c_str());
    TEST_ASSERT_TRUE(a != b);
}

void test_bin_newest_and_entries() {
    FakeFileStorage files;
    Bin bin(files);
    BinEntry e;
    TEST_ASSERT_FALSE(bin.newest(e));

    files.writeFile(paths::kRecording, "a");
    bin.add(paths::kRecording, 3, "Granny", kNow + 100);
    files.writeFile(paths::kRecording, "b");
    bin.add(paths::kRecording, 12, "Grandad", kNow);
    files.writeFile("/bin/readme.txt", "not a recording");

    const auto list = bin.entries();
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL(12, list[0].contact);
    TEST_ASSERT_TRUE(bin.newest(e));
    TEST_ASSERT_EQUAL(3, e.contact);
    TEST_ASSERT_TRUE(e.time == kNow + 100);
}

void test_bin_removes_only_expired() {
    FakeFileStorage files;
    Bin bin(files);
    files.writeFile(paths::kRecording, "old");
    const std::string old = bin.add(paths::kRecording, 3, "Granny", kNow - 31 * kDay);
    files.writeFile(paths::kRecording, "recent");
    const std::string recent = bin.add(paths::kRecording, 3, "Granny", kNow - 29 * kDay);

    TEST_ASSERT_EQUAL(1, bin.removeExpired(kNow));
    TEST_ASSERT_FALSE(files.exists(old));
    TEST_ASSERT_TRUE(files.exists(recent));
}

void test_bin_keeps_recordings_made_before_clock_was_set() {
    FakeFileStorage files;
    Bin bin(files);
    files.writeFile(paths::kRecording, "a");
    const std::string unknown = bin.add(paths::kRecording, 3, "Granny", 120);  // clock not set: 1970
    TEST_ASSERT_EQUAL(0, bin.removeExpired(kNow));
    TEST_ASSERT_TRUE(files.exists(unknown));
}

void test_bin_add_missing_file_fails() {
    FakeFileStorage files;
    Bin bin(files);
    TEST_ASSERT_EQUAL_STRING("", bin.add("/work/nothing.mp3", 3, "Granny", kNow).c_str());
}

// ---- Storage: jobs that use several pieces ----

struct Box {
    FakeFileStorage files;
    FakeSettingsStore nvs;
    Storage storage{files, nvs};
    uint32_t granny = 0;

    Box() {
        Contact c;
        c.telegramId = 500;
        c.name = "Granny";
        granny = storage.people.addContact(c);
    }
};

void test_queue_recording_shows_in_history_and_outbox() {
    Box box;
    box.files.writeFile(paths::kRecording, "mp3");
    const uint32_t seq = box.storage.queueRecording(box.granny, paths::kRecording, 8000, kNow);
    TEST_ASSERT_NOT_EQUAL(0, seq);

    const auto history = box.storage.messages.messages(box.granny);
    TEST_ASSERT_EQUAL(1, history.size());
    TEST_ASSERT_TRUE(history[0].status == StoredMessage::Status::Waiting);
    TEST_ASSERT_EQUAL(8000, history[0].durationMs);

    const auto entries = box.storage.outbox.entries();
    TEST_ASSERT_EQUAL(1, entries.size());
    TEST_ASSERT_EQUAL(box.granny, entries[0].contact);
    TEST_ASSERT_EQUAL(history[0].id, entries[0].messageId);
}

void test_queue_recording_for_unknown_contact_fails() {
    Box box;
    box.files.writeFile(paths::kRecording, "mp3");
    TEST_ASSERT_EQUAL(0, box.storage.queueRecording(99, paths::kRecording, 8000, kNow));
    TEST_ASSERT_TRUE(box.files.exists(paths::kRecording));
}

void test_mark_sent_updates_message_and_clears_outbox() {
    Box box;
    box.files.writeFile(paths::kRecording, "mp3");
    box.storage.queueRecording(box.granny, paths::kRecording, 8000, kNow);
    const OutboxEntry entry = box.storage.outbox.entries()[0];

    TEST_ASSERT_TRUE(box.storage.markSent(entry, 321, "tg-file"));
    TEST_ASSERT_TRUE(box.storage.outbox.empty());
    StoredMessage m;
    box.storage.messages.find(box.granny, entry.messageId, m);
    TEST_ASSERT_TRUE(m.status == StoredMessage::Status::Delivered);
    TEST_ASSERT_TRUE(m.telegramMessageId == 321);
}

void test_bin_recording_uses_contact_name() {
    Box box;
    box.files.writeFile(paths::kRecording, "mp3");
    TEST_ASSERT_EQUAL_STRING("/bin/20261101-143205-c1-Granny.mp3",
                             box.storage.binRecording(box.granny, paths::kRecording, kNow).c_str());
}

void test_remove_contact_removes_everything() {
    Box box;
    Contact other;
    other.telegramId = 600;
    other.name = "Grandad";
    const uint32_t grandad = box.storage.people.addContact(other);

    box.files.writeFile(paths::kRecording, "a");
    box.storage.queueRecording(box.granny, paths::kRecording, 8000, kNow);
    box.files.writeFile(paths::kRecording, "b");
    box.storage.queueRecording(grandad, paths::kRecording, 8000, kNow);
    box.files.writeFile(paths::contactPhoto(box.granny), "jpg");

    TEST_ASSERT_TRUE(box.storage.removeContact(box.granny));
    TEST_ASSERT_FALSE(box.storage.people.isContact(500));
    TEST_ASSERT_EQUAL(0, box.files.listDir(paths::contactDir(box.granny)).size());
    TEST_ASSERT_EQUAL(1, box.storage.outbox.size());
    TEST_ASSERT_EQUAL(grandad, box.storage.outbox.entries()[0].contact);
    TEST_ASSERT_EQUAL(1, box.storage.messages.messages(grandad).size());
    TEST_ASSERT_FALSE(box.storage.removeContact(box.granny));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_add_moves_file_into_contact_folder);
    RUN_TEST(test_sent_messages_are_mp3);
    RUN_TEST(test_messages_come_back_oldest_first_with_unread_count);
    RUN_TEST(test_mark_heard);
    RUN_TEST(test_mark_delivered_only_for_sent);
    RUN_TEST(test_remove_deletes_audio_and_ids_are_not_reused);
    RUN_TEST(test_add_fails_cleanly);
    RUN_TEST(test_index_recovered_from_temp_file_after_power_cut);
    RUN_TEST(test_damaged_index_lines_are_skipped);
    RUN_TEST(test_next_id_never_clashes_even_if_next_line_lost);
    RUN_TEST(test_add_does_not_overwrite_untracked_file);
    RUN_TEST(test_remove_all_clears_folder);
    RUN_TEST(test_outbox_keeps_order);
    RUN_TEST(test_outbox_sorts_numerically_not_alphabetically);
    RUN_TEST(test_outbox_new_entries_go_after_existing_ones);
    RUN_TEST(test_outbox_records_failures);
    RUN_TEST(test_outbox_reset_retry_times_after_restart);
    RUN_TEST(test_outbox_remove_and_remove_for_contact);
    RUN_TEST(test_outbox_ignores_half_written_and_stray_files);
    RUN_TEST(test_outbox_add_refuses_bad_entries);
    RUN_TEST(test_bin_names_files_with_date_time_and_contact);
    RUN_TEST(test_bin_handles_awkward_names);
    RUN_TEST(test_bin_never_overwrites_in_the_same_second);
    RUN_TEST(test_bin_newest_and_entries);
    RUN_TEST(test_bin_removes_only_expired);
    RUN_TEST(test_bin_keeps_recordings_made_before_clock_was_set);
    RUN_TEST(test_bin_add_missing_file_fails);
    RUN_TEST(test_queue_recording_shows_in_history_and_outbox);
    RUN_TEST(test_queue_recording_for_unknown_contact_fails);
    RUN_TEST(test_mark_sent_updates_message_and_clears_outbox);
    RUN_TEST(test_bin_recording_uses_contact_name);
    RUN_TEST(test_remove_contact_removes_everything);
    return UNITY_END();
}

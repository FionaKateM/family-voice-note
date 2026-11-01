#include <unity.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "platform/desktop/DesktopFileStorage.h"

namespace fs = std::filesystem;
using fvb::DesktopFileStorage;

static fs::path tempRoot;

void setUp() {
    std::random_device rd;
    tempRoot = fs::temp_directory_path() / ("fvb-test-" + std::to_string(rd()));
    fs::remove_all(tempRoot);
}

void tearDown() { fs::remove_all(tempRoot); }

void test_creates_root_and_reports_mounted() {
    DesktopFileStorage card(tempRoot / "sdcard");
    TEST_ASSERT_TRUE(card.isMounted());
    TEST_ASSERT_TRUE(fs::is_directory(tempRoot / "sdcard"));
}

void test_write_then_read_creates_folders() {
    DesktopFileStorage card(tempRoot);
    TEST_ASSERT_TRUE(card.writeFile("/contacts/1/index.json", "{}"));
    TEST_ASSERT_TRUE(card.exists("/contacts/1/index.json"));

    std::string contents;
    TEST_ASSERT_TRUE(card.readFile("/contacts/1/index.json", contents));
    TEST_ASSERT_EQUAL_STRING("{}", contents.c_str());
    TEST_ASSERT_EQUAL(2, card.fileSize("/contacts/1/index.json"));
}

void test_binary_contents_survive() {
    DesktopFileStorage card(tempRoot);
    const std::string data("\x00\x01\xff\n\r", 5);
    TEST_ASSERT_TRUE(card.writeFile("/a.bin", data));
    std::string back;
    TEST_ASSERT_TRUE(card.readFile("/a.bin", back));
    TEST_ASSERT_TRUE(back == data);
}

void test_append_adds_to_end() {
    DesktopFileStorage card(tempRoot);
    card.writeFile("/log.txt", "one\n");
    card.appendFile("/log.txt", "two\n");
    std::string contents;
    card.readFile("/log.txt", contents);
    TEST_ASSERT_EQUAL_STRING("one\ntwo\n", contents.c_str());
}

void test_rename_and_remove() {
    DesktopFileStorage card(tempRoot);
    card.writeFile("/outbox/1.mp3", "audio");
    TEST_ASSERT_TRUE(card.renameFile("/outbox/1.mp3", "/bin/1.mp3"));
    TEST_ASSERT_FALSE(card.exists("/outbox/1.mp3"));
    TEST_ASSERT_TRUE(card.exists("/bin/1.mp3"));

    TEST_ASSERT_TRUE(card.removeFile("/bin/1.mp3"));
    TEST_ASSERT_FALSE(card.exists("/bin/1.mp3"));
    TEST_ASSERT_FALSE(card.removeFile("/bin/1.mp3"));
}

void test_missing_file_reads_fail() {
    DesktopFileStorage card(tempRoot);
    std::string contents;
    TEST_ASSERT_FALSE(card.readFile("/nope.txt", contents));
    TEST_ASSERT_EQUAL(-1, card.fileSize("/nope.txt"));
}

void test_list_and_remove_folder() {
    DesktopFileStorage card(tempRoot);
    card.writeFile("/contacts/1/a", "x");
    card.writeFile("/contacts/2/b", "y");
    auto names = card.listDir("/contacts");
    std::sort(names.begin(), names.end());
    TEST_ASSERT_EQUAL(2, names.size());
    TEST_ASSERT_EQUAL_STRING("1", names[0].c_str());
    TEST_ASSERT_EQUAL_STRING("2", names[1].c_str());

    TEST_ASSERT_TRUE(card.removeDir("/contacts/1"));
    TEST_ASSERT_FALSE(card.exists("/contacts/1/a"));
    TEST_ASSERT_TRUE(card.exists("/contacts/2/b"));
}

void test_refuses_to_remove_whole_card() {
    DesktopFileStorage card(tempRoot);
    card.writeFile("/keep.txt", "x");
    TEST_ASSERT_FALSE(card.removeDir("/"));
    TEST_ASSERT_TRUE(card.exists("/keep.txt"));
}

void test_refuses_paths_outside_card() {
    DesktopFileStorage card(tempRoot / "sdcard");
    TEST_ASSERT_FALSE(card.writeFile("/../escaped.txt", "x"));
    TEST_ASSERT_FALSE(card.writeFile("/a/../../escaped.txt", "x"));
    TEST_ASSERT_FALSE(card.writeFile("relative.txt", "x"));
    TEST_ASSERT_FALSE(fs::exists(tempRoot / "escaped.txt"));
}

void test_used_bytes_counts_files() {
    DesktopFileStorage card(tempRoot, 1000);
    card.writeFile("/a", "12345");
    card.writeFile("/b/c", "123");
    TEST_ASSERT_EQUAL(8, card.usedBytes());
    TEST_ASSERT_EQUAL(1000, card.totalBytes());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_creates_root_and_reports_mounted);
    RUN_TEST(test_write_then_read_creates_folders);
    RUN_TEST(test_binary_contents_survive);
    RUN_TEST(test_append_adds_to_end);
    RUN_TEST(test_rename_and_remove);
    RUN_TEST(test_missing_file_reads_fail);
    RUN_TEST(test_list_and_remove_folder);
    RUN_TEST(test_refuses_to_remove_whole_card);
    RUN_TEST(test_refuses_paths_outside_card);
    RUN_TEST(test_used_bytes_counts_files);
    return UNITY_END();
}

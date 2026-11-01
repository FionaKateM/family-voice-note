#include <unity.h>

#include <filesystem>
#include <random>
#include <string>

#include "platform/desktop/DesktopSettingsStore.h"

namespace fs = std::filesystem;
using fvb::DesktopSettingsStore;

static fs::path tempRoot;

void setUp() {
    std::random_device rd;
    tempRoot = fs::temp_directory_path() / ("fvb-test-" + std::to_string(rd()));
    fs::remove_all(tempRoot);
}

void tearDown() { fs::remove_all(tempRoot); }

void test_strings_round_trip_and_survive_restart() {
    {
        DesktopSettingsStore store(tempRoot);
        TEST_ASSERT_TRUE(store.setString("child_name", "Child"));
    }
    DesktopSettingsStore reopened(tempRoot);
    TEST_ASSERT_TRUE(reopened.has("child_name"));
    TEST_ASSERT_EQUAL_STRING("Child", reopened.getString("child_name").c_str());
}

void test_missing_key_gives_fallback() {
    DesktopSettingsStore store(tempRoot);
    TEST_ASSERT_FALSE(store.has("nothing"));
    TEST_ASSERT_EQUAL_STRING("x", store.getString("nothing", "x").c_str());
    TEST_ASSERT_EQUAL(42, store.getInt("nothing", 42));
}

void test_ints_round_trip() {
    DesktopSettingsStore store(tempRoot);
    TEST_ASSERT_TRUE(store.setInt("max_rec_s", 360));
    TEST_ASSERT_EQUAL(360, store.getInt("max_rec_s"));
    TEST_ASSERT_TRUE(store.setInt("neg", -5));
    TEST_ASSERT_EQUAL(-5, store.getInt("neg"));
}

void test_non_number_gives_int_fallback() {
    DesktopSettingsStore store(tempRoot);
    store.setString("word", "hello");
    TEST_ASSERT_EQUAL(7, store.getInt("word", 7));
    store.setString("half", "12abc");
    TEST_ASSERT_EQUAL(7, store.getInt("half", 7));
}

void test_remove() {
    DesktopSettingsStore store(tempRoot);
    store.setString("k", "v");
    TEST_ASSERT_TRUE(store.remove("k"));
    TEST_ASSERT_FALSE(store.has("k"));
    TEST_ASSERT_FALSE(store.remove("k"));
}

void test_rejects_keys_the_device_would_reject() {
    DesktopSettingsStore store(tempRoot);
    TEST_ASSERT_TRUE(store.setString("exactly15chars_", "ok"));
    TEST_ASSERT_FALSE(store.setString("sixteen_chars_xx", "too long"));
    TEST_ASSERT_FALSE(store.setString("", "empty"));
}

void test_rejects_keys_unsafe_as_file_names() {
    DesktopSettingsStore store(tempRoot);
    TEST_ASSERT_FALSE(store.setString("../evil", "x"));
    TEST_ASSERT_FALSE(store.setString("a/b", "x"));
    TEST_ASSERT_FALSE(store.setString("a.b", "x"));
}

void test_rejects_values_the_device_would_reject() {
    DesktopSettingsStore store(tempRoot);
    TEST_ASSERT_TRUE(store.setString("big", std::string(4000, 'x')));
    TEST_ASSERT_FALSE(store.setString("too_big", std::string(4001, 'x')));
    TEST_ASSERT_FALSE(store.has("too_big"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_strings_round_trip_and_survive_restart);
    RUN_TEST(test_missing_key_gives_fallback);
    RUN_TEST(test_ints_round_trip);
    RUN_TEST(test_non_number_gives_int_fallback);
    RUN_TEST(test_remove);
    RUN_TEST(test_rejects_keys_the_device_would_reject);
    RUN_TEST(test_rejects_keys_unsafe_as_file_names);
    RUN_TEST(test_rejects_values_the_device_would_reject);
    return UNITY_END();
}

// The shared text format and the date stamps used in bin file names.

#include <unity.h>

#include <string>
#include <vector>

#include "app/storage/Paths.h"
#include "app/storage/TextRecord.h"

using namespace fvb;

void setUp() {}
void tearDown() {}

void test_fields_round_trip_with_awkward_characters() {
    const std::vector<std::string> fields = {"plain", "tab\there", "new\nline", "back\\slash", "", "end\\"};
    const std::string line = text::joinFields(fields);
    TEST_ASSERT_EQUAL(std::string::npos, line.find('\n'));
    const auto back = text::splitFields(line);
    TEST_ASSERT_EQUAL(fields.size(), back.size());
    for (size_t i = 0; i < fields.size(); ++i) TEST_ASSERT_EQUAL_STRING(fields[i].c_str(), back[i].c_str());
}

void test_unicode_passes_through() {
    const std::string name = "Grand-m\xC3\xA8re \xF0\x9F\x91\xB5";  // accent and emoji
    const auto back = text::splitFields(text::joinFields({name}));
    TEST_ASSERT_EQUAL_STRING(name.c_str(), back[0].c_str());
}

void test_split_lines_handles_windows_endings() {
    const auto lines = text::splitLines("a\r\nb\n\nc");
    TEST_ASSERT_EQUAL(4, lines.size());
    TEST_ASSERT_EQUAL_STRING("a", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("b", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("", lines[2].c_str());
    TEST_ASSERT_EQUAL_STRING("c", lines[3].c_str());
    TEST_ASSERT_EQUAL(1, text::splitLines("only\n").size());
    TEST_ASSERT_EQUAL(0, text::splitLines("").size());
}

void test_number_parsing_is_strict() {
    int64_t i;
    uint64_t u;
    TEST_ASSERT_TRUE(text::parseInt("-42", i));
    TEST_ASSERT_EQUAL(-42, i);
    TEST_ASSERT_TRUE(text::parseInt("9000000000", i));
    TEST_ASSERT_TRUE(i == 9000000000LL);
    TEST_ASSERT_FALSE(text::parseInt("", i));
    TEST_ASSERT_FALSE(text::parseInt("12abc", i));
    TEST_ASSERT_FALSE(text::parseInt(" 12", i));
    TEST_ASSERT_FALSE(text::parseInt("99999999999999999999", i));
    TEST_ASSERT_TRUE(text::parseUint("7", u));
    TEST_ASSERT_FALSE(text::parseUint("-7", u));
}

void test_trim() {
    TEST_ASSERT_EQUAL_STRING("a b", text::trim("  a b \r\n").c_str());
    TEST_ASSERT_EQUAL_STRING("", text::trim(" \t ").c_str());
}

void test_compact_time_formats_and_parses() {
    TEST_ASSERT_EQUAL_STRING("19700101-000000", formatCompactTime(0).c_str());
    // 2026-11-01 14:32:05 UTC
    TEST_ASSERT_EQUAL_STRING("20261101-143205", formatCompactTime(1793543525).c_str());
    // A leap day
    TEST_ASSERT_EQUAL_STRING("20280229-235959", formatCompactTime(1835481599).c_str());

    int64_t back;
    TEST_ASSERT_TRUE(parseCompactTime("20261101-143205", back));
    TEST_ASSERT_TRUE(back == 1793543525);
    TEST_ASSERT_TRUE(parseCompactTime("20280229-235959", back));
    TEST_ASSERT_TRUE(back == 1835481599);
}

void test_compact_time_rejects_rubbish() {
    int64_t t;
    TEST_ASSERT_FALSE(parseCompactTime("20261101143205", t));
    TEST_ASSERT_FALSE(parseCompactTime("2026110a-143205", t));
    TEST_ASSERT_FALSE(parseCompactTime("20261301-143205", t));
    TEST_ASSERT_FALSE(parseCompactTime("20261101-246000", t));
}

void test_contact_paths() {
    TEST_ASSERT_EQUAL_STRING("/contacts/3", paths::contactDir(3).c_str());
    TEST_ASSERT_EQUAL_STRING("/contacts/3/index.txt", paths::contactIndex(3).c_str());
    TEST_ASSERT_EQUAL_STRING("/contacts/3/photo.jpg", paths::contactPhoto(3).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fields_round_trip_with_awkward_characters);
    RUN_TEST(test_unicode_passes_through);
    RUN_TEST(test_split_lines_handles_windows_endings);
    RUN_TEST(test_number_parsing_is_strict);
    RUN_TEST(test_trim);
    RUN_TEST(test_compact_time_formats_and_parses);
    RUN_TEST(test_compact_time_rejects_rubbish);
    RUN_TEST(test_contact_paths);
    return UNITY_END();
}

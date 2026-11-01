// The desktop build's drawing, running ffmpeg, and sound (ticket 1.11).
// The window itself isn't opened here; it needs a display.

#include <unity.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include "fakes/Fakes.h"
#include "platform/desktop/DesktopAudio.h"
#include "platform/desktop/PixelCanvas.h"
#include "platform/desktop/Subprocess.h"

using namespace fvb;
namespace fs = std::filesystem;

void setUp() {}
void tearDown() {}

namespace {

const Colour kRed{255, 0, 0};
const uint32_t kRedRgb = 0xff0000;

int countDrawn(const PixelCanvas& c, uint32_t rgb) {
    int n = 0;
    for (int y = 0; y < c.height(); ++y) {
        for (int x = 0; x < c.width(); ++x) n += c.drawnPixel(x, y) == rgb ? 1 : 0;
    }
    return n;
}

// The test tone from assets/. Tests usually run from the project folder, but
// look next to this file too in case they don't.
fs::path testTone() {
    const fs::path relative = fs::path("assets") / "test-audio" / "test-tone.mp3";
    const fs::path besideSource = fs::path(__FILE__).parent_path().parent_path().parent_path() / relative;
    return fs::exists(relative) ? fs::absolute(relative) : besideSource;
}

}  // namespace

// ---- PixelCanvas ----

void test_rect_fills_exactly_its_area() {
    PixelCanvas c;
    c.fillRect(10, 20, 5, 3, kRed);
    TEST_ASSERT_EQUAL(15, countDrawn(c, kRedRgb));
    TEST_ASSERT_EQUAL_HEX32(kRedRgb, c.drawnPixel(10, 20));
    TEST_ASSERT_EQUAL_HEX32(kRedRgb, c.drawnPixel(14, 22));
    TEST_ASSERT_EQUAL_HEX32(0, c.drawnPixel(15, 22));
}

void test_shapes_off_the_edge_are_clipped() {
    PixelCanvas c;
    c.fillRect(-10, -10, 20, 20, kRed);
    TEST_ASSERT_EQUAL(100, countDrawn(c, kRedRgb));
    c.clear();
    c.fillCircle(319, 239, 30, kRed);  // must not write outside the buffer
    TEST_ASSERT_TRUE(countDrawn(c, kRedRgb) > 0);
}

void test_circle_is_round() {
    PixelCanvas c;
    c.fillCircle(100, 100, 20, kRed);
    TEST_ASSERT_EQUAL_HEX32(kRedRgb, c.drawnPixel(100, 100));
    TEST_ASSERT_EQUAL_HEX32(kRedRgb, c.drawnPixel(120, 100));
    TEST_ASSERT_EQUAL_HEX32(kRedRgb, c.drawnPixel(100, 80));
    TEST_ASSERT_EQUAL_HEX32(0, c.drawnPixel(116, 116));  // outside, in the corner of its square
    const int area = countDrawn(c, kRedRgb);
    TEST_ASSERT_INT_WITHIN(60, 1257, area);  // pi x 20 x 20
}

void test_triangle_either_way_round() {
    PixelCanvas c;
    c.fillTriangle(0, 0, 20, 0, 0, 20, kRed);
    const int clockwise = countDrawn(c, kRedRgb);
    TEST_ASSERT_INT_WITHIN(25, 200, clockwise);
    c.clear();
    c.fillTriangle(0, 0, 0, 20, 20, 0, kRed);
    TEST_ASSERT_EQUAL(clockwise, countDrawn(c, kRedRgb));
}

void test_text_draws_and_measures() {
    PixelCanvas c;
    TEST_ASSERT_EQUAL(0, c.textWidth(""));
    TEST_ASSERT_EQUAL(14 * 3, c.textWidth("Abc", 2));
    TEST_ASSERT_EQUAL(24, c.textHeight(2));
    c.drawText(0, 0, "A", kRed, 1);
    TEST_ASSERT_TRUE(countDrawn(c, kRedRgb) > 10);
    c.clear();
    c.drawText(0, 0, " ", kRed, 3);
    TEST_ASSERT_EQUAL(0, countDrawn(c, kRedRgb));
}

void test_letters_outside_ascii_count_as_one_each() {
    PixelCanvas c;
    // "Zoë" and an emoji: three letters then one.
    TEST_ASSERT_EQUAL(c.textWidth("abc", 1), c.textWidth("Zo\xc3\xab", 1));
    TEST_ASSERT_EQUAL(c.textWidth("a", 1), c.textWidth("\xf0\x9f\x91\xb5", 1));
    c.drawText(0, 0, "\xc3\xab", kRed, 1);  // drawn as a box
    TEST_ASSERT_TRUE(countDrawn(c, kRedRgb) > 0);
}

void test_present_shows_what_was_drawn() {
    PixelCanvas c;
    const uint64_t before = c.frameNumber();
    c.fillRect(0, 0, 1, 1, kRed);
    TEST_ASSERT_EQUAL_HEX32(0, c.shownPixel(0, 0));
    c.present();
    TEST_ASSERT_EQUAL_HEX32(kRedRgb, c.shownPixel(0, 0));
    TEST_ASSERT_TRUE(c.frameNumber() > before);
}

// ---- Running other programs ----

void test_subprocess_reads_output() {
    std::string out;
    const int code = subprocess::runReadingOutput({"echo", "hello there"}, [&](const char* d, size_t n) {
        out.append(d, n);
        return true;
    });
    TEST_ASSERT_EQUAL(0, code);
    TEST_ASSERT_EQUAL_STRING("hello there\n", out.c_str());
}

void test_subprocess_sends_input() {
    const fs::path file = fs::temp_directory_path() / "fvb-subprocess-test.txt";
    fs::remove(file);
    const std::string text = "it's got 'quotes' and spaces";
    const int code = subprocess::runWithInput({"sh", "-c", "cat > \"$0\"", file.string()}, text.data(), text.size());
    TEST_ASSERT_EQUAL(0, code);
    TEST_ASSERT_EQUAL(text.size(), fs::file_size(file));
    fs::remove(file);
}

void test_subprocess_reports_a_missing_program() {
    TEST_ASSERT_FALSE(subprocess::isAvailable("no-such-program-fvb"));
    TEST_ASSERT_EQUAL(-1, subprocess::runReadingOutput({"no-such-program-fvb"}, [](const char*, size_t) { return true; }));
}

// ---- Sound ----

void test_level_from_samples() {
    const int16_t quiet[] = {0, 10, -10};
    const int16_t loud[] = {0, 20000, -32768};
    TEST_ASSERT_EQUAL(0, DesktopAudio::levelOf(quiet, 3));
    TEST_ASSERT_EQUAL(100, DesktopAudio::levelOf(loud, 3));
    TEST_ASSERT_EQUAL(0, DesktopAudio::levelOf(loud, 0));
}

void test_plays_a_real_file_to_the_end() {
    if (!subprocess::isAvailable("ffmpeg")) TEST_IGNORE_MESSAGE("ffmpeg isn't installed");
    setenv("SDL_AUDIODRIVER", "dummy", 1);  // no real speakers needed

    const fs::path card = fs::temp_directory_path() / "fvb-audio-test";
    fs::remove_all(card);
    fs::create_directories(card / "contacts" / "1");
    TEST_ASSERT_TRUE_MESSAGE(fs::exists(testTone()), "can't find assets/test-audio/test-tone.mp3");
    fs::copy_file(testTone(), card / "contacts" / "1" / "0001.mp3");
    fs::path junk = card / "contacts" / "1" / "0002.ogg";
    {
        FILE* f = std::fopen(junk.string().c_str(), "w");
        std::fputs("not audio", f);
        std::fclose(f);
    }

    fakes::FakeLogger log;
    DesktopAudio audio(card, log);
    TEST_ASSERT_TRUE(audio.begin());

    TEST_ASSERT_FALSE(audio.play("/contacts/1/missing.ogg"));
    TEST_ASSERT_FALSE(audio.play("/contacts/1/0002.ogg"));
    TEST_ASSERT_FALSE(audio.play("/../outside.mp3"));
    TEST_ASSERT_FALSE(audio.isPlaying());

    TEST_ASSERT_TRUE(audio.play("/contacts/1/0001.mp3"));
    TEST_ASSERT_TRUE(audio.isPlaying());
    audio.stopPlayback();
    TEST_ASSERT_FALSE(audio.isPlaying());

    // The tone is about 3 seconds long.
    TEST_ASSERT_TRUE(audio.play("/contacts/1/0001.mp3"));
    const auto start = std::chrono::steady_clock::now();
    while (audio.isPlaying() && std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    TEST_ASSERT_FALSE(audio.isPlaying());
    TEST_ASSERT_INT_WITHIN(1000, 3100, static_cast<int>(took.count()));

    fs::remove_all(card);
}

void test_records_an_mp3_with_its_length() {
    if (!subprocess::isAvailable("ffmpeg")) TEST_IGNORE_MESSAGE("ffmpeg isn't installed");
    setenv("SDL_AUDIODRIVER", "dummy", 1);  // records silence, no real mic needed

    const fs::path card = fs::temp_directory_path() / "fvb-record-test";
    fs::remove_all(card);
    fakes::FakeLogger log;
    DesktopAudio audio(card, log);
    TEST_ASSERT_TRUE(audio.begin());
    TEST_ASSERT_EQUAL(0, audio.stopRecording());  // nothing to stop

    if (!audio.startRecording("/work/recording.mp3")) TEST_IGNORE_MESSAGE("no microphone, even a pretend one");
    TEST_ASSERT_TRUE(audio.isRecording());
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const uint32_t ms = audio.stopRecording();
    TEST_ASSERT_FALSE(audio.isRecording());
    TEST_ASSERT_INT_WITHIN(400, 1500, static_cast<int>(ms));
    const fs::path file = card / "work" / "recording.mp3";
    TEST_ASSERT_TRUE(fs::exists(file));
    // 32 kbit/s is 4 KB a second.
    TEST_ASSERT_INT_WITHIN(2500, 6000, static_cast<int>(fs::file_size(file)));

    fs::remove_all(card);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rect_fills_exactly_its_area);
    RUN_TEST(test_shapes_off_the_edge_are_clipped);
    RUN_TEST(test_circle_is_round);
    RUN_TEST(test_triangle_either_way_round);
    RUN_TEST(test_text_draws_and_measures);
    RUN_TEST(test_letters_outside_ascii_count_as_one_each);
    RUN_TEST(test_present_shows_what_was_drawn);
    RUN_TEST(test_subprocess_reads_output);
    RUN_TEST(test_subprocess_sends_input);
    RUN_TEST(test_subprocess_reports_a_missing_program);
    RUN_TEST(test_level_from_samples);
    RUN_TEST(test_plays_a_real_file_to_the_end);
    RUN_TEST(test_records_an_mp3_with_its_length);
    return UNITY_END();
}

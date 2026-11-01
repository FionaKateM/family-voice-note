#include <unity.h>

#include "app/App.h"
#include "app/Version.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;

void setUp() {}
void tearDown() {}

void test_begin_shows_name_and_version() {
    FakeHardware hw;
    App app(hw.hardware);
    app.begin();

    TEST_ASSERT_TRUE(hw.display.on);
    TEST_ASSERT_TRUE(hw.display.shows("Family Voice Box"));
    TEST_ASSERT_TRUE(hw.display.shows(kFirmwareVersion));
}

void test_begin_turns_leds_off() {
    FakeHardware hw;
    App app(hw.hardware);
    app.begin();

    TEST_ASSERT_EQUAL(1, hw.leds.showCount);
    TEST_ASSERT_TRUE(hw.leds.allShown(Colour{0, 0, 0}));
}

void test_begin_warns_when_sd_card_missing() {
    FakeHardware hw;
    hw.files.mounted = false;
    App app(hw.hardware);
    app.begin();

    TEST_ASSERT_TRUE(hw.log.contains("SD card not available"));
}

void test_tick_reads_input_and_drains_events() {
    FakeHardware hw;
    App app(hw.hardware);
    app.begin();

    hw.input.press(Button::Left);
    hw.input.press(Button::Right);
    app.tick();

    TEST_ASSERT_EQUAL(1, hw.input.updateCount);
    TEST_ASSERT_EQUAL(0, hw.input.queue.size());
    TEST_ASSERT_EQUAL(1, app.tickCount());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_shows_name_and_version);
    RUN_TEST(test_begin_turns_leds_off);
    RUN_TEST(test_begin_warns_when_sd_card_missing);
    RUN_TEST(test_tick_reads_input_and_drains_events);
    return UNITY_END();
}

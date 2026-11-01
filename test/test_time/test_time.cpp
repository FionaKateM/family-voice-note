// Time zones, daylight saving, quiet hours and the asleep check, using fake times.

#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "app/storage/DeviceSettings.h"
#include "app/storage/People.h"
#include "app/time/PosixTimeZone.h"
#include "app/time/TimeChecks.h"
#include "app/time/TimeZones.h"
#include "fakes/Fakes.h"

using namespace fvb;
using fvb::fakes::FakeClock;
using fvb::fakes::FakeSettingsStore;

void setUp() {}
void tearDown() {}

// Moments in UTC, seconds since 1970.
static const int64_t kNewYear2026 = 1767225600;      // 2026-01-01 00:00
static const int64_t kUkClocksForward = 1774746000;  // 2026-03-29 01:00
static const int64_t kUkClocksBack = 1792890000;     // 2026-10-25 01:00
static const int64_t kSydneyClocksBack = 1775318400; // 2026-04-04 16:00 (05 April 03:00 local)
static const int64_t kSydneyForward = 1791043200;    // 2026-10-03 16:00 (04 October 02:00 local)
static const int64_t kUsForward = 1772953200;        // 2026-03-08 07:00 (02:00 in New York)
static const int64_t kUsBack = 1793512800;           // 2026-11-01 06:00 (02:00 daylight time)
static const int64_t kMidJuly = 1784116800;          // 2026-07-15 12:00
static const int64_t kLeapDay = 1835438400;          // 2028-02-29 12:00

static int64_t at(int year, int month, int day, int hour, int minute) {
    return civil::daysFromDate(year, month, day) * 86400 + hour * 3600 + minute * 60;
}

static PosixTimeZone rule(const char* text) {
    PosixTimeZone z;
    TEST_ASSERT_TRUE_MESSAGE(PosixTimeZone::parse(text, z), text);
    return z;
}

// ---- Dates ----

void test_dates_round_trip() {
    TEST_ASSERT_EQUAL_INT64(0, civil::daysFromDate(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(kNewYear2026 / 86400, civil::daysFromDate(2026, 1, 1));
    TEST_ASSERT_EQUAL_INT64(kLeapDay, at(2028, 2, 29, 12, 0));
    TEST_ASSERT_EQUAL(4, civil::weekdayFromDays(0));                              // Thursday
    TEST_ASSERT_EQUAL(0, civil::weekdayFromDays(civil::daysFromDate(2026, 3, 29)));  // Sunday
    for (int64_t d = -1000; d < 100000; d += 37) {
        int y, m, day;
        civil::dateFromDays(d, y, m, day);
        TEST_ASSERT_EQUAL_INT64(d, civil::daysFromDate(y, m, day));
    }
    const LocalTime t = civil::split(kLeapDay + 3 * 3600 + 25 * 60 + 9);
    TEST_ASSERT_EQUAL(2028, t.year);
    TEST_ASSERT_EQUAL(2, t.month);
    TEST_ASSERT_EQUAL(29, t.day);
    TEST_ASSERT_EQUAL(15, t.hour);
    TEST_ASSERT_EQUAL(25, t.minute);
    TEST_ASSERT_EQUAL(9, t.second);
    TEST_ASSERT_EQUAL(2, t.weekday);  // Tuesday
}

// ---- POSIX rules ----

void test_uk_daylight_saving() {
    const PosixTimeZone uk = rule("GMT0BST,M3.5.0/1,M10.5.0");
    TEST_ASSERT_TRUE(uk.hasDaylightSaving());
    TEST_ASSERT_EQUAL(0, uk.utcOffsetAt(kNewYear2026));
    TEST_ASSERT_EQUAL(0, uk.utcOffsetAt(kUkClocksForward - 1));
    TEST_ASSERT_EQUAL(3600, uk.utcOffsetAt(kUkClocksForward));
    TEST_ASSERT_EQUAL(3600, uk.utcOffsetAt(kMidJuly));
    TEST_ASSERT_EQUAL(3600, uk.utcOffsetAt(kUkClocksBack - 1));
    TEST_ASSERT_EQUAL(0, uk.utcOffsetAt(kUkClocksBack));

    // 00:59:59 GMT, then the clocks jump to 02:00 BST.
    LocalTime before = uk.toLocal(kUkClocksForward - 1);
    LocalTime after = uk.toLocal(kUkClocksForward);
    TEST_ASSERT_EQUAL(0, before.hour);
    TEST_ASSERT_FALSE(before.daylightSaving);
    TEST_ASSERT_EQUAL(2, after.hour);
    TEST_ASSERT_EQUAL(0, after.minute);
    TEST_ASSERT_TRUE(after.daylightSaving);
}

void test_us_daylight_saving() {
    const PosixTimeZone ny = rule("EST5EDT,M3.2.0,M11.1.0");
    TEST_ASSERT_EQUAL(-5 * 3600, ny.utcOffsetAt(kUsForward - 1));
    TEST_ASSERT_EQUAL(-4 * 3600, ny.utcOffsetAt(kUsForward));
    TEST_ASSERT_EQUAL(-4 * 3600, ny.utcOffsetAt(kUsBack - 1));
    TEST_ASSERT_EQUAL(-5 * 3600, ny.utcOffsetAt(kUsBack));
    const LocalTime t = ny.toLocal(kMidJuly);
    TEST_ASSERT_EQUAL(8, t.hour);
    TEST_ASSERT_EQUAL(15, t.day);
}

void test_southern_hemisphere_daylight_saving_crosses_new_year() {
    const PosixTimeZone sydney = rule("AEST-10AEDT,M10.1.0,M4.1.0/3");
    TEST_ASSERT_EQUAL(11 * 3600, sydney.utcOffsetAt(kNewYear2026));
    TEST_ASSERT_EQUAL(11 * 3600, sydney.utcOffsetAt(kSydneyClocksBack - 1));
    TEST_ASSERT_EQUAL(10 * 3600, sydney.utcOffsetAt(kSydneyClocksBack));
    TEST_ASSERT_EQUAL(10 * 3600, sydney.utcOffsetAt(kMidJuly));
    TEST_ASSERT_EQUAL(10 * 3600, sydney.utcOffsetAt(kSydneyForward - 1));
    TEST_ASSERT_EQUAL(11 * 3600, sydney.utcOffsetAt(kSydneyForward));
    TEST_ASSERT_EQUAL(11 * 3600, sydney.utcOffsetAt(at(2026, 12, 31, 23, 0)));
}

void test_half_hour_and_odd_offsets() {
    const LocalTime india = rule("IST-5:30").toLocal(kMidJuly);
    TEST_ASSERT_EQUAL(17, india.hour);
    TEST_ASSERT_EQUAL(30, india.minute);
    TEST_ASSERT_FALSE(india.daylightSaving);

    TEST_ASSERT_EQUAL(-(3 * 3600 + 30 * 60), rule("NST3:30NDT,M3.2.0,M11.1.0").utcOffsetAt(kNewYear2026));
    TEST_ASSERT_EQUAL(8 * 3600 + 45 * 60, rule("<+0845>-8:45").utcOffsetAt(kMidJuly));

    // Lord Howe Island only moves its clocks by half an hour.
    const PosixTimeZone lordHowe = rule("<+1030>-10:30<+11>-11,M10.1.0,M4.1.0");
    TEST_ASSERT_EQUAL(11 * 3600, lordHowe.utcOffsetAt(kNewYear2026));
    TEST_ASSERT_EQUAL(10 * 3600 + 1800, lordHowe.utcOffsetAt(kMidJuly));
}

void test_rules_with_unusual_change_times() {
    // Greenland style: change at -1:00 local, i.e. 23:00 the day before.
    const PosixTimeZone nuuk = rule("<-02>2<-01>,M3.5.0/-1,M10.5.0/0");
    TEST_ASSERT_EQUAL(-2 * 3600, nuuk.utcOffsetAt(at(2026, 3, 29, 0, 59)));
    TEST_ASSERT_EQUAL(-1 * 3600, nuuk.utcOffsetAt(at(2026, 3, 29, 1, 0)));
    // Israel style: change at 26:00 on a Thursday, which is 02:00 on the Friday.
    const PosixTimeZone israel = rule("IST-2IDT,M3.4.4/26,M10.5.0");
    TEST_ASSERT_EQUAL(2 * 3600, israel.utcOffsetAt(at(2026, 3, 26, 23, 59)));
    TEST_ASSERT_EQUAL(3 * 3600, israel.utcOffsetAt(at(2026, 3, 27, 0, 0)));
    // Day numbers: J60 is always 1 March; 59 is 29 February in a leap year.
    const PosixTimeZone julian = rule("AAA0BBB,J60/0,J300/0");
    TEST_ASSERT_EQUAL(0, julian.utcOffsetAt(at(2028, 2, 29, 23, 59)));
    TEST_ASSERT_EQUAL(3600, julian.utcOffsetAt(at(2028, 3, 1, 0, 0)));
    const PosixTimeZone zeroBased = rule("AAA0BBB,59/0,299/0");
    TEST_ASSERT_EQUAL(3600, zeroBased.utcOffsetAt(at(2028, 2, 29, 0, 0)));
    TEST_ASSERT_EQUAL(0, zeroBased.utcOffsetAt(at(2027, 2, 28, 23, 59)));
    TEST_ASSERT_EQUAL(3600, zeroBased.utcOffsetAt(at(2027, 3, 1, 0, 0)));
}

void test_bad_rules_are_refused() {
    PosixTimeZone z;
    const char* bad[] = {"", "GM0", "GMT", "GMT+", "<+05", "GMT0BST", "GMT0BST,M3.5.0", "GMT0BST,M13.5.0,M10.5.0",
                         "GMT0BST,M3.6.0,M10.5.0", "GMT0BST,M3.5.7,M10.5.0", "GMT0BST,J0,J100", "GMT0 junk",
                         "GMT200"};
    for (const char* text : bad) TEST_ASSERT_FALSE_MESSAGE(PosixTimeZone::parse(text, z), text);
}

// Every rule in the built-in table is compared with the laptop's C library, which
// reads the same POSIX strings, every 3 hours for 4 years and either side of
// every change.
void test_every_built_in_rule_matches_the_c_library() {
    const char* savedTz = std::getenv("TZ");
    const std::string saved = savedTz ? savedTz : "";
    const int64_t from = at(2027, 1, 1, 0, 0);
    const int64_t to = at(2031, 1, 1, 0, 0);
    for (size_t i = 0; i < tzdata::kZoneCount; ++i) {
        const TimeZoneInfo& zone = tzdata::kZones[i];
        PosixTimeZone ours;
        TEST_ASSERT_TRUE_MESSAGE(PosixTimeZone::parse(zone.rule, ours), zone.name);
        // A leading ':' would mean a file name; without one glibc reads the rule itself.
        setenv("TZ", zone.rule, 1);
        tzset();
        int32_t previous = ours.utcOffsetAt(from);
        for (int64_t t = from; t < to; t += 3 * 3600) {
            const time_t tt = static_cast<time_t>(t);
            struct tm theirs;
            localtime_r(&tt, &theirs);
            if (ours.utcOffsetAt(t) != theirs.tm_gmtoff) {
                char msg[160];
                std::snprintf(msg, sizeof(msg), "%s at %lld: ours %d, C library %ld", zone.name,
                              static_cast<long long>(t), ours.utcOffsetAt(t), static_cast<long>(theirs.tm_gmtoff));
                TEST_FAIL_MESSAGE(msg);
            }
            const int32_t now = ours.utcOffsetAt(t);
            if (now != previous) {
                // Find the exact second of the change and check both sides of it.
                int64_t lo = t - 3 * 3600, hi = t;
                while (hi - lo > 1) {
                    const int64_t mid = (lo + hi) / 2;
                    (ours.utcOffsetAt(mid) == previous ? lo : hi) = mid;
                }
                for (int64_t probe : {lo, hi}) {
                    const time_t tt = static_cast<time_t>(probe);
                    struct tm theirs;
                    localtime_r(&tt, &theirs);
                    TEST_ASSERT_EQUAL_MESSAGE(theirs.tm_gmtoff, ours.utcOffsetAt(probe), zone.name);
                }
                previous = now;
            }
        }
    }
    if (savedTz) {
        setenv("TZ", saved.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

// ---- The built-in table ----

void test_table_lookups() {
    const TimeZoneInfo* london = timezones::find("Europe/London");
    TEST_ASSERT_NOT_NULL(london);
    TEST_ASSERT_EQUAL_STRING("London", london->city);
    TEST_ASSERT_EQUAL_STRING("GB", london->country);
    TEST_ASSERT_EQUAL_STRING("United Kingdom", timezones::countryOf(*london)->name);

    // Old and alternative names lead to the entry that behaves the same.
    TEST_ASSERT_EQUAL_STRING("Europe/Kyiv", timezones::find("Europe/Kiev")->name);
    TEST_ASSERT_EQUAL_STRING("America/New_York", timezones::find("America/Detroit")->name);
    TEST_ASSERT_EQUAL_STRING("America/New_York", timezones::find("US/Eastern")->name);
    TEST_ASSERT_EQUAL_STRING("Asia/Kolkata", timezones::find("Asia/Calcutta")->name);
    TEST_ASSERT_EQUAL_STRING("Australia/Sydney", timezones::find("Australia/Melbourne")->name);
    TEST_ASSERT_NULL(timezones::find(""));
    TEST_ASSERT_NULL(timezones::find("Mars/Olympus_Mons"));
    TEST_ASSERT_NULL(timezones::find("europe/london"));

    TEST_ASSERT_EQUAL_STRING("Spain", timezones::findCountry("es")->name);
    TEST_ASSERT_NULL(timezones::findCountry("ZZ"));
    TEST_ASSERT_NULL(timezones::findCountry("GBR"));
}

void test_countries_and_cities_for_the_picker() {
    const auto all = timezones::countries();
    TEST_ASSERT_TRUE(all.size() > 240);
    for (size_t i = 1; i < all.size(); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(std::strcmp(all[i - 1]->name, all[i]->name) < 0, all[i]->name);
    }
    // Every country has at least one zone, and every zone points back at its country.
    for (const CountryInfo* c : all) {
        const auto zones = timezones::zonesIn(*c);
        TEST_ASSERT_TRUE_MESSAGE(!zones.empty(), c->name);
        for (const TimeZoneInfo* z : zones) TEST_ASSERT_EQUAL_STRING(c->code, z->country);
    }
    // Most countries need no city question.
    const TimeZoneInfo* france = timezones::onlyZoneIn(*timezones::findCountry("FR"));
    TEST_ASSERT_NOT_NULL(france);
    TEST_ASSERT_EQUAL_STRING("Europe/Paris", france->name);

    // Big countries list one city per set of clock rules, not every IANA zone.
    const CountryInfo* us = timezones::findCountry("US");
    TEST_ASSERT_NULL(timezones::onlyZoneIn(*us));
    const auto usZones = timezones::zonesIn(*us);
    TEST_ASSERT_TRUE(usZones.size() >= 6 && usZones.size() <= 10);
    bool hasChicago = false;
    for (const TimeZoneInfo* z : usZones) hasChicago = hasChicago || std::string(z->city) == "Chicago";
    TEST_ASSERT_TRUE(hasChicago);
    // Well-known cities are used for the name.
    bool hasSydney = false;
    for (const TimeZoneInfo* z : timezones::zonesIn(*timezones::findCountry("AU"))) {
        hasSydney = hasSydney || std::string(z->city) == "Sydney";
    }
    TEST_ASSERT_TRUE(hasSydney);
}

void test_aliases_are_sorted_and_point_at_real_zones() {
    for (size_t i = 0; i < tzdata::kAliasCount; ++i) {
        TEST_ASSERT_TRUE(tzdata::kAliases[i].zone < tzdata::kZoneCount);
        if (i > 0) TEST_ASSERT_TRUE(std::strcmp(tzdata::kAliases[i - 1].name, tzdata::kAliases[i].name) < 0);
        TEST_ASSERT_EQUAL_PTR(&tzdata::kZones[tzdata::kAliases[i].zone], timezones::find(tzdata::kAliases[i].name));
    }
    TEST_ASSERT_TRUE(std::strlen(timezones::dataVersion()) >= 5);
}

void test_local_time_by_zone_name() {
    LocalTime t;
    TEST_ASSERT_TRUE(timezones::toLocal("Europe/London", kMidJuly, t));
    TEST_ASSERT_EQUAL(13, t.hour);
    TEST_ASSERT_TRUE(t.daylightSaving);
    TEST_ASSERT_TRUE(timezones::toLocal("Pacific/Auckland", kMidJuly, t));
    TEST_ASSERT_EQUAL(16, t.day);
    TEST_ASSERT_EQUAL(0, t.hour);
    t.hour = 99;
    TEST_ASSERT_FALSE(timezones::toLocal("Nowhere/Special", kMidJuly, t));
    TEST_ASSERT_EQUAL(99, t.hour);
}

void test_device_timezone_setting_uses_the_table() {
    FakeSettingsStore store;
    DeviceSettings s(store);
    TEST_ASSERT_FALSE(s.setTimezone("Not/AZone"));
    TEST_ASSERT_FALSE(s.setTimezone(""));
    TEST_ASSERT_EQUAL_STRING("", s.timezone().c_str());
    TEST_ASSERT_TRUE(s.setTimezone("Europe/Kiev"));
    TEST_ASSERT_EQUAL_STRING("Europe/Kyiv", s.timezone().c_str());
}

// ---- Daily windows ----

void test_daily_hours_within_one_day() {
    TEST_ASSERT_FALSE(isWithinDailyHours(9 * 60, 17 * 60, 9 * 60 - 1));
    TEST_ASSERT_TRUE(isWithinDailyHours(9 * 60, 17 * 60, 9 * 60));
    TEST_ASSERT_TRUE(isWithinDailyHours(9 * 60, 17 * 60, 17 * 60 - 1));
    TEST_ASSERT_FALSE(isWithinDailyHours(9 * 60, 17 * 60, 17 * 60));
}

void test_daily_hours_crossing_midnight() {
    const uint16_t start = 19 * 60, end = 7 * 60;
    TEST_ASSERT_FALSE(isWithinDailyHours(start, end, 18 * 60 + 59));
    TEST_ASSERT_TRUE(isWithinDailyHours(start, end, 19 * 60));
    TEST_ASSERT_TRUE(isWithinDailyHours(start, end, 23 * 60 + 59));
    TEST_ASSERT_TRUE(isWithinDailyHours(start, end, 0));
    TEST_ASSERT_TRUE(isWithinDailyHours(start, end, 6 * 60 + 59));
    TEST_ASSERT_FALSE(isWithinDailyHours(start, end, 7 * 60));
    TEST_ASSERT_FALSE(isWithinDailyHours(start, end, 12 * 60));
}

void test_daily_hours_edge_cases() {
    // Same start and end means no window at all.
    for (uint16_t m = 0; m < 24 * 60; m += 30) TEST_ASSERT_FALSE(isWithinDailyHours(8 * 60, 8 * 60, m));
    // Starting at midnight.
    TEST_ASSERT_TRUE(isWithinDailyHours(0, 6 * 60, 0));
    TEST_ASSERT_FALSE(isWithinDailyHours(0, 6 * 60, 23 * 60 + 59));
    // Nonsense values are never "within".
    TEST_ASSERT_FALSE(isWithinDailyHours(24 * 60, 6 * 60, 0));
    TEST_ASSERT_FALSE(isWithinDailyHours(22 * 60, 6 * 60, 24 * 60));
}

// ---- Quiet hours ----

void test_quiet_hours_use_the_box_timezone() {
    FakeSettingsStore store;
    DeviceSettings settings(store);
    FakeClock clock;
    clock.timeSet = true;
    TEST_ASSERT_TRUE(settings.setTimezone("Europe/London"));  // default quiet hours 19:00 to 07:00

    clock.unixSeconds = at(2026, 7, 15, 17, 59);  // 18:59 BST
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));
    clock.unixSeconds = at(2026, 7, 15, 18, 0);  // 19:00 BST
    TEST_ASSERT_TRUE(isQuietHours(settings, clock));
    clock.unixSeconds = at(2026, 7, 16, 5, 59);  // 06:59 BST next day
    TEST_ASSERT_TRUE(isQuietHours(settings, clock));
    clock.unixSeconds = at(2026, 7, 16, 6, 0);  // 07:00 BST
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));

    // In winter the same UTC time is an hour earlier on the wall clock.
    clock.unixSeconds = at(2026, 12, 15, 18, 30);  // 18:30 GMT
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));
    clock.unixSeconds = at(2026, 12, 15, 19, 0);
    TEST_ASSERT_TRUE(isQuietHours(settings, clock));

    // Somewhere else entirely.
    TEST_ASSERT_TRUE(settings.setTimezone("Asia/Tokyo"));
    TEST_ASSERT_TRUE(settings.setQuietHours(20 * 60, 6 * 60 + 30));
    clock.unixSeconds = at(2026, 7, 15, 11, 0);  // 20:00 in Tokyo
    TEST_ASSERT_TRUE(isQuietHours(settings, clock));
    clock.unixSeconds = at(2026, 7, 15, 21, 30);  // 06:30 in Tokyo
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));

    LocalTime local;
    TEST_ASSERT_TRUE(deviceLocalTime(settings, clock, local));
    TEST_ASSERT_EQUAL(6, local.hour);
    TEST_ASSERT_EQUAL(16, local.day);
}

void test_quiet_hours_across_the_clocks_changing() {
    FakeSettingsStore store;
    DeviceSettings settings(store);
    FakeClock clock;
    clock.timeSet = true;
    TEST_ASSERT_TRUE(settings.setTimezone("Europe/London"));
    TEST_ASSERT_TRUE(settings.setQuietHours(19 * 60, 7 * 60));
    // The night the clocks go forward: 06:30 GMT would be 07:30 BST, so it's over.
    clock.unixSeconds = at(2026, 3, 29, 5, 59);  // 06:59 BST
    TEST_ASSERT_TRUE(isQuietHours(settings, clock));
    clock.unixSeconds = at(2026, 3, 29, 6, 0);  // 07:00 BST
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));
    // The night they go back.
    clock.unixSeconds = at(2026, 10, 25, 6, 59);  // 06:59 GMT
    TEST_ASSERT_TRUE(isQuietHours(settings, clock));
    clock.unixSeconds = at(2026, 10, 25, 7, 0);
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));
}

void test_not_quiet_when_time_or_timezone_unknown() {
    FakeSettingsStore store;
    DeviceSettings settings(store);
    FakeClock clock;
    clock.unixSeconds = at(2026, 7, 15, 23, 0);
    clock.timeSet = true;
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));  // no timezone yet
    TEST_ASSERT_TRUE(settings.setTimezone("Europe/London"));
    TEST_ASSERT_TRUE(isQuietHours(settings, clock));
    clock.timeSet = false;
    TEST_ASSERT_FALSE(isQuietHours(settings, clock));  // clock not set from the internet yet
    LocalTime local;
    TEST_ASSERT_FALSE(deviceLocalTime(settings, clock, local));
}

void test_quiet_hours_off_when_start_equals_end() {
    FakeSettingsStore store;
    DeviceSettings settings(store);
    FakeClock clock;
    clock.timeSet = true;
    TEST_ASSERT_TRUE(settings.setTimezone("Europe/London"));
    TEST_ASSERT_TRUE(settings.setQuietHours(0, 0));
    for (int h = 0; h < 24; ++h) {
        clock.unixSeconds = at(2026, 7, 15, h, 0);
        TEST_ASSERT_FALSE(isQuietHours(settings, clock));
    }
}

void test_quiet_hours_watcher() {
    QuietHoursWatcher w;
    TEST_ASSERT_TRUE(w.update(false) == QuietHoursWatcher::Change::None);
    TEST_ASSERT_TRUE(w.update(false) == QuietHoursWatcher::Change::None);
    TEST_ASSERT_TRUE(w.update(true) == QuietHoursWatcher::Change::Started);
    TEST_ASSERT_TRUE(w.isQuiet());
    TEST_ASSERT_TRUE(w.update(true) == QuietHoursWatcher::Change::None);
    TEST_ASSERT_TRUE(w.update(false) == QuietHoursWatcher::Change::Ended);
    TEST_ASSERT_FALSE(w.isQuiet());

    // Switched on in the middle of the night.
    QuietHoursWatcher night;
    TEST_ASSERT_TRUE(night.update(true) == QuietHoursWatcher::Change::Started);
}

// ---- Contacts asleep ----

void test_contact_asleep_in_their_own_timezone() {
    Contact granny;
    granny.timezone = "America/New_York";  // default sleep 21:00 to 09:00
    // 12:00 UTC in July is 08:00 in New York: still asleep.
    TEST_ASSERT_TRUE(isContactAsleep(granny, at(2026, 7, 15, 12, 59)));
    // 13:00 UTC is 09:00 there: awake.
    TEST_ASSERT_FALSE(isContactAsleep(granny, at(2026, 7, 15, 13, 0)));
    // 01:00 UTC is 21:00 there: asleep.
    TEST_ASSERT_TRUE(isContactAsleep(granny, at(2026, 7, 16, 1, 0)));
    TEST_ASSERT_FALSE(isContactAsleep(granny, at(2026, 7, 16, 0, 59)));

    // Someone in Sydney is asleep while it's afternoon in New York.
    Contact uncle;
    uncle.timezone = "Australia/Sydney";
    uncle.sleepStart = 22 * 60;
    uncle.sleepEnd = 6 * 60 + 30;
    TEST_ASSERT_TRUE(isContactAsleep(uncle, at(2026, 7, 15, 13, 0)));   // 23:00 there
    TEST_ASSERT_FALSE(isContactAsleep(uncle, at(2026, 7, 15, 20, 30)));  // 06:30 there
}

void test_contact_not_asleep_when_unknown() {
    Contact c;
    TEST_ASSERT_FALSE(isContactAsleep(c, at(2026, 7, 15, 3, 0)));  // no timezone
    c.timezone = "Nowhere/Special";
    TEST_ASSERT_FALSE(isContactAsleep(c, at(2026, 7, 15, 3, 0)));
    c.timezone = "Europe/London";
    FakeClock clock;
    clock.unixSeconds = at(2026, 7, 15, 3, 0);
    TEST_ASSERT_FALSE(isContactAsleep(c, clock));  // clock not set
    clock.timeSet = true;
    TEST_ASSERT_TRUE(isContactAsleep(c, clock));
}

// ---- Formatting ----

void test_formatting() {
    TEST_ASSERT_EQUAL_STRING("00:00", formatHourMinute(0).c_str());
    TEST_ASSERT_EQUAL_STRING("07:05", formatHourMinute(7 * 60 + 5).c_str());
    TEST_ASSERT_EQUAL_STRING("23:59", formatHourMinute(23 * 60 + 59).c_str());
    LocalTime t;
    t.hour = 14;
    t.minute = 32;
    TEST_ASSERT_EQUAL_STRING("14:32", formatHourMinute(t).c_str());
    TEST_ASSERT_EQUAL_STRING("Sunday", weekdayName(0).c_str());
    TEST_ASSERT_EQUAL_STRING("Friday", weekdayName(5).c_str());
    TEST_ASSERT_EQUAL_STRING("", weekdayName(7).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dates_round_trip);
    RUN_TEST(test_uk_daylight_saving);
    RUN_TEST(test_us_daylight_saving);
    RUN_TEST(test_southern_hemisphere_daylight_saving_crosses_new_year);
    RUN_TEST(test_half_hour_and_odd_offsets);
    RUN_TEST(test_rules_with_unusual_change_times);
    RUN_TEST(test_bad_rules_are_refused);
    RUN_TEST(test_every_built_in_rule_matches_the_c_library);
    RUN_TEST(test_table_lookups);
    RUN_TEST(test_countries_and_cities_for_the_picker);
    RUN_TEST(test_aliases_are_sorted_and_point_at_real_zones);
    RUN_TEST(test_local_time_by_zone_name);
    RUN_TEST(test_device_timezone_setting_uses_the_table);
    RUN_TEST(test_daily_hours_within_one_day);
    RUN_TEST(test_daily_hours_crossing_midnight);
    RUN_TEST(test_daily_hours_edge_cases);
    RUN_TEST(test_quiet_hours_use_the_box_timezone);
    RUN_TEST(test_quiet_hours_across_the_clocks_changing);
    RUN_TEST(test_not_quiet_when_time_or_timezone_unknown);
    RUN_TEST(test_quiet_hours_off_when_start_equals_end);
    RUN_TEST(test_quiet_hours_watcher);
    RUN_TEST(test_contact_asleep_in_their_own_timezone);
    RUN_TEST(test_contact_not_asleep_when_unknown);
    RUN_TEST(test_formatting);
    return UNITY_END();
}

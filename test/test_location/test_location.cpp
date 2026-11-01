// Guessing the box's country from its internet connection, and the moved-country
// check. Uses pretend location service replies; never touches the internet.

#include <unity.h>

#include <string>

#include "app/storage/DeviceSettings.h"
#include "app/time/Location.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;

static FakeHardware* hw;
static DeviceSettings* settings;

void setUp() {
    hw = new FakeHardware();
    settings = new DeviceSettings(hw->settings);
}

void tearDown() {
    delete settings;
    delete hw;
}

static const uint64_t kHour = 60ULL * 60 * 1000;

// Shaped like the real replies, with made-up addresses and places.
static std::string ipinfoReply(const char* country, const char* zone) {
    return std::string(R"({"ip":"192.0.2.10","city":"Sometown","region":"Somewhere","country":")") + country +
           R"(","loc":"0.0000,0.0000","org":"AS64500 Example","timezone":")" + zone + R"("})";
}

static std::string ipapiReply(const char* country, const char* zone) {
    return std::string(R"({"ip":"192.0.2.10","city":"Sometown","country_code":")") + country +
           R"(","country_name":"Wherever","timezone":")" + zone + R"(","utc_offset":"+0000"})";
}

static void queue(int status, const std::string& body) { hw->network.responses.push_back({status, body, ""}); }
static void queueOffline() { hw->network.responses.push_back({0, "", "could not connect"}); }

// ---- Reading replies ----

void test_reads_country_and_zone() {
    const LocationGuess g = parseLocationReply(ipinfoReply("GB", "Europe/London"), "country", "timezone");
    TEST_ASSERT_TRUE(g.ok);
    TEST_ASSERT_EQUAL_STRING("GB", g.country->code);
    TEST_ASSERT_EQUAL_STRING("Europe/London", g.zone->name);
}

void test_old_zone_names_are_understood() {
    const LocationGuess g = parseLocationReply(ipinfoReply("US", "America/Indiana/Indianapolis"), "country", "timezone");
    TEST_ASSERT_TRUE(g.ok);
    TEST_ASSERT_EQUAL_STRING("America/New_York", g.zone->name);
}

void test_country_with_several_zones_and_no_zone_asks_for_city() {
    const LocationGuess g = parseLocationReply(R"({"country":"AU"})", "country", "timezone");
    TEST_ASSERT_TRUE(g.ok);
    TEST_ASSERT_EQUAL_STRING("AU", g.country->code);
    TEST_ASSERT_NULL(g.zone);
}

void test_single_zone_country_fills_in_zone() {
    const LocationGuess g = parseLocationReply(ipinfoReply("FR", "Not/AZone"), "country", "timezone");
    TEST_ASSERT_TRUE(g.ok);
    TEST_ASSERT_EQUAL_STRING("Europe/Paris", g.zone->name);
}

void test_zone_from_another_country_is_not_trusted() {
    // A service near a border might say Spain but give France's timezone.
    const LocationGuess es = parseLocationReply(ipinfoReply("ES", "Europe/Paris"), "country", "timezone");
    TEST_ASSERT_TRUE(es.ok);
    TEST_ASSERT_EQUAL_STRING("ES", es.country->code);
    TEST_ASSERT_NULL(es.zone);  // Spain has more than one zone, so ask
    const LocationGuess de = parseLocationReply(ipinfoReply("DE", "Europe/Paris"), "country", "timezone");
    TEST_ASSERT_EQUAL_STRING("Europe/Berlin", de.zone->name);
}

void test_zone_alone_gives_country() {
    const LocationGuess g = parseLocationReply(R"({"timezone":"Asia/Tokyo"})", "country", "timezone");
    TEST_ASSERT_TRUE(g.ok);
    TEST_ASSERT_EQUAL_STRING("JP", g.country->code);
}

void test_rubbish_replies_fail() {
    TEST_ASSERT_FALSE(parseLocationReply("", "country", "timezone").ok);
    TEST_ASSERT_FALSE(parseLocationReply("<html>Too many requests</html>", "country", "timezone").ok);
    TEST_ASSERT_FALSE(parseLocationReply("[1,2]", "country", "timezone").ok);
    TEST_ASSERT_FALSE(parseLocationReply(R"({"country":"ZZ","timezone":"Nope/Nope"})", "country", "timezone").ok);
    TEST_ASSERT_FALSE(parseLocationReply(R"({"error":{"title":"Wrong ip"}})", "country", "timezone").ok);
}

// ---- Asking the services ----

void test_asks_first_service() {
    queue(200, ipinfoReply("NZ", "Pacific/Auckland"));
    LocationService service(hw->network);
    const LocationGuess g = service.guess();
    TEST_ASSERT_TRUE(g.ok);
    TEST_ASSERT_EQUAL_STRING("Pacific/Auckland", g.zone->name);
    TEST_ASSERT_EQUAL(1, hw->network.requests.size());
    TEST_ASSERT_EQUAL_STRING("GET", hw->network.requests[0].method.c_str());
    TEST_ASSERT_EQUAL_STRING("https://ipinfo.io/json", hw->network.requests[0].url.c_str());
}

void test_falls_back_to_second_service() {
    queue(429, R"({"error":"rate limited"})");
    queue(200, ipapiReply("IE", "Europe/Dublin"));
    LocationService service(hw->network);
    const LocationGuess g = service.guess();
    TEST_ASSERT_TRUE(g.ok);
    TEST_ASSERT_EQUAL_STRING("IE", g.country->code);
    TEST_ASSERT_EQUAL(2, hw->network.requests.size());
    TEST_ASSERT_EQUAL_STRING("https://ipapi.co/json/", hw->network.requests[1].url.c_str());
}

void test_fails_when_neither_answers() {
    queueOffline();
    queue(200, "not json");
    LocationService service(hw->network);
    const LocationGuess g = service.guess();
    TEST_ASSERT_FALSE(g.ok);
    TEST_ASSERT_NULL(g.country);
    TEST_ASSERT_TRUE(g.error.find("ipapi.co") != std::string::npos);
    // The reply's IP address never ends up in the error.
    TEST_ASSERT_TRUE(g.error.find("192.0.2") == std::string::npos);
}

// ---- Moved country ----

static LocationGuess guessFor(const char* country, const char* zone) {
    return parseLocationReply(ipinfoReply(country, zone), "country", "timezone");
}

void test_no_question_before_setup() {
    MovedCountryCheck check(hw->settings, *settings);
    TEST_ASSERT_NULL(check.consider(guessFor("ES", "Europe/Madrid")));
}

void test_no_question_at_home_or_when_guess_failed() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    MovedCountryCheck check(hw->settings, *settings);
    TEST_ASSERT_NULL(check.consider(guessFor("GB", "Europe/London")));
    TEST_ASSERT_NULL(check.consider(LocationGuess{}));
}

void test_asks_once_about_a_new_country() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    MovedCountryCheck check(hw->settings, *settings);
    const CountryInfo* spain = check.consider(guessFor("ES", "Europe/Madrid"));
    TEST_ASSERT_NOT_NULL(spain);
    TEST_ASSERT_EQUAL_STRING("Spain", spain->name);
    // Asked and the admins said no: don't ask again.
    check.markAsked(*spain);
    TEST_ASSERT_NULL(check.consider(guessFor("ES", "Europe/Madrid")));
    // Survives a restart.
    MovedCountryCheck afterRestart(hw->settings, *settings);
    TEST_ASSERT_NULL(afterRestart.consider(guessFor("ES", "Europe/Madrid")));
    // Somewhere else again is a new question.
    TEST_ASSERT_EQUAL_STRING("PT", afterRestart.consider(guessFor("PT", "Europe/Lisbon"))->code);
}

void test_going_home_and_back_asks_again() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    MovedCountryCheck check(hw->settings, *settings);
    check.markAsked(*check.consider(guessFor("ES", "Europe/Madrid")));
    TEST_ASSERT_NULL(check.consider(guessFor("GB", "Europe/London")));
    TEST_ASSERT_EQUAL_STRING("", check.lastAsked().c_str());
    TEST_ASSERT_NOT_NULL(check.consider(guessFor("ES", "Europe/Madrid")));
}

void test_accepting_the_move_stops_the_question() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    MovedCountryCheck check(hw->settings, *settings);
    const LocationGuess spain = guessFor("ES", "Europe/Madrid");
    check.markAsked(*check.consider(spain));
    // Admin said yes.
    TEST_ASSERT_TRUE(settings->setTimezone(spain.zone->name));
    TEST_ASSERT_NULL(check.consider(spain));
    // And the UK is now the "new" country.
    TEST_ASSERT_EQUAL_STRING("GB", check.consider(guessFor("GB", "Europe/London"))->code);
}

void test_moving_within_a_country_is_not_a_move() {
    TEST_ASSERT_TRUE(settings->setTimezone("America/New_York"));
    MovedCountryCheck check(hw->settings, *settings);
    TEST_ASSERT_NULL(check.consider(guessFor("US", "America/Los_Angeles")));
}

// ---- The daily watcher ----

void test_watcher_checks_on_start_then_daily() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    CountryWatcher watcher(hw->network, hw->settings, *settings, hw->log);

    queue(200, ipinfoReply("GB", "Europe/London"));
    TEST_ASSERT_FALSE(watcher.update(1000));
    TEST_ASSERT_EQUAL(1, hw->network.requests.size());
    TEST_ASSERT_TRUE(watcher.lastGuess().ok);

    // Not again until a day has passed.
    TEST_ASSERT_FALSE(watcher.update(1000 + 23 * kHour));
    TEST_ASSERT_EQUAL(1, hw->network.requests.size());

    queue(200, ipinfoReply("ES", "Europe/Madrid"));
    const CountryWatcher::Suggestion s = watcher.update(1000 + 24 * kHour);
    TEST_ASSERT_EQUAL(2, hw->network.requests.size());
    TEST_ASSERT_TRUE(static_cast<bool>(s));
    TEST_ASSERT_EQUAL_STRING("ES", s.country->code);
    TEST_ASSERT_EQUAL_STRING("Europe/Madrid", s.zone->name);
}

void test_watcher_retries_after_an_hour_when_check_fails() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    CountryWatcher watcher(hw->network, hw->settings, *settings, hw->log);
    queueOffline();
    queueOffline();
    TEST_ASSERT_FALSE(watcher.update(0));
    TEST_ASSERT_EQUAL(2, hw->network.requests.size());
    TEST_ASSERT_TRUE(hw->log.contains("Couldn't check the box's location"));

    TEST_ASSERT_FALSE(watcher.update(kHour - 1));
    TEST_ASSERT_EQUAL(2, hw->network.requests.size());
    queue(200, ipinfoReply("GB", "Europe/London"));
    watcher.update(kHour);
    TEST_ASSERT_EQUAL(3, hw->network.requests.size());
}

void test_watcher_waits_while_offline() {
    CountryWatcher watcher(hw->network, hw->settings, *settings, hw->log);
    hw->network.connected = false;
    TEST_ASSERT_FALSE(watcher.update(5 * kHour));
    TEST_ASSERT_EQUAL(0, hw->network.requests.size());
    hw->network.connected = true;
    queue(200, ipinfoReply("GB", "Europe/London"));
    watcher.update(5 * kHour + 1);
    TEST_ASSERT_EQUAL(1, hw->network.requests.size());
    // Before setup there's nothing to ask, but the guess is kept for setup to use.
    TEST_ASSERT_EQUAL_STRING("GB", watcher.lastGuess().country->code);
}

void test_watcher_check_soon() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    CountryWatcher watcher(hw->network, hw->settings, *settings, hw->log);
    queue(200, ipinfoReply("GB", "Europe/London"));
    watcher.update(0);
    watcher.checkSoon();
    queue(200, ipinfoReply("GB", "Europe/London"));
    watcher.update(10);
    TEST_ASSERT_EQUAL(2, hw->network.requests.size());
}

void test_watcher_asks_once_per_move() {
    TEST_ASSERT_TRUE(settings->setTimezone("Europe/London"));
    CountryWatcher watcher(hw->network, hw->settings, *settings, hw->log);
    queue(200, ipinfoReply("ES", "Europe/Madrid"));
    const auto first = watcher.update(0);
    TEST_ASSERT_TRUE(static_cast<bool>(first));
    watcher.movedCheck().markAsked(*first.country);
    queue(200, ipinfoReply("ES", "Europe/Madrid"));
    TEST_ASSERT_FALSE(watcher.update(24 * kHour));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reads_country_and_zone);
    RUN_TEST(test_old_zone_names_are_understood);
    RUN_TEST(test_country_with_several_zones_and_no_zone_asks_for_city);
    RUN_TEST(test_single_zone_country_fills_in_zone);
    RUN_TEST(test_zone_from_another_country_is_not_trusted);
    RUN_TEST(test_zone_alone_gives_country);
    RUN_TEST(test_rubbish_replies_fail);
    RUN_TEST(test_asks_first_service);
    RUN_TEST(test_falls_back_to_second_service);
    RUN_TEST(test_fails_when_neither_answers);
    RUN_TEST(test_no_question_before_setup);
    RUN_TEST(test_no_question_at_home_or_when_guess_failed);
    RUN_TEST(test_asks_once_about_a_new_country);
    RUN_TEST(test_going_home_and_back_asks_again);
    RUN_TEST(test_accepting_the_move_stops_the_question);
    RUN_TEST(test_moving_within_a_country_is_not_a_move);
    RUN_TEST(test_watcher_checks_on_start_then_daily);
    RUN_TEST(test_watcher_retries_after_an_hour_when_check_fails);
    RUN_TEST(test_watcher_waits_while_offline);
    RUN_TEST(test_watcher_check_soon);
    RUN_TEST(test_watcher_asks_once_per_move);
    return UNITY_END();
}

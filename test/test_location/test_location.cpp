// Guessing the box's country from its internet connection, for setup. Uses pretend location service replies; never touches the internet.

#include <unity.h>

#include <string>

#include "app/time/Location.h"
#include "fakes/Fakes.h"

using namespace fvb;
using namespace fvb::fakes;

static FakeHardware* hw;

void setUp() { hw = new FakeHardware(); }
void tearDown() { delete hw; }

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
    return UNITY_END();
}

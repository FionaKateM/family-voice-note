#include "app/time/Location.h"

#include <ArduinoJson.h>

namespace fvb {

namespace {

struct Service {
    const char* url;
    const char* countryField;
    const char* zoneField;
};

const Service kServices[] = {
    {"https://ipinfo.io/json", "country", "timezone"},
    {"https://ipapi.co/json/", "country_code", "timezone"},
};

}  // namespace

LocationGuess parseLocationReply(const std::string& body, const char* countryField, const char* zoneField) {
    LocationGuess guess;
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok || !doc.is<JsonObject>()) {
        guess.error = "reply wasn't JSON";
        return guess;
    }
    const char* countryCode = doc[countryField] | "";
    const char* zoneName = doc[zoneField] | "";

    const CountryInfo* country = timezones::findCountry(countryCode);
    const TimeZoneInfo* zone = timezones::find(zoneName);
    if (!country && zone) country = timezones::countryOf(*zone);
    if (!country) {
        guess.error = "reply had no country we know";
        return guess;
    }
    // Only trust the zone if it belongs to that country (the two can disagree near borders).
    if (zone && timezones::countryOf(*zone) != country) zone = nullptr;
    if (!zone) zone = timezones::onlyZoneIn(*country);

    guess.ok = true;
    guess.country = country;
    guess.zone = zone;
    return guess;
}

LocationGuess LocationService::guess() {
    LocationGuess last;
    last.error = "no location service answered";
    for (const auto& service : kServices) {
        HttpRequest request;
        request.url = service.url;
        request.headers["Accept"] = "application/json";
        request.headers["User-Agent"] = "FamilyVoiceBox";
        request.timeoutMs = 15000;
        const HttpResponse response = network_.request(request);
        if (!response.ok()) {
            last.error = std::string(service.url) + ": " +
                         (response.status == 0 ? response.error : "HTTP " + std::to_string(response.status));
            continue;
        }
        LocationGuess guess = parseLocationReply(response.body, service.countryField, service.zoneField);
        if (guess.ok) return guess;
        last.error = std::string(service.url) + ": " + guess.error;
    }
    return last;
}

}  // namespace fvb

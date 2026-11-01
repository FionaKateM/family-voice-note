#include "app/time/Location.h"

#include <ArduinoJson.h>

namespace fvb {

namespace {

constexpr const char* kAskedKey = "tz_asked";  // NVS key, 15 characters at most

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

const CountryInfo* MovedCountryCheck::consider(const LocationGuess& guess) {
    if (!guess.ok || !guess.country) return nullptr;
    const TimeZoneInfo* home = timezones::find(settings_.timezone());
    if (!home) return nullptr;  // not set up yet; setup asks its own question

    if (timezones::countryOf(*home) == guess.country) {
        // Back home (or never left). Forget the last question so a later move is asked about.
        if (store_.has(kAskedKey)) store_.remove(kAskedKey);
        return nullptr;
    }
    if (lastAsked() == guess.country->code) return nullptr;
    return guess.country;
}

void MovedCountryCheck::markAsked(const CountryInfo& country) { store_.setString(kAskedKey, country.code); }

std::string MovedCountryCheck::lastAsked() const { return store_.getString(kAskedKey); }

CountryWatcher::Suggestion CountryWatcher::update(uint64_t millisSinceBoot) {
    Suggestion suggestion;
    if (millisSinceBoot < nextCheckMs_ || !network_.isConnected()) return suggestion;

    const LocationGuess guess = service_.guess();
    if (!guess.ok) {
        log_.warning("Couldn't check the box's location: " + guess.error);
        nextCheckMs_ = millisSinceBoot + kRetryAfterMs;
        return suggestion;
    }
    nextCheckMs_ = millisSinceBoot + kCheckEveryMs;
    lastGuess_ = guess;

    suggestion.country = check_.consider(guess);
    if (suggestion.country) {
        suggestion.zone = guess.zone;
        log_.info(std::string("The box seems to have moved to ") + suggestion.country->name);
    }
    return suggestion;
}

}  // namespace fvb

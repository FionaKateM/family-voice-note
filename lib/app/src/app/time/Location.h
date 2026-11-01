#pragma once

#include <cstdint>
#include <string>

#include "app/storage/DeviceSettings.h"
#include "app/time/TimeZones.h"
#include "hal/Logger.h"
#include "hal/Network.h"
#include "hal/SettingsStore.h"

namespace fvb {

// A guess at where the box is, from its internet connection.
struct LocationGuess {
    bool ok = false;
    const CountryInfo* country = nullptr;  // set when ok
    // The zone the service suggested, if it's one we know and it's in that country.
    // nullptr for a country with several zones when the service didn't say which
    // (or said something we don't recognise): ask the admin to pick a city.
    const TimeZoneInfo* zone = nullptr;
    std::string error;  // why it failed, for the log. Never contains the IP address.
};

// Reads a reply from a location service. `countryField` and `zoneField` are the
// JSON field names that service uses.
LocationGuess parseLocationReply(const std::string& body, const char* countryField, const char* zoneField);

// Asks a location service where the box's internet connection is. These services
// look up the public IP address the request comes from, which they see anyway.
// Only the country and timezone are kept. The town and IP address in the reply
// are thrown away and never logged.
//
// ipinfo.io is tried first and ipapi.co if that fails. Both are free without an
// account at the box's rate of a few requests a day.
class LocationService {
public:
    explicit LocationService(Network& network) : network_(network) {}
    LocationGuess guess();

private:
    Network& network_;
};

// The admins' answer to "Looks like you're in [country]" is kept here, so the box
// asks once per move rather than every day.
//
// The flow, for whoever sends the message (the conversation code):
//   1. CountryWatcher::update says the box seems to be somewhere new.
//   2. Ask the admins. Then call markAsked() with that country.
//   3. If they say yes, save the new timezone with DeviceSettings::setTimezone.
//      If they say no, do nothing; the box won't ask about that country again
//      until it has been back home in between.
class MovedCountryCheck {
public:
    MovedCountryCheck(SettingsStore& store, const DeviceSettings& settings) : store_(store), settings_(settings) {}

    // What a fresh guess means. Returns the country to ask about, or nullptr.
    // Nothing is asked before the box's timezone has first been set (setup does
    // that) or when the guess failed.
    const CountryInfo* consider(const LocationGuess& guess);

    void markAsked(const CountryInfo& country);
    // The country last asked about, or "" if none.
    std::string lastAsked() const;

private:
    SettingsStore& store_;
    const DeviceSettings& settings_;
};

// Checks the box's location once when it first gets online after starting, then
// once a day, retrying after an hour if the check fails. Uses time since start-up,
// so it works before the clock is set.
class CountryWatcher {
public:
    static constexpr uint64_t kCheckEveryMs = 24ULL * 60 * 60 * 1000;
    static constexpr uint64_t kRetryAfterMs = 60ULL * 60 * 1000;

    struct Suggestion {
        const CountryInfo* country = nullptr;  // nullptr: nothing to ask
        const TimeZoneInfo* zone = nullptr;    // nullptr: ask which city
        explicit operator bool() const { return country != nullptr; }
    };

    CountryWatcher(Network& network, SettingsStore& store, const DeviceSettings& settings, Logger& log)
        : network_(network), service_(network), check_(store, settings), log_(log) {}

    // Call regularly. Does nothing unless a check is due and the box is online.
    // Makes a web request when a check is due, so it can take a few seconds.
    Suggestion update(uint64_t millisSinceBoot);

    // Check again on the next update (e.g. straight after Wi-Fi changes).
    void checkSoon() { nextCheckMs_ = 0; }

    // The most recent successful guess, for the setup flow's first question.
    const LocationGuess& lastGuess() const { return lastGuess_; }
    MovedCountryCheck& movedCheck() { return check_; }

private:
    Network& network_;
    LocationService service_;
    MovedCountryCheck check_;
    Logger& log_;
    uint64_t nextCheckMs_ = 0;
    LocationGuess lastGuess_;
};

}  // namespace fvb

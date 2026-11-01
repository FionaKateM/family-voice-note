#pragma once

#include <string>

#include "app/time/TimeZones.h"
#include "hal/Network.h"

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

// Asks a location service where the box's internet connection is. Used during
// setup to suggest the box's timezone ("Looks like you're in [country]. Is that
// right?"), and again whenever the box joins a different Wi-Fi network, to
// notice it has moved country (see app/settings/MovedCountry.h).
//
// The service looks up the public IP address the request comes from, which it
// sees anyway. Only the country and timezone are kept. The town and IP address
// in the reply are thrown away and never logged.
//
// ipinfo.io is tried first and ipapi.co if that fails. Both are free without an
// account. Makes up to two web requests, so it can take a few seconds.
class LocationService {
public:
    explicit LocationService(Network& network) : network_(network) {}
    LocationGuess guess();

private:
    Network& network_;
};

}  // namespace fvb

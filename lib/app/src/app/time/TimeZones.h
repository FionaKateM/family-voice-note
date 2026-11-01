#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/time/PosixTimeZone.h"
#include "app/time/TimeZoneData.h"

namespace fvb {
namespace timezones {

// Looking things up in the built-in table (TimeZoneData.h).
//
// A time zone is saved by its IANA name, e.g. "Europe/London". Older or
// alternative names (e.g. "Europe/Kiev", or "America/Detroit", which behaves the
// same as New York today) are understood and lead to the entry they match.

// nullptr if the name isn't known.
const TimeZoneInfo* find(const std::string& name);

// By two-letter code, any case. nullptr if not known.
const CountryInfo* findCountry(const std::string& code);
const CountryInfo* countryOf(const TimeZoneInfo& zone);

// All countries, sorted by name, for the country picker.
std::vector<const CountryInfo*> countries();
// A country's zones, sorted by city. Most countries have one, so the city picker
// can be skipped.
std::vector<const TimeZoneInfo*> zonesIn(const CountryInfo& country);
// The country's only zone, or nullptr if it has more than one (ask which city).
const TimeZoneInfo* onlyZoneIn(const CountryInfo& country);

// Local time in a zone. False if the zone name isn't known (out is left alone).
bool toLocal(const std::string& zoneName, int64_t unixTime, LocalTime& out);
LocalTime toLocal(const TimeZoneInfo& zone, int64_t unixTime);

// Which tzdata release the table was made from, e.g. "2026e". For /status.
const char* dataVersion();

}  // namespace timezones
}  // namespace fvb

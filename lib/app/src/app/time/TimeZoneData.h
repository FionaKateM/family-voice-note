#pragma once

#include <cstddef>
#include <cstdint>

// The built-in table of countries and time zones. The data is in TimeZoneData.cpp,
// made by scripts/make-timezones.py. Use the functions in TimeZones.h rather than
// reading these directly.

namespace fvb {

struct TimeZoneInfo {
    const char* name;     // IANA name, e.g. "Europe/London". This is what gets saved.
    const char* city;     // e.g. "London". Shown in the city picker.
    const char* rule;     // POSIX TZ rule, e.g. "GMT0BST,M3.5.0/1,M10.5.0"
    const char* country;  // two-letter code, e.g. "GB"
};

struct CountryInfo {
    const char* code;  // two-letter code, e.g. "GB"
    const char* name;  // e.g. "United Kingdom"
    uint16_t firstZone;  // index into kZones
    uint16_t zoneCount;  // 1 for most countries
};

struct ZoneAlias {
    const char* name;  // another IANA name, e.g. "Europe/Belfast"
    uint16_t zone;     // index into kZones of the entry that behaves the same
};

namespace tzdata {
extern const char* const kVersion;
extern const TimeZoneInfo kZones[];
extern const size_t kZoneCount;
extern const CountryInfo kCountries[];  // sorted by name
extern const size_t kCountryCount;
extern const ZoneAlias kAliases[];  // sorted by name
extern const size_t kAliasCount;
}  // namespace tzdata

}  // namespace fvb

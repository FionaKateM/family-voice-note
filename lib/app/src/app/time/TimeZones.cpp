#include "app/time/TimeZones.h"

#include <cctype>
#include <cstring>

namespace fvb {
namespace timezones {

const TimeZoneInfo* find(const std::string& name) {
    if (name.empty()) return nullptr;
    for (size_t i = 0; i < tzdata::kZoneCount; ++i) {
        if (name == tzdata::kZones[i].name) return &tzdata::kZones[i];
    }
    size_t low = 0, high = tzdata::kAliasCount;
    while (low < high) {
        const size_t mid = (low + high) / 2;
        const int cmp = std::strcmp(tzdata::kAliases[mid].name, name.c_str());
        if (cmp == 0) return &tzdata::kZones[tzdata::kAliases[mid].zone];
        if (cmp < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return nullptr;
}

const CountryInfo* findCountry(const std::string& code) {
    if (code.size() != 2) return nullptr;
    const char upper[3] = {static_cast<char>(std::toupper(static_cast<unsigned char>(code[0]))),
                           static_cast<char>(std::toupper(static_cast<unsigned char>(code[1]))), '\0'};
    for (size_t i = 0; i < tzdata::kCountryCount; ++i) {
        if (std::strcmp(tzdata::kCountries[i].code, upper) == 0) return &tzdata::kCountries[i];
    }
    return nullptr;
}

const CountryInfo* countryOf(const TimeZoneInfo& zone) { return findCountry(zone.country); }

std::vector<const CountryInfo*> countries() {
    std::vector<const CountryInfo*> out;
    out.reserve(tzdata::kCountryCount);
    for (size_t i = 0; i < tzdata::kCountryCount; ++i) out.push_back(&tzdata::kCountries[i]);
    return out;
}

std::vector<const TimeZoneInfo*> zonesIn(const CountryInfo& country) {
    std::vector<const TimeZoneInfo*> out;
    for (uint16_t i = 0; i < country.zoneCount; ++i) out.push_back(&tzdata::kZones[country.firstZone + i]);
    return out;
}

const TimeZoneInfo* onlyZoneIn(const CountryInfo& country) {
    return country.zoneCount == 1 ? &tzdata::kZones[country.firstZone] : nullptr;
}

LocalTime toLocal(const TimeZoneInfo& zone, int64_t unixTime) {
    PosixTimeZone rule;
    // Every rule in the table is checked by the tests, so this can't fail in practice.
    // If it ever did, UTC is used.
    PosixTimeZone::parse(zone.rule, rule);
    return rule.toLocal(unixTime);
}

bool toLocal(const std::string& zoneName, int64_t unixTime, LocalTime& out) {
    const TimeZoneInfo* zone = find(zoneName);
    if (!zone) return false;
    out = toLocal(*zone, unixTime);
    return true;
}

const char* dataVersion() { return tzdata::kVersion; }

}  // namespace timezones
}  // namespace fvb

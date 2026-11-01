#include "app/bot/Pickers.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "app/storage/TextRecord.h"
#include "app/time/TimeZones.h"

namespace fvb {

using telegram::InlineButton;
using telegram::InlineKeyboard;

namespace timezonePicker {

namespace {
// Button values:
//   tz:c:<code>    a country
//   tz:z:<index>   a city (index into the zone table)
//   tz:l:<letter>  countries starting with that letter
//   tz:az          the A to Z list of letters
constexpr const char* kPrefix = "tz:";

const char* kLettersQuestion = "Which letter does your country's name start with?";
const char* kCountryQuestion = "Which is your country?";
const char* kCityQuestion = "Which city has the same time as you? Pick the nearest one.";
const char* kAnotherCountry = "Another country";
const char* kBack = "\xE2\x86\x90 Back";  // ← Back

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The letter a country is listed under. "Åland Islands" goes under A.
char letterOf(const CountryInfo& country) {
    const std::string name = country.name;
    if (name.empty()) return '?';
    const unsigned char first = static_cast<unsigned char>(name[0]);
    if (first < 0x80) return static_cast<char>(std::toupper(first));
    if (name.compare(0, 2, "\xC3\x85") == 0) return 'A';  // Å
    return '?';
}

// Puts buttons into rows of `perRow`.
InlineKeyboard inRows(const std::vector<InlineButton>& buttons, size_t perRow) {
    InlineKeyboard keyboard;
    for (size_t i = 0; i < buttons.size(); ++i) {
        if (i % perRow == 0) keyboard.emplace_back();
        keyboard.back().push_back(buttons[i]);
    }
    return keyboard;
}

InlineKeyboard lettersKeyboard(const Context& ctx) {
    std::set<char> letters;
    for (const auto* c : timezones::countries()) letters.insert(letterOf(*c));
    std::vector<InlineButton> buttons;
    for (char letter : letters) {
        if (letter == '?') continue;
        const std::string l(1, letter);
        buttons.push_back(ctx.button(l, std::string(kPrefix) + "l:" + l));
    }
    return inRows(buttons, 6);
}

InlineKeyboard countriesKeyboard(const Context& ctx, const std::vector<const CountryInfo*>& countries) {
    std::vector<InlineButton> buttons;
    for (const auto* c : countries) buttons.push_back(ctx.button(c->name, std::string(kPrefix) + "c:" + c->code));
    InlineKeyboard keyboard = inRows(buttons, 2);
    keyboard.push_back({ctx.button(kBack, std::string(kPrefix) + "az")});
    return keyboard;
}

InlineKeyboard citiesKeyboard(const Context& ctx, const CountryInfo& country) {
    std::vector<InlineButton> buttons;
    for (const auto* zone : timezones::zonesIn(country)) {
        const size_t index = static_cast<size_t>(zone - tzdata::kZones);
        buttons.push_back(ctx.button(zone->city, std::string(kPrefix) + "z:" + std::to_string(index)));
    }
    InlineKeyboard keyboard = inRows(buttons, 2);
    keyboard.push_back({ctx.button(kBack, std::string(kPrefix) + "az")});
    return keyboard;
}

// A country was picked. Returns its zone if it only has one, otherwise shows the cities.
std::string chooseCountry(Context& ctx, const CountryInfo& country, bool editInPlace) {
    if (const TimeZoneInfo* only = timezones::onlyZoneIn(country)) {
        if (editInPlace) ctx.editButtonMessage(std::string("Country: ") + country.name);
        return only->name;
    }
    const InlineKeyboard keyboard = citiesKeyboard(ctx, country);
    const std::string question = std::string(country.name) + ". " + kCityQuestion;
    if (editInPlace) {
        ctx.editButtonMessage(question, &keyboard);
    } else {
        ctx.reply(question, &keyboard);
    }
    return "";
}
}  // namespace

void ask(Context& ctx, const std::string& question, const std::vector<std::string>& suggestions) {
    std::vector<InlineButton> buttons;
    std::set<std::string> seen;
    for (const auto& code : suggestions) {
        const CountryInfo* c = timezones::findCountry(code);
        if (!c || !seen.insert(c->code).second) continue;
        buttons.push_back(ctx.button(c->name, std::string(kPrefix) + "c:" + c->code));
    }
    if (buttons.empty()) {
        const InlineKeyboard keyboard = lettersKeyboard(ctx);
        ctx.reply(question + " Type its name, or tap the letter it starts with.", &keyboard);
        return;
    }
    InlineKeyboard keyboard = inRows(buttons, 2);
    keyboard.push_back({ctx.button(kAnotherCountry, std::string(kPrefix) + "az")});
    ctx.reply(question + " Tap it, or type its name.", &keyboard);
}

bool owns(const std::string& value) { return value.compare(0, 3, kPrefix) == 0; }

std::string onButton(Context& ctx, const std::string& value) {
    if (!owns(value)) return "";
    const std::string rest = value.substr(3);

    if (rest == "az") {
        const InlineKeyboard keyboard = lettersKeyboard(ctx);
        ctx.editButtonMessage(kLettersQuestion, &keyboard);
        return "";
    }
    if (rest.compare(0, 2, "l:") == 0 && rest.size() == 3) {
        std::vector<const CountryInfo*> matching;
        for (const auto* c : timezones::countries()) {
            if (letterOf(*c) == rest[2]) matching.push_back(c);
        }
        const InlineKeyboard keyboard = countriesKeyboard(ctx, matching);
        ctx.editButtonMessage(kCountryQuestion, &keyboard);
        return "";
    }
    if (rest.compare(0, 2, "c:") == 0) {
        if (const CountryInfo* c = timezones::findCountry(rest.substr(2))) return chooseCountry(ctx, *c, true);
    }
    if (rest.compare(0, 2, "z:") == 0) {
        uint64_t index = 0;
        if (text::parseUint(rest.substr(2), index) && index < tzdata::kZoneCount) {
            const TimeZoneInfo& zone = tzdata::kZones[index];
            ctx.editButtonMessage("Time zone: " + describe(zone.name));
            return zone.name;
        }
    }
    ctx.popup("This button no longer works.");
    return "";
}

std::vector<std::string> findCountries(const std::string& typed) {
    const std::string want = lower(text::trim(typed));
    if (want.empty()) return {};

    // Common names people might type that aren't the official one.
    static const std::pair<const char*, const char*> kShortNames[] = {
        {"uk", "GB"},      {"britain", "GB"},     {"great britain", "GB"}, {"england", "GB"},
        {"scotland", "GB"}, {"wales", "GB"},       {"northern ireland", "GB"}, {"us", "US"},
        {"usa", "US"},     {"america", "US"},     {"uae", "AE"},
    };
    for (const auto& s : kShortNames) {
        if (want == s.first && timezones::findCountry(s.second)) return {s.second};
    }

    std::vector<std::string> matches;
    for (const auto* c : timezones::countries()) {
        const std::string name = lower(c->name);
        if (name == want || lower(c->code) == want) return {c->code};
        if (want.size() >= 3 && name.find(want) != std::string::npos) matches.push_back(c->code);
    }
    return matches;
}

std::string onText(Context& ctx, const std::string& typed) {
    const auto matches = findCountries(typed);
    if (matches.size() == 1) {
        if (const CountryInfo* c = timezones::findCountry(matches[0])) return chooseCountry(ctx, *c, false);
    }
    if (matches.size() > 1 && matches.size() <= 12) {
        std::vector<const CountryInfo*> countries;
        for (const auto& code : matches) countries.push_back(timezones::findCountry(code));
        const InlineKeyboard keyboard = countriesKeyboard(ctx, countries);
        ctx.reply("Which one?", &keyboard);
        return "";
    }
    const InlineKeyboard keyboard = lettersKeyboard(ctx);
    ctx.reply("I couldn't find that country. Tap the letter its name starts with.", &keyboard);
    return "";
}

std::string describe(const std::string& zoneName) {
    const TimeZoneInfo* zone = timezones::find(zoneName);
    if (!zone) return "";
    const CountryInfo* country = timezones::countryOf(*zone);
    if (!country) return zone->city;
    if (country->zoneCount <= 1) return country->name;
    return std::string(country->name) + " (" + zone->city + ")";
}

}  // namespace timezonePicker

namespace hourPicker {

InlineKeyboard keyboard(const Context& ctx, const std::string& prefix) {
    InlineKeyboard rows;
    for (int hour = 0; hour < 24; ++hour) {
        if (hour % 6 == 0) rows.emplace_back();
        rows.back().push_back(
            ctx.button(formatTimeOfDay(static_cast<uint16_t>(hour * 60)), prefix + ":" + std::to_string(hour)));
    }
    return rows;
}

bool parse(const std::string& value, const std::string& prefix, uint16_t& minutes) {
    if (value.compare(0, prefix.size() + 1, prefix + ":") != 0) return false;
    uint64_t hour = 0;
    if (!text::parseUint(value.substr(prefix.size() + 1), hour) || hour > 23) return false;
    minutes = static_cast<uint16_t>(hour * 60);
    return true;
}

}  // namespace hourPicker

std::string formatTimeOfDay(uint16_t minutes) {
    const int hour = (minutes / 60) % 24;
    const int minute = minutes % 60;
    const int twelve = hour % 12 == 0 ? 12 : hour % 12;
    std::string out = std::to_string(twelve);
    if (minute != 0) out += (minute < 10 ? ":0" : ":") + std::to_string(minute);
    return out + (hour < 12 ? "am" : "pm");
}

}  // namespace fvb

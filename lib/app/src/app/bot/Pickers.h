#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/bot/Bot.h"

namespace fvb {

// Tap-button pickers shared by conversations. They only work inside a
// conversation, because they use Context::button.

// Picks a time zone without anyone typing a time zone name: country first,
// then city if the country has more than one time zone.
//
// The first question offers a few likely countries (for example the box's own)
// and an "Another country" button that leads to an A to Z list. People can also
// type a country's name instead.
//
// Its button values all start with "tz:", so a conversation can pass any
// button starting with that to onButton.
namespace timezonePicker {

// Sends the first question. `suggestions` are two-letter country codes to offer
// first; unknown ones are skipped.
void ask(Context& ctx, const std::string& question, const std::vector<std::string>& suggestions);

// Skips the country question and sends the city list for a country with more
// than one time zone. Returns the zone name straight away (sending nothing) if
// the country only has one; "" otherwise, or if the code isn't known.
std::string askCity(Context& ctx, const std::string& countryCode);

// True if `value` is one of the picker's buttons.
bool owns(const std::string& value);

// Handles one of the picker's buttons, moving to the next list by changing the
// message in place. Returns the chosen zone's IANA name once a country with
// one zone, or a city, has been picked; otherwise "".
std::string onButton(Context& ctx, const std::string& value);

// Someone typed instead of tapping. Looks for a country with that name.
// Returns the zone name if that settles it, otherwise "" (having replied with
// the matching countries, the city list, or a "couldn't find it" message).
std::string onText(Context& ctx, const std::string& text);

// Countries whose name (or a common short name such as "UK") matches `text`,
// any case. An exact match comes back on its own. For tests.
std::vector<std::string> findCountries(const std::string& text);

// "United Kingdom" or "United States (Chicago)" for a zone name, "" if unknown.
std::string describe(const std::string& zoneName);

}  // namespace timezonePicker

// Picks an hour of the day: 24 buttons, "12am" to "11pm".
// Button values are "<prefix>:<hour>".
namespace hourPicker {

telegram::InlineKeyboard keyboard(const Context& ctx, const std::string& prefix);
// True with the time in minutes after midnight if `value` is one of this
// picker's buttons for `prefix`.
bool parse(const std::string& value, const std::string& prefix, uint16_t& minutes);

}  // namespace hourPicker

// "9pm", "12am" (midnight), "12pm" (midday), "6:30am". Takes minutes after midnight.
std::string formatTimeOfDay(uint16_t minutes);

}  // namespace fvb

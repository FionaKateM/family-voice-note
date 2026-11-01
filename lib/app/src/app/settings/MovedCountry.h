#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/bot/Bot.h"
#include "app/time/Location.h"

namespace fvb {

// Noticing the box has moved country (section 8 of the requirements).
//
// Whenever the box joins a different Wi-Fi network from last time, it asks a
// location service which country it's in. If that isn't the country of the
// box's time zone, every admin gets:
//
//   "Looks like Child's box is now in France. Switch to France time? [Yes] [No]"
//
// The first admin to answer decides, and everyone's message changes to say who
// decided, the same way approvals work. Yes switches the time zone (asking
// which city first, if the country has several time zones and the location
// service didn't say). No means the box never asks about that country again.
//
// Nothing is asked until the box's time zone has been set for the first time:
// the setup conversation does that (ticket 1.8).
//
// Running the check is split in two so the slow part (asking the location
// service over the internet) can happen without holding the storage lock:
//
//   if (moved.shouldCheck(network.wifiName())) {
//       LocationGuess guess = LocationService(network).guess();   // slow
//       moved.check(network.wifiName(), guess);                   // quick
//   }
//
// Create one after the Bot and AdminSettings; it registers its buttons with
// the Bot. All three must stay alive together.
class MovedCountry {
public:
    // Most countries remembered as "don't ask again". The oldest is forgotten first.
    static constexpr size_t kMaxDeclined = 20;

    MovedCountry(Bot& bot, telegram::TelegramClient& client, Storage& storage, SettingsStore& store, Logger& log);

    // True if the box should check where it is: it's on a Wi-Fi network it
    // wasn't on last time, and its time zone has been set. Doesn't change anything.
    bool shouldCheck(const std::string& wifiName) const;

    // Acts on a location guess made after joining `wifiName`. Asks the admins if
    // the box seems to be in a different country. Returns true if it asked.
    //
    // A failed guess changes nothing, so the box checks again next time it
    // joins a network. A successful one remembers `wifiName` as checked.
    bool check(const std::string& wifiName, const LocationGuess& guess);

    // Two-letter codes of countries the admins said no to. For tests.
    std::vector<std::string> declinedCountries() const;
    // The country an unanswered question is about, or "". For tests.
    std::string askingAbout() const;

private:
    // The question waiting for an answer, saved so it survives a restart.
    struct Question {
        std::string country;  // two-letter code
        std::string zone;     // the suggested zone, or "" to ask for a city
        std::vector<std::pair<int64_t, int64_t>> adminMessages;  // (admin, message ID)
    };

    bool loadQuestion(Question& out) const;
    void saveQuestion(const Question& q);
    void clearQuestion();
    // Changes every admin's copy of the question to `text`, except `skipAdmin`'s.
    void editAll(const Question& q, const std::string& text, int64_t skipAdmin = 0);
    void decline(const std::string& country);
    void onButton(Context& ctx, const std::string& data);
    std::string childName() const;

    telegram::TelegramClient& client_;
    Storage& storage_;
    SettingsStore& store_;
    Logger& log_;
};

}  // namespace fvb

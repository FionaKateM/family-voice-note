#include "app/settings/MovedCountry.h"

#include <algorithm>

#include "app/bot/Pickers.h"
#include "app/settings/AdminSettings.h"
#include "app/storage/TextRecord.h"
#include "app/time/TimeZones.h"

namespace fvb {

using telegram::InlineButton;
using telegram::InlineKeyboard;

namespace {
// NVS keys. 15 characters at most.
constexpr const char* kLastWifi = "tz_last_wifi";   // the network the last successful check was made on
constexpr const char* kDeclined = "tz_declined";    // country codes, tab separated, oldest first
constexpr const char* kQuestion = "tz_moved_ask";   // the unanswered question, see Question

constexpr const char* kPrefix = "moved";

std::string countryName(const std::string& code) {
    const CountryInfo* c = timezones::findCountry(code);
    return c ? c->name : code;
}

// "France" or "United States (Chicago)": what Yes would switch to.
std::string switchTo(const std::string& country, const std::string& zone) {
    if (!zone.empty()) {
        const std::string described = timezonePicker::describe(zone);
        if (!described.empty()) return described;
    }
    return countryName(country);
}

// The country the box's time zone is in, or "" if it hasn't been set.
std::string boxCountry(const DeviceSettings& settings) {
    const TimeZoneInfo* zone = timezones::find(settings.timezone());
    return zone ? zone->country : "";
}
}  // namespace

MovedCountry::MovedCountry(Bot& bot, telegram::TelegramClient& client, Storage& storage, SettingsStore& store,
                           Logger& log)
    : client_(client), storage_(storage), store_(store), log_(log) {
    bot.addButtons(kPrefix, Roles::kAdmin, [this](Context& ctx, const std::string& data) { onButton(ctx, data); });
}

std::string MovedCountry::childName() const { return storage_.settings.childName(); }

bool MovedCountry::shouldCheck(const std::string& wifiName) const {
    if (wifiName.empty()) return false;
    if (boxCountry(storage_.settings).empty()) return false;  // setup hasn't set the time zone yet
    return store_.getString(kLastWifi) != wifiName;
}

bool MovedCountry::check(const std::string& wifiName, const LocationGuess& guess) {
    if (!guess.ok || !guess.country) {
        log_.warning("Couldn't check which country the box is in: " + guess.error);
        return false;
    }
    if (!wifiName.empty()) store_.setString(kLastWifi, wifiName);

    const std::string here = guess.country->code;
    const std::string home = boxCountry(storage_.settings);
    if (home.empty()) return false;

    Question waiting;
    const bool hasWaiting = loadQuestion(waiting);

    if (here == home) {
        // Back where its clock already is. An unanswered question about
        // somewhere else no longer makes sense.
        if (hasWaiting) {
            editAll(waiting, "The box is back in " + countryName(home) + ", so its time zone doesn't need to change.");
            clearQuestion();
        }
        return false;
    }

    const auto declined = declinedCountries();
    if (std::find(declined.begin(), declined.end(), here) != declined.end()) {
        log_.info("The box seems to be in a country the admins said not to switch to");
        return false;
    }
    if (hasWaiting && waiting.country == here) return false;  // already asked; still waiting
    if (hasWaiting) {
        editAll(waiting, "The box has moved again, so this question no longer applies.");
        clearQuestion();
    }

    Question q;
    q.country = here;
    q.zone = guess.zone ? guess.zone->name : "";
    const std::string target = switchTo(q.country, q.zone);
    std::string text = "Looks like " + childName() + "'s box is now in " + countryName(here) + ". Switch to " +
                       target + " time?";
    if (q.zone.empty()) text += " You'll be asked which city is nearest.";
    text += "\n\nAt the moment it's on " + timezonePicker::describe(storage_.settings.timezone()) + " time.";

    const std::string base = std::string(kPrefix) + ":" + here + ":";
    const InlineKeyboard keyboard = {{InlineButton{"Yes", base + "y"}, InlineButton{"No", base + "n"}}};
    for (const auto& admin : storage_.people.admins()) {
        telegram::Message sent;
        if (client_.sendMessage(admin.telegramId, text, &keyboard, &sent)) {
            q.adminMessages.emplace_back(admin.telegramId, sent.messageId);
        } else {
            log_.warning("Couldn't ask an admin about the box moving country");
        }
    }
    if (q.adminMessages.empty()) return false;  // nobody heard; try again next time
    saveQuestion(q);
    log_.info("The box seems to have moved country; asked the admins whether to change its time zone");
    return true;
}

void MovedCountry::onButton(Context& ctx, const std::string& data) {
    // data is "<country>:y" or "<country>:n"
    const size_t colon = data.find(':');
    const std::string country = data.substr(0, colon);
    const std::string answer = colon == std::string::npos ? "" : data.substr(colon + 1);
    Question q;
    if (colon == std::string::npos || (answer != "y" && answer != "n") || !loadQuestion(q) || q.country != country) {
        ctx.popup("This has already been answered.");
        ctx.editButtonMessage("This has already been answered.");
        return;
    }

    const std::string who = ctx.sender().firstName.empty() ? "An admin" : ctx.sender().firstName;
    const std::string target = switchTo(q.country, q.zone);
    clearQuestion();

    if (answer == "n") {
        decline(q.country);
        const std::string kept = timezonePicker::describe(storage_.settings.timezone());
        const std::string after = " I won't ask about " + countryName(q.country) + " again.";
        ctx.editButtonMessage("Kept " + kept + " time." + after + " You can change it in /settings at any time.");
        editAll(q, who + " chose to keep " + kept + " time." + after, ctx.sender().userId);
        ctx.popup("Kept");
        log_.info("An admin chose not to change the time zone after the box moved");
        return;
    }

    if (q.zone.empty()) {
        // A country with several time zones, and the location service didn't
        // say which. This admin picks the city.
        ctx.editButtonMessage("Switching to " + target + " time.");
        editAll(q, who + " is choosing the box's new time zone.", ctx.sender().userId);
        ctx.startConversation(AdminSettings::kConversation, {{"open", "city"}, {"country", q.country}});
        return;
    }

    if (!storage_.settings.setTimezone(q.zone)) {
        saveQuestion(q);  // put it back so they can try again
        ctx.popup("Sorry, I couldn't save that. Try again in a minute.");
        return;
    }
    ctx.editButtonMessage("Switched to " + target + " time.");
    editAll(q, who + " switched the box to " + target + " time.", ctx.sender().userId);
    ctx.popup("Switched");
    log_.info("An admin changed the time zone after the box moved: " + q.zone);
}

void MovedCountry::editAll(const Question& q, const std::string& text, int64_t skipAdmin) {
    for (const auto& m : q.adminMessages) {
        if (m.first == skipAdmin) continue;
        if (!client_.editMessageText(m.first, m.second, text)) {
            log_.warning("Couldn't update an admin's message about the box moving country");
        }
    }
}

// ---------------------------------------------------------------------------
// Remembering things

std::vector<std::string> MovedCountry::declinedCountries() const {
    const std::string saved = store_.getString(kDeclined);
    if (saved.empty()) return {};
    return text::splitFields(saved);
}

void MovedCountry::decline(const std::string& country) {
    auto list = declinedCountries();
    list.erase(std::remove(list.begin(), list.end(), country), list.end());
    list.push_back(country);
    while (list.size() > kMaxDeclined) list.erase(list.begin());
    if (!store_.setString(kDeclined, text::joinFields(list))) {
        log_.warning("Couldn't save which countries not to ask about again");
    }
}

std::string MovedCountry::askingAbout() const {
    Question q;
    return loadQuestion(q) ? q.country : "";
}

bool MovedCountry::loadQuestion(Question& out) const {
    const std::string saved = store_.getString(kQuestion);
    if (saved.empty()) return false;
    const auto fields = text::splitFields(saved);
    if (fields.size() < 2 || fields.size() % 2 != 0 || fields[0].empty()) return false;
    out = Question();
    out.country = fields[0];
    out.zone = fields[1];
    for (size_t i = 2; i + 1 < fields.size(); i += 2) {
        int64_t admin = 0, message = 0;
        if (text::parseInt(fields[i], admin) && text::parseInt(fields[i + 1], message)) {
            out.adminMessages.emplace_back(admin, message);
        }
    }
    return true;
}

void MovedCountry::saveQuestion(const Question& q) {
    std::vector<std::string> fields = {q.country, q.zone};
    for (const auto& m : q.adminMessages) {
        fields.push_back(std::to_string(m.first));
        fields.push_back(std::to_string(m.second));
    }
    if (!store_.setString(kQuestion, text::joinFields(fields))) {
        log_.warning("Couldn't save the question about the box moving country");
    }
}

void MovedCountry::clearQuestion() { store_.remove(kQuestion); }

}  // namespace fvb

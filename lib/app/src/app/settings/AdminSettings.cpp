#include "app/settings/AdminSettings.h"

#include <cctype>
#include <set>
#include <vector>

#include "app/Version.h"
#include "app/bot/Pickers.h"
#include "app/storage/TextRecord.h"
#include "app/time/TimeChecks.h"
#include "app/time/TimeZones.h"

namespace fvb {

using telegram::InlineButton;
using telegram::InlineKeyboard;
using telegram::Message;
using telegram::MessageKind;

namespace {
const char* kUseButtons = "Tap one of the buttons above, or send /cancel to stop.";
const char* kButtonGone = "This button no longer works.";
const char* kBack = "\xE2\x86\x90 Back";      // ← Back
const char* kTick = "\xE2\x9C\x93 ";           // ✓
const char* kEnDash = " \xE2\x80\x93 ";        // –

// Choices offered as buttons. Other values can be typed.
const uint32_t kRecordingChoices[] = {30, 60, 120, 180, 360, 600, 900};
const uint32_t kKeepChoices[] = {30, 60, 90, 180, 365, 0};
// The child would hear nothing at 0, so voice volume starts at 10%.
constexpr uint8_t kMinVoiceVolume = 10;

size_t characterCount(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;  // count everything except UTF-8 continuation bytes
    }
    return n;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string current;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool parseNumber(std::string s, uint64_t& out) {
    s = text::trim(s);
    if (!s.empty() && s.back() == '%') s.pop_back();
    return text::parseUint(text::trim(s), out);
}

std::string quietHoursText(uint16_t start, uint16_t end) {
    if (start == end) return "off";
    return formatTimeOfDay(start) + " to " + formatTimeOfDay(end);
}

std::string keepText(uint32_t days) {
    if (days == 0) return "kept on the box for ever";
    return "removed from the box after " + std::to_string(days) + (days == 1 ? " day" : " days");
}

std::string keepLabel(uint32_t days) {
    switch (days) {
        case 0: return "For ever";
        case 180: return "6 months";
        case 365: return "1 year";
        default: return std::to_string(days) + " days";
    }
}

std::string recordingLabel(uint32_t seconds) {
    if (seconds < 60) return std::to_string(seconds) + " sec";
    return std::to_string(seconds / 60) + " min";
}

const char* monitorModeText(MonitorMode mode) {
    switch (mode) {
        case MonitorMode::Off: return "Off";
        case MonitorMode::SentOnly: return "Sent only";
        case MonitorMode::ReceivedOnly: return "Received only";
        case MonitorMode::All: return "All";
    }
    return "Off";
}

bool parseMonitorMode(const std::string& typed, MonitorMode& out) {
    const std::string w = lower(text::trim(typed));
    if (w == "off" || w == "none") out = MonitorMode::Off;
    else if (w == "sent" || w == "sent only") out = MonitorMode::SentOnly;
    else if (w == "received" || w == "received only") out = MonitorMode::ReceivedOnly;
    else if (w == "all" || w == "both") out = MonitorMode::All;
    else return false;
    return true;
}

std::string personName(const Person& p) { return p.name.empty() ? "Someone with no name yet" : p.name; }

std::string thousands(uint64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

const char* monthName(int month) {
    static const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};
    return month >= 1 && month <= 12 ? kMonths[month - 1] : "";
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
}  // namespace

bool parseTimeOfDay(const std::string& typed, uint16_t& minutes) {
    std::string s;
    for (char c : lower(typed)) {
        if (c != ' ') s += c;
    }
    bool am = false, pm = false;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "am") == 0) am = true;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "pm") == 0) pm = true;
    if (am || pm) s.resize(s.size() - 2);

    std::string hourPart = s, minutePart = "0";
    const size_t sep = s.find_first_of(":.");
    if (sep != std::string::npos) {
        hourPart = s.substr(0, sep);
        minutePart = s.substr(sep + 1);
        if (minutePart.size() != 2) return false;
    }
    if (hourPart.empty() || hourPart.size() > 2) return false;
    uint64_t hour = 0, minute = 0;
    if (!text::parseUint(hourPart, hour) || !text::parseUint(minutePart, minute) || minute > 59) return false;
    if (am || pm) {
        if (hour < 1 || hour > 12) return false;
        hour %= 12;
        if (pm) hour += 12;
    } else if (hour > 23) {
        return false;
    }
    minutes = static_cast<uint16_t>(hour * 60 + minute);
    return true;
}

std::string describeSeconds(uint32_t seconds) {
    const uint32_t m = seconds / 60, s = seconds % 60;
    std::string out;
    if (m > 0) out = std::to_string(m) + (m == 1 ? " minute" : " minutes");
    if (s > 0 || m == 0) out += (out.empty() ? "" : " ") + std::to_string(s) + (s == 1 ? " second" : " seconds");
    return out;
}

// ---------------------------------------------------------------------------
// The /settings menu
//
// One message that changes in place as the admin taps through it. The page
// they're on is the conversation step:
//
//   main, contacts, monitors, mode, admins, remove_contact, remove_monitor,
//   remove_admin, confirm, quiet, quiet_start, quiet_end, zone, zone_pick,
//   voice, alert, maxrec, keep, name, status
//
// After a change is saved it goes back to the main page, saying what changed.

class AdminSettings::Menu : public ConversationType {
public:
    explicit Menu(AdminSettings& s) : s_(s) {}

    const char* name() const override { return kConversation; }
    bool allows(const Roles& roles) const override { return roles.isAdmin(); }

    // state "open" says which page to start on (a direct command, or "city"
    // from the moved-country question). "typed" is a country typed after /timezone.
    Outcome begin(Context& ctx, ConversationState& state) override {
        const std::string page = state.get("open", "main");
        if (page == "city") {
            state.step = "zone_pick";
            const std::string zone = timezonePicker::askCity(ctx, state.get("country"));
            if (!zone.empty()) return zoneChosen(ctx, state, zone);
            if (!timezones::findCountry(state.get("country"))) show(ctx, state, "main");
            return Outcome::Continue;
        }
        if (page == "zone" && !state.get("typed").empty()) {
            state.step = "zone_pick";
            const std::string zone = timezonePicker::onText(ctx, state.get("typed"));
            return zone.empty() ? Outcome::Continue : zoneChosen(ctx, state, zone);
        }
        show(ctx, state, page);
        return Outcome::Continue;
    }

    Outcome onMessage(Context& ctx, ConversationState& state, const Message& message) override {
        const bool isText = message.kind == MessageKind::Text;
        const std::string typed = isText ? text::trim(message.text) : "";
        uint64_t n = 0;

        if (state.step == "zone_pick") {
            if (!isText) {
                ctx.reply("Tap the country above, or type its name.");
                return Outcome::Continue;
            }
            const std::string zone = timezonePicker::onText(ctx, typed);
            return zone.empty() ? Outcome::Continue : zoneChosen(ctx, state, zone);
        }
        if (state.step == "maxrec") {
            const std::string done = parseNumber(typed, n) && n <= 100000 ? s_.setMaxRecording(static_cast<uint32_t>(n)) : "";
            if (done.empty()) {
                ctx.reply("Please type a number of seconds from " +
                          std::to_string(DeviceSettings::kMinMaxRecordingSeconds) + " to " +
                          std::to_string(DeviceSettings::kMaxMaxRecordingSeconds) + ", or tap a button above.");
                return Outcome::Continue;
            }
            return saved(ctx, state, done, true);
        }
        if (state.step == "keep") {
            const std::string done = parseNumber(typed, n) && n <= 100000 ? s_.setKeepDays(static_cast<uint32_t>(n)) : "";
            if (done.empty()) {
                ctx.reply("Please type a number of days from 0 to " + std::to_string(DeviceSettings::kMaxKeepDays) +
                          " (0 keeps them for ever), or tap a button above.");
                return Outcome::Continue;
            }
            return saved(ctx, state, done, true);
        }
        if (state.step == "name") {
            std::string problem;
            const std::string done = isText ? s_.setChildName(message.text, problem) : "";
            if (done.empty()) {
                ctx.reply(problem.empty() ? "Please type the child's name." : problem);
                return Outcome::Continue;
            }
            return saved(ctx, state, done, true);
        }
        ctx.reply(kUseButtons);
        return Outcome::Continue;
    }

    Outcome onButton(Context& ctx, ConversationState& state, const std::string& value) override {
        uint16_t minutes = 0;
        uint64_t n = 0;

        if (value == "done") {
            ctx.editButtonMessage("Settings closed.");
            return Outcome::Finished;
        }
        if (value.compare(0, 3, "go:") == 0) {
            const std::string page = value.substr(3);
            if (page == "zone_pick") {
                ctx.editButtonMessage("Changing the box's time zone.");
                askCountry(ctx, state);
                return Outcome::Continue;
            }
            static const std::set<std::string> kPages = {
                "main",  "contacts", "monitors", "mode",   "admins", "remove_contact", "remove_monitor",
                "remove_admin", "quiet", "quiet_start", "zone", "voice", "alert", "maxrec", "keep", "name", "status"};
            if (kPages.count(page)) {
                show(ctx, state, page);
                return Outcome::Continue;
            }
        }
        if (value.size() == 5 && value.compare(0, 4, "inv:") == 0) {
            const InviteRole role = value[4] == 'm' ? InviteRole::Monitor
                                    : value[4] == 'a' ? InviteRole::Admin
                                                      : InviteRole::Contact;
            ctx.editButtonMessage("Settings closed.");
            s_.invites_.makeInvite(ctx, role);
            return Outcome::Finished;
        }
        if (value == "trash") {
            ctx.editButtonMessage("Settings closed.");
            s_.sendLastDeleted(ctx);
            return Outcome::Finished;
        }
        if ((value.compare(0, 3, "rc:") == 0 || value.compare(0, 3, "rm:") == 0 || value.compare(0, 3, "ra:") == 0) &&
            value.size() > 3) {
            state.set("remove_kind", value.substr(0, 2));
            state.set("remove_id", value.substr(3));
            show(ctx, state, "confirm");
            return Outcome::Continue;
        }
        if (value == "cfm" && state.step == "confirm") return remove(ctx, state);

        if (value.compare(0, 5, "mode:") == 0 && text::parseUint(value.substr(5), n) && n <= 3) {
            return saved(ctx, state, s_.setMonitorMode(static_cast<MonitorMode>(n)));
        }
        if (value.compare(0, 4, "vol:") == 0 && text::parseUint(value.substr(4), n) && n <= 100) {
            return saved(ctx, state, s_.setVolume(false, static_cast<uint8_t>(n)));
        }
        if (value.compare(0, 4, "alv:") == 0 && text::parseUint(value.substr(4), n) && n <= 100) {
            return saved(ctx, state, s_.setVolume(true, static_cast<uint8_t>(n)));
        }
        if (value.compare(0, 4, "rec:") == 0 && text::parseUint(value.substr(4), n) && n <= 100000) {
            return saved(ctx, state, s_.setMaxRecording(static_cast<uint32_t>(n)));
        }
        if (value.compare(0, 5, "keep:") == 0 && text::parseUint(value.substr(5), n) && n <= 100000) {
            return saved(ctx, state, s_.setKeepDays(static_cast<uint32_t>(n)));
        }
        if (value == "qoff") return saved(ctx, state, s_.setQuietHours(0, 0));
        if (state.step == "quiet_start" && hourPicker::parse(value, "qs", minutes)) {
            state.set("qs", std::to_string(minutes));
            show(ctx, state, "quiet_end");
            return Outcome::Continue;
        }
        if (state.step == "quiet_end" && hourPicker::parse(value, "qe", minutes)) {
            int64_t start = 0;
            if (!text::parseInt(state.get("qs"), start) || start < 0 || start >= 24 * 60) {
                show(ctx, state, "quiet_start");
                return Outcome::Continue;
            }
            return saved(ctx, state, s_.setQuietHours(static_cast<uint16_t>(start), minutes));
        }
        if (state.step == "zone_pick" && timezonePicker::owns(value)) {
            const std::string zone = timezonePicker::onButton(ctx, value);
            return zone.empty() ? Outcome::Continue : zoneChosen(ctx, state, zone);
        }

        ctx.popup(kButtonGone);
        return Outcome::Continue;
    }

private:
    std::string child() const { return s_.storage_.settings.childName(); }

    // Shows a page: changes the message the button was on, or sends a new one
    // after something was typed (or when `fresh`).
    void show(Context& ctx, ConversationState& state, const std::string& page, const std::string& note = "",
              bool fresh = false) {
        state.step = page;
        InlineKeyboard keyboard;
        const std::string text = build(ctx, state, page, note, keyboard);
        if (ctx.isButtonPress() && !fresh) {
            ctx.editButtonMessage(text, &keyboard);
        } else {
            ctx.reply(text, &keyboard);
        }
    }

    // A change was made (or turned down): back to the main page, saying so.
    Outcome saved(Context& ctx, ConversationState& state, const std::string& note, bool fresh = false) {
        show(ctx, state, "main", note.empty() ? "Sorry, I couldn't save that." : note, fresh);
        return Outcome::Continue;
    }

    Outcome zoneChosen(Context& ctx, ConversationState& state, const std::string& zone) {
        // The picker has already changed its own message, so the menu comes back as a new one.
        return saved(ctx, state, s_.setTimezone(zone), true);
    }

    void askCountry(Context& ctx, ConversationState& state) {
        state.step = "zone_pick";
        // The box's own country and those of its contacts first: a family's
        // box usually moves between the same few places.
        std::vector<std::string> suggestions;
        std::set<std::string> seen;
        auto add = [&](const std::string& zoneName) {
            const TimeZoneInfo* zone = timezones::find(zoneName);
            if (zone && suggestions.size() < 6 && seen.insert(zone->country).second) suggestions.push_back(zone->country);
        };
        add(s_.storage_.settings.timezone());
        for (const auto& c : s_.storage_.people.contacts()) add(c.timezone);
        timezonePicker::ask(ctx, "Which country is the box in?", suggestions);
    }

    Outcome remove(Context& ctx, ConversationState& state) {
        const std::string kind = state.get("remove_kind");
        int64_t id = 0;
        text::parseInt(state.get("remove_id"), id);
        People& people = s_.storage_.people;
        const std::string box = child() + "'s box";

        if (kind == "rc") {
            Contact c;
            if (!people.findContact(static_cast<uint32_t>(id), c)) return saved(ctx, state, "They've already been removed.");
            if (!s_.storage_.removeContact(c.number)) return saved(ctx, state, "");
            ctx.log().info("An admin removed contact " + std::to_string(c.number));
            return saved(ctx, state, c.name + " has been removed from " + box + ".");
        }
        if (kind == "rm") {
            Person p;
            if (!find(people.monitors(), id, p)) return saved(ctx, state, "They've already been removed.");
            if (!people.removeMonitor(id)) return saved(ctx, state, "");
            ctx.log().info("An admin removed a monitor");
            return saved(ctx, state, personName(p) + " is no longer a monitor.");
        }
        if (kind == "ra") {
            Person p;
            if (!find(people.admins(), id, p)) return saved(ctx, state, "They've already been removed.");
            if (people.admins().size() <= 1) {
                return saved(ctx, state, box + " needs at least one admin, so the last one can't be removed.");
            }
            if (!people.removeAdmin(id)) return saved(ctx, state, "");
            ctx.log().info("An admin removed an admin");

            // Every admin is told, and so is the person removed.
            const std::string actor = ctx.sender().firstName.empty() ? "An admin" : ctx.sender().firstName;
            const bool self = id == ctx.sender().userId;
            const std::string news = self ? actor + " is no longer an admin of " + box + ". They removed themselves."
                                          : actor + " removed " + personName(p) + " as an admin of " + box + ".";
            for (const auto& admin : people.admins()) {
                if (admin.telegramId == ctx.sender().userId) continue;
                if (!ctx.telegram().sendMessage(admin.telegramId, news)) ctx.log().warning("Couldn't tell an admin");
            }
            if (self) {
                ctx.editButtonMessage("You're no longer an admin of " + box + ".");
                return Outcome::Finished;
            }
            if (!ctx.telegram().sendMessage(id, actor + " removed you as an admin of " + box + ".")) {
                ctx.log().warning("Couldn't tell someone they're no longer an admin");
            }
            return saved(ctx, state, personName(p) + " is no longer an admin.");
        }
        return saved(ctx, state, "");
    }

    static bool find(const std::vector<Person>& list, int64_t id, Person& out) {
        for (const auto& p : list) {
            if (p.telegramId == id) {
                out = p;
                return true;
            }
        }
        return false;
    }

    // ---- The pages ----

    std::string build(Context& ctx, ConversationState& state, const std::string& page, const std::string& note,
                      InlineKeyboard& k) {
        const DeviceSettings& settings = s_.storage_.settings;
        const People& people = s_.storage_.people;
        const InlineButton back = ctx.button(kBack, "go:main");

        if (page == "contacts") {
            const auto contacts = people.contacts();
            std::string text = "Contacts on " + child() + "'s box (" + std::to_string(contacts.size()) + " of " +
                               std::to_string(People::kMaxContacts) + "):";
            if (contacts.empty()) text += "\nNone yet.";
            for (const auto& c : contacts) {
                text += "\n" + c.name;
                const std::string zone = timezonePicker::describe(c.timezone);
                if (!zone.empty()) text += kEnDash + zone;
            }
            k.push_back({ctx.button("Invite a contact", "inv:c")});
            if (!contacts.empty()) k.push_back({ctx.button("Remove a contact", "go:remove_contact")});
            k.push_back({back});
            return text;
        }
        if (page == "remove_contact") {
            std::vector<InlineButton> buttons;
            for (const auto& c : people.contacts()) buttons.push_back(ctx.button(c.name, "rc:" + std::to_string(c.number)));
            k = inRows(buttons, 2);
            k.push_back({ctx.button(kBack, "go:contacts")});
            return buttons.empty() ? "There are no contacts to remove." : "Which contact do you want to remove?";
        }
        if (page == "monitors") {
            const auto monitors = people.monitors();
            std::string text = std::string("Monitoring: ") + monitorModeText(settings.monitorMode()) + ".\n\nMonitors (" +
                               std::to_string(monitors.size()) + " of " + std::to_string(People::kMaxMonitors) + "):";
            if (monitors.empty()) text += "\nNone yet.";
            for (const auto& m : monitors) text += "\n" + personName(m);
            k.push_back({ctx.button("Change what monitors get", "go:mode")});
            k.push_back({ctx.button("Invite a monitor", "inv:m")});
            if (!monitors.empty()) k.push_back({ctx.button("Remove a monitor", "go:remove_monitor")});
            k.push_back({back});
            return text;
        }
        if (page == "mode") {
            const MonitorMode current = settings.monitorMode();
            std::vector<InlineButton> buttons;
            for (int m = 0; m <= 3; ++m) {
                const MonitorMode mode = static_cast<MonitorMode>(m);
                buttons.push_back(ctx.button(std::string(mode == current ? kTick : "") + monitorModeText(mode),
                                             "mode:" + std::to_string(m)));
            }
            k = inRows(buttons, 2);
            k.push_back({ctx.button(kBack, "go:monitors")});
            return "Which voice notes should monitors get copies of?\n\nSent only: the ones " + child() +
                   " sends.\nReceived only: the ones " + child() + " receives.\nAll: both.";
        }
        if (page == "remove_monitor") {
            std::vector<InlineButton> buttons;
            for (const auto& m : people.monitors()) {
                buttons.push_back(ctx.button(personName(m), "rm:" + std::to_string(m.telegramId)));
            }
            k = inRows(buttons, 2);
            k.push_back({ctx.button(kBack, "go:monitors")});
            return buttons.empty() ? "There are no monitors to remove." : "Which monitor do you want to remove?";
        }
        if (page == "admins") {
            const auto admins = people.admins();
            std::string text = "Admins (" + std::to_string(admins.size()) + " of " + std::to_string(People::kMaxAdmins) + "):";
            for (const auto& a : admins) {
                text += "\n" + personName(a) + (a.telegramId == ctx.sender().userId ? " (you)" : "");
            }
            k.push_back({ctx.button("Invite an admin", "inv:a")});
            if (admins.size() > 1) k.push_back({ctx.button("Remove an admin", "go:remove_admin")});
            k.push_back({back});
            if (admins.size() <= 1) text += "\n\nThe box needs at least one admin, so the last one can't be removed.";
            return text;
        }
        if (page == "remove_admin") {
            std::vector<InlineButton> buttons;
            for (const auto& a : people.admins()) {
                const std::string label = personName(a) + (a.telegramId == ctx.sender().userId ? " (you)" : "");
                buttons.push_back(ctx.button(label, "ra:" + std::to_string(a.telegramId)));
            }
            k = inRows(buttons, 2);
            k.push_back({ctx.button(kBack, "go:admins")});
            return "Which admin do you want to remove?";
        }
        if (page == "confirm") {
            const std::string kind = state.get("remove_kind");
            int64_t id = 0;
            text::parseInt(state.get("remove_id"), id);
            std::string text, backTo;
            if (kind == "rc") {
                Contact c;
                people.findContact(static_cast<uint32_t>(id), c);
                text = "Remove " + c.name + " from " + child() + "'s box? Their voice notes on the box are deleted too. "
                       "Nothing changes in their Telegram app.";
                backTo = "go:contacts";
            } else {
                Person p;
                const bool admin = kind == "ra";
                find(admin ? people.admins() : people.monitors(), id, p);
                text = "Remove " + personName(p) + (id == ctx.sender().userId ? " (you)" : "") + " as " +
                       (admin ? "an admin" : "a monitor") + "?";
                backTo = admin ? "go:admins" : "go:monitors";
            }
            k.push_back({ctx.button("Yes, remove", "cfm"), ctx.button("No", backTo)});
            return text;
        }
        if (page == "quiet") {
            const std::string hours = quietHoursText(settings.quietStart(), settings.quietEnd());
            k.push_back({ctx.button("Change", "go:quiet_start")});
            if (hours != "off") k.back().push_back(ctx.button("Turn off", "qoff"));
            k.push_back({back});
            if (hours == "off") return "Quiet hours are off.";
            return "Quiet hours: " + hours + ". The screen, lights and sounds are off then. Voice notes still arrive.";
        }
        if (page == "quiet_start") {
            k = hourPicker::keyboard(ctx, "qs");
            k.push_back({ctx.button(kBack, "go:quiet")});
            return "When should quiet hours start?";
        }
        if (page == "quiet_end") {
            int64_t start = 0;
            text::parseInt(state.get("qs"), start);
            k = hourPicker::keyboard(ctx, "qe");
            k.push_back({ctx.button(kBack, "go:quiet")});
            return "Quiet hours start at " + formatTimeOfDay(static_cast<uint16_t>(start)) +
                   ". When should they end? Picking the same time turns quiet hours off.";
        }
        if (page == "zone") {
            const std::string zone = timezonePicker::describe(settings.timezone());
            std::string text;
            if (zone.empty()) {
                text = "The box's time zone hasn't been set yet. Until it is, quiet hours don't start.";
            } else {
                text = "Time zone: " + zone + ".";
                LocalTime now;
                if (deviceLocalTime(settings, ctx.clock(), now)) text += " The time there now is " + formatHourMinute(now) + ".";
            }
            k.push_back({ctx.button(zone.empty() ? "Set it" : "Change", "go:zone_pick"), back});
            return text;
        }
        if (page == "voice" || page == "alert") {
            const bool alert = page == "alert";
            const uint8_t current = alert ? settings.alertVolume() : settings.voiceVolume();
            std::vector<InlineButton> buttons;
            for (int v = alert ? 0 : kMinVoiceVolume; v <= 100; v += 10) {
                buttons.push_back(ctx.button(std::string(v == current ? kTick : "") + std::to_string(v) + "%",
                                             (alert ? "alv:" : "vol:") + std::to_string(v)));
            }
            k = inRows(buttons, alert ? 6 : 5);
            k.push_back({back});
            if (alert) {
                return "Chimes and alerts play at " + std::to_string(current) + "%. " + child() +
                       " can't change this on the box. Pick a new level:";
            }
            return "Voice notes play at " + std::to_string(current) + "%. Pick a new level:";
        }
        if (page == "maxrec") {
            const uint32_t current = settings.maxRecordingSeconds();
            std::vector<InlineButton> buttons;
            for (uint32_t s : kRecordingChoices) {
                buttons.push_back(ctx.button(std::string(s == current ? kTick : "") + recordingLabel(s), "rec:" + std::to_string(s)));
            }
            k = inRows(buttons, 4);
            k.push_back({back});
            return "The longest voice note " + child() + " can record is " + describeSeconds(current) +
                   ". Pick a new limit, or type a number of seconds from " +
                   std::to_string(DeviceSettings::kMinMaxRecordingSeconds) + " to " +
                   std::to_string(DeviceSettings::kMaxMaxRecordingSeconds) + ".";
        }
        if (page == "keep") {
            const uint32_t current = settings.keepDays();
            std::vector<InlineButton> buttons;
            for (uint32_t d : kKeepChoices) {
                buttons.push_back(ctx.button(std::string(d == current ? kTick : "") + keepLabel(d), "keep:" + std::to_string(d)));
            }
            k = inRows(buttons, 3);
            k.push_back({back});
            return "Voice notes are " + keepText(current) +
                   ". They stay in Telegram on family members' phones either way.\n\nPick how long to keep them, or type a "
                   "number of days from 0 to " + std::to_string(DeviceSettings::kMaxKeepDays) + " (0 keeps them for ever).";
        }
        if (page == "name") {
            k.push_back({back});
            return "The bot calls the child " + child() + " in its messages. Type a new name to change it.";
        }
        if (page == "status") {
            k.push_back({back, ctx.button("Done", "done")});
            return s_.statusText();
        }

        // The main page.
        state.step = "main";
        k = {
            {ctx.button("Contacts", "go:contacts"), ctx.button("Monitors", "go:monitors"), ctx.button("Admins", "go:admins")},
            {ctx.button("Quiet hours", "go:quiet"), ctx.button("Time zone", "go:zone")},
            {ctx.button("Voice volume", "go:voice"), ctx.button("Alert volume", "go:alert")},
            {ctx.button("Max recording", "go:maxrec"), ctx.button("Keep messages", "go:keep")},
            {ctx.button("Child's name", "go:name")},
            {ctx.button("Status", "go:status"), ctx.button("Last deleted", "trash")},
            {ctx.button("Done", "done")},
        };
        if (!note.empty()) return note + "\n\nAnything else?";
        return "Settings for " + child() + "'s box. What would you like to look at or change?";
    }

    AdminSettings& s_;
};

// ---------------------------------------------------------------------------
// Setting up

AdminSettings::AdminSettings(Bot& bot, Storage& storage, InviteFlows& invites, FileStorage& files, Network& network,
                             Power& power)
    : bot_(bot), storage_(storage), invites_(invites), files_(files), network_(network), power_(power),
      menu_(new Menu(*this)) {
    bot_.addConversation(*menu_);

    // Only /settings, /status and /trash are listed in /help, to keep it short.
    // The rest are shortcuts into the menu for people who prefer typing.
    bot_.addCommand("settings", Roles::kAdmin, "change the box's settings, and add or remove people",
                    [this](Context& ctx, const std::string&) { open(ctx, "main"); });
    bot_.addCommand("contacts", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string&) { open(ctx, "contacts"); });
    bot_.addCommand("admins", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string&) { open(ctx, "admins"); });
    bot_.addCommand("monitors", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string& arg) {
                        MonitorMode mode;
                        if (arg.empty()) return open(ctx, "monitors");
                        if (!parseMonitorMode(arg, mode)) {
                            ctx.reply("Send /monitors off, /monitors sent, /monitors received or /monitors all.");
                            return;
                        }
                        const std::string done = setMonitorMode(mode);
                        ctx.reply(done.empty() ? "Sorry, I couldn't save that." : done);
                    });
    bot_.addCommand("quiethours", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string& arg) {
                        if (arg.empty()) return open(ctx, "quiet");
                        std::string a = lower(arg);
                        for (char& c : a) {
                            if (c == '-') c = ' ';
                        }
                        std::vector<std::string> parts;
                        for (const auto& w : words(a)) {
                            if (w != "to") parts.push_back(w);
                        }
                        uint16_t start = 0, end = 0;
                        if (parts.size() == 1 && parts[0] == "off") {
                            start = end = 0;
                        } else if (parts.size() != 2 || !parseTimeOfDay(parts[0], start) || !parseTimeOfDay(parts[1], end)) {
                            ctx.reply("Send the start and end times, for example /quiethours 19:00 07:00. Send "
                                      "/quiethours off to turn them off.");
                            return;
                        }
                        const std::string done = setQuietHours(start, end);
                        ctx.reply(done.empty() ? "Sorry, I couldn't save that." : done);
                    });
    bot_.addCommand("timezone", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string& arg) { open(ctx, "zone", arg); });
    auto volumeCommand = [this](bool alert) {
        return [this, alert](Context& ctx, const std::string& arg) {
            if (arg.empty()) return open(ctx, alert ? "alert" : "voice");
            uint64_t n = 0;
            const std::string done = parseNumber(arg, n) && n <= 100 ? setVolume(alert, static_cast<uint8_t>(n)) : "";
            if (done.empty()) {
                ctx.reply(std::string("Send a volume from ") + (alert ? "0" : std::to_string(kMinVoiceVolume)) +
                          " to 100, for example /" + (alert ? "alertvolume" : "volume") + " 50.");
                return;
            }
            ctx.reply(done);
        };
    };
    bot_.addCommand("volume", Roles::kAdmin, "", volumeCommand(false));
    bot_.addCommand("alertvolume", Roles::kAdmin, "",
                    volumeCommand(true));
    bot_.addCommand("maxrecording", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string& arg) {
                        if (arg.empty()) return open(ctx, "maxrec");
                        uint64_t n = 0;
                        const std::string done =
                            parseNumber(arg, n) && n <= 100000 ? setMaxRecording(static_cast<uint32_t>(n)) : "";
                        ctx.reply(done.empty() ? "Send a number of seconds from 10 to 900, for example /maxrecording 360."
                                               : done);
                    });
    bot_.addCommand("keepmessages", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string& arg) {
                        if (arg.empty()) return open(ctx, "keep");
                        uint64_t n = 0;
                        const std::string done = parseNumber(arg, n) && n <= 100000 ? setKeepDays(static_cast<uint32_t>(n)) : "";
                        ctx.reply(done.empty() ? "Send a number of days from 0 to 3650, for example /keepmessages 60. "
                                                 "0 keeps them for ever."
                                               : done);
                    });
    bot_.addCommand("childname", Roles::kAdmin, "",
                    [this](Context& ctx, const std::string& arg) {
                        if (arg.empty()) return open(ctx, "name");
                        std::string problem;
                        const std::string done = setChildName(arg, problem);
                        ctx.reply(done.empty() ? problem : done);
                    });
    bot_.addCommand("status", Roles::kAdmin, "battery, Wi-Fi, storage and version",
                    [this](Context& ctx, const std::string&) { ctx.reply(statusText()); });
    bot_.addCommand("trash", Roles::kAdmin, "/trash getlast sends the most recently deleted recording",
                    [this](Context& ctx, const std::string& arg) {
                        if (lower(text::trim(arg)) == "getlast") {
                            sendLastDeleted(ctx);
                        } else {
                            ctx.reply("Send /trash getlast to get the most recently deleted recording.");
                        }
                    });
}

AdminSettings::~AdminSettings() = default;

void AdminSettings::open(Context& ctx, const std::string& page, const std::string& typed) {
    std::map<std::string, std::string> data = {{"open", page}};
    if (!typed.empty()) data["typed"] = typed;
    ctx.startConversation(kConversation, data);
}

// ---------------------------------------------------------------------------
// Changing settings

std::string AdminSettings::setQuietHours(uint16_t start, uint16_t end) {
    if (start == end) {
        start = end = 0;  // "the same start and end time means there are none"
    }
    if (!storage_.settings.setQuietHours(start, end)) return "";
    if (start == end) return "Quiet hours are now off.";
    return "Quiet hours are now " + quietHoursText(start, end) + ".";
}

std::string AdminSettings::setVolume(bool alert, uint8_t percent) {
    if (!alert && percent < kMinVoiceVolume) return "";
    const bool ok = alert ? storage_.settings.setAlertVolume(percent) : storage_.settings.setVoiceVolume(percent);
    if (!ok) return "";
    return std::string(alert ? "Alert volume" : "Voice volume") + " is now " + std::to_string(percent) + "%.";
}

std::string AdminSettings::setMaxRecording(uint32_t seconds) {
    if (!storage_.settings.setMaxRecordingSeconds(seconds)) return "";
    return "The longest voice note is now " + describeSeconds(seconds) + ".";
}

std::string AdminSettings::setKeepDays(uint32_t days) {
    if (!storage_.settings.setKeepDays(days)) return "";
    return "Voice notes are now " + keepText(days) + ".";
}

std::string AdminSettings::setMonitorMode(MonitorMode mode) {
    if (!storage_.settings.setMonitorMode(mode)) return "";
    switch (mode) {
        case MonitorMode::Off: return "Monitoring is now off.";
        case MonitorMode::SentOnly: return "Monitors now get copies of voice notes " + storage_.settings.childName() + " sends.";
        case MonitorMode::ReceivedOnly:
            return "Monitors now get copies of voice notes " + storage_.settings.childName() + " receives.";
        case MonitorMode::All: return "Monitors now get copies of all voice notes.";
    }
    return "";
}

std::string AdminSettings::setTimezone(const std::string& zone) {
    if (!storage_.settings.setTimezone(zone)) return "";
    return "The box is now on " + timezonePicker::describe(zone) + " time.";
}

std::string AdminSettings::setChildName(const std::string& typed, std::string& problem) {
    std::string name = typed;
    for (char& c : name) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    name = text::trim(name);
    if (name.empty()) {
        problem = "Please type the child's name.";
        return "";
    }
    if (name[0] == '/') {
        problem = "That looks like a command. Type the child's name, or send /cancel to stop.";
        return "";
    }
    if (characterCount(name) > kMaxChildNameLength) {
        problem = "That's a bit long. Please keep it to " + std::to_string(kMaxChildNameLength) + " letters or fewer.";
        return "";
    }
    if (!storage_.settings.setChildName(name)) {
        problem = "Sorry, I couldn't save that.";
        return "";
    }
    return "The bot now calls the child " + name + ".";
}

// ---------------------------------------------------------------------------
// /status and /trash getlast

std::string AdminSettings::statusText() const {
    std::string out = storage_.settings.childName() + "'s box\n";

    const int8_t battery = power_.batteryPercent();
    out += "\nBattery: ";
    if (battery < 0) {
        out += "can't be read";
    } else {
        out += std::to_string(battery) + "%";
        if (power_.isCharging()) out += ", charging";
        else if (power_.isPluggedIn()) out += ", plugged in";
        else out += ", on battery";
    }

    const int8_t signal = network_.signalStrength();
    out += "\nWi-Fi: ";
    if (!network_.isConnected()) {
        out += "not connected";
    } else if (signal == 0) {
        out += "connected, signal strength unknown";
    } else {
        const char* quality = signal >= -60 ? "strong" : signal >= -70 ? "good" : signal >= -80 ? "weak" : "very weak";
        out += std::string(quality) + " (" + std::to_string(signal) + " dBm)";
    }

    out += "\nStorage: ";
    const uint64_t total = files_.isMounted() ? files_.totalBytes() : 0;
    if (total == 0) {
        out += "SD card not found";
    } else {
        const uint64_t used = files_.usedBytes();
        const uint64_t mb = 1000 * 1000;
        out += thousands((used + mb / 2) / mb) + " MB of " + thousands(total / mb) + " MB used (" +
               std::to_string((used * 100 + total / 2) / total) + "%)";
    }

    out += "\nWaiting to send: " + std::to_string(storage_.outbox.size()) + " voice notes";
    out += "\nFirmware: " + firmwareVersion();
    return out;
}

void AdminSettings::sendLastDeleted(Context& ctx) {
    BinEntry entry;
    if (!storage_.bin.newest(entry)) {
        ctx.reply("There's nothing in the bin. Deleted recordings are kept for 30 days.");
        return;
    }

    Contact c;
    std::string caption = storage_.people.findContact(entry.contact, c)
                              ? "Recorded for " + c.name + "."
                              : "Recorded for a contact who has since been removed.";
    LocalTime when;
    const std::string zone = storage_.settings.timezone();
    if (entry.time < Bin::kEarliestRealTime) {
        caption += " Deleted before the box's clock was set.";
    } else {
        if (zone.empty() || !timezones::toLocal(zone, entry.time, when)) {
            when = PosixTimeZone().toLocal(entry.time);  // UTC until a rule is parsed
        }
        caption += " Deleted " + weekdayName(when.weekday) + " " + std::to_string(when.day) + " " +
                   monthName(when.month) + " at " + formatHourMinute(when) + (zone.empty() ? " UTC" : "") + ".";
    }

    // The bin doesn't record how long a recording is, so Telegram is left to
    // work it out (0 leaves the duration out).
    if (!ctx.telegram().sendVoiceFile(ctx.sender().chatId, entry.path(), 0, caption)) {
        ctx.log().warning("Couldn't send the last deleted recording");
        ctx.reply("Sorry, I couldn't send it. Try again in a minute.");
        return;
    }
    ctx.log().info("Sent an admin the last deleted recording");
}

}  // namespace fvb

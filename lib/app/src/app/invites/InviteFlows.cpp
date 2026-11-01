#include "app/invites/InviteFlows.h"

#include <cctype>
#include <set>
#include <vector>

#include "app/bot/Pickers.h"
#include "app/storage/Paths.h"
#include "app/storage/TextRecord.h"
#include "app/time/TimeZones.h"

namespace fvb {

using telegram::InlineButton;
using telegram::InlineKeyboard;
using telegram::Message;
using telegram::MessageKind;

namespace {
constexpr const char* kApprovePrefix = "approve";
const char* kUseButtons = "Tap one of the buttons above, or send /cancel to stop.";
const char* kButtonGone = "This button no longer works.";

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

// "Pat Example (@patexample)", so admins can tell who's asking.
std::string telegramName(const Sender& sender) {
    std::string name = sender.firstName;
    if (!sender.lastName.empty()) name += (name.empty() ? "" : " ") + sender.lastName;
    if (name.empty()) name = "Someone";
    if (!sender.username.empty()) name += " (@" + sender.username + ")";
    return name;
}

std::string sleepHours(uint16_t start, uint16_t end) {
    if (start == end) return "none";
    return formatTimeOfDay(start) + " to " + formatTimeOfDay(end);
}

// The name to use for someone in a request: what the child calls a contact,
// or the Telegram name of a monitor or admin.
std::string requestLabel(const PendingApproval& p) {
    return p.role == InviteRole::Contact ? p.details.name : p.telegramName;
}
}  // namespace

// ---------------------------------------------------------------------------
// Contact setup, and /me
//
// Steps: name, country, sleep, sleep_start, sleep_end, photo, notice (setup
// only) and menu (/me only). The answers so far are kept in the state under
// name, zone, ss, se (sleep start and end, minutes after midnight), photo and
// profile (their Telegram profile picture, if they have one).

class InviteFlows::ContactDetails : public ConversationType {
public:
    ContactDetails(InviteFlows& flows, bool setup) : flows_(flows), setup_(setup) {}

    const char* name() const override { return setup_ ? kContactSetup : kMe; }
    // Anyone can do setup (they're not on a list until approved). /me is for contacts.
    bool allows(const Roles& roles) const override { return setup_ || roles.isContact(); }

    Outcome begin(Context& ctx, ConversationState& state) override {
        if (setup_) {
            state.set("ss", std::to_string(Contact::kDefaultSleepStart));
            state.set("se", std::to_string(Contact::kDefaultSleepEnd));
            ctx.reply("Hello! You've been invited to send voice notes to " + child() +
                      ". Setting up takes about a minute.");
            askName(ctx, state);
            return Outcome::Continue;
        }
        Contact c;
        if (!flows_.storage_.people.findContactByTelegramId(ctx.sender().userId, c)) return Outcome::Finished;
        state.set("name", c.name);
        state.set("zone", c.timezone);
        state.set("ss", std::to_string(c.sleepStart));
        state.set("se", std::to_string(c.sleepEnd));
        showMenu(ctx, state, describe(c) + "\n\nWhat would you like to change?");
        return Outcome::Continue;
    }

    Outcome onMessage(Context& ctx, ConversationState& state, const Message& message) override {
        if (state.step == "name") return onName(ctx, state, message);
        if (state.step == "country") {
            if (message.kind != MessageKind::Text) {
                ctx.reply("Tap your country above, or type its name.");
                return Outcome::Continue;
            }
            const std::string zone = timezonePicker::onText(ctx, message.text);
            return zone.empty() ? Outcome::Continue : zoneChosen(ctx, state, zone);
        }
        if (state.step == "photo") return onPhoto(ctx, state, message);
        ctx.reply(kUseButtons);
        return Outcome::Continue;
    }

    Outcome onButton(Context& ctx, ConversationState& state, const std::string& value) override {
        uint16_t minutes = 0;
        if (state.step == "country" && timezonePicker::owns(value)) {
            const std::string zone = timezonePicker::onButton(ctx, value);
            return zone.empty() ? Outcome::Continue : zoneChosen(ctx, state, zone);
        }
        if (state.step == "sleep" && value == "sleep:keep") {
            ctx.editButtonMessage("Sleep hours: " + currentSleep(state));
            return finishedStep(ctx, state, "sleep");
        }
        if (state.step == "sleep" && value == "sleep:change") {
            state.step = "sleep_start";
            const InlineKeyboard keyboard = hourPicker::keyboard(ctx, "bed");
            ctx.editButtonMessage("What time do you usually go to sleep?", &keyboard);
            return Outcome::Continue;
        }
        if (state.step == "sleep_start" && hourPicker::parse(value, "bed", minutes)) {
            state.set("ss", std::to_string(minutes));
            state.step = "sleep_end";
            const InlineKeyboard keyboard = hourPicker::keyboard(ctx, "wake");
            ctx.editButtonMessage("Bedtime: " + formatTimeOfDay(minutes) + ". What time do you usually wake up?",
                                  &keyboard);
            return Outcome::Continue;
        }
        if (state.step == "sleep_end" && hourPicker::parse(value, "wake", minutes)) {
            state.set("se", std::to_string(minutes));
            ctx.editButtonMessage("Sleep hours: " + currentSleep(state));
            return finishedStep(ctx, state, "sleep");
        }
        if (state.step == "photo" && value == "photo:profile" && !state.get("profile").empty()) {
            state.set("photo", state.get("profile"));
            ctx.editButtonMessage("Photo: your Telegram profile picture.");
            return finishedStep(ctx, state, "photo");
        }
        // Setup needs a photo, so only /me has a way to carry on without sending one.
        if (!setup_ && state.step == "photo" && value == "photo:none") {
            state.set("photo", "");
            ctx.editButtonMessage("Photo: kept as it was.");
            return finishedStep(ctx, state, "photo");
        }
        if (setup_ && state.step == "notice" && value == "notice:ok") {
            ctx.editButtonMessage(noticeText() + "\n\n\xE2\x9C\x93 Understood");  // ✓
            return submit(ctx, state);
        }
        if (!setup_ && state.step == "menu") return onMenu(ctx, state, value);

        ctx.popup(kButtonGone);
        return Outcome::Continue;
    }

    void onTimedOut(const ConversationState& state, int64_t now) override {
        if (setup_) flows_.setupTimedOut(state, now);
    }

private:
    std::string child() const { return flows_.childName(); }

    std::string currentSleep(const ConversationState& state) const {
        return sleepHours(sleepStart(state), sleepEnd(state));
    }
    static uint16_t minutesIn(const ConversationState& state, const char* key, uint16_t fallback) {
        int64_t v = 0;
        if (!text::parseInt(state.get(key), v) || v < 0 || v >= 24 * 60) return fallback;
        return static_cast<uint16_t>(v);
    }
    static uint16_t sleepStart(const ConversationState& s) { return minutesIn(s, "ss", Contact::kDefaultSleepStart); }
    static uint16_t sleepEnd(const ConversationState& s) { return minutesIn(s, "se", Contact::kDefaultSleepEnd); }

    std::string describe(const Contact& c) const {
        const bool hasPhoto = flows_.storage_.hasContactPhoto(c.number);
        return "Name: " + c.name + "\nTime zone: " + timezonePicker::describe(c.timezone) +
               "\nSleep hours: " + sleepHours(c.sleepStart, c.sleepEnd) + "\nPhoto: " + (hasPhoto ? "yes" : "none");
    }

    // ---- Name ----

    void askName(Context& ctx, ConversationState& state) {
        state.step = "name";
        ctx.reply("What does " + child() + " call you? For example Granny or Uncle Sam. Type it below.");
    }

    Outcome onName(Context& ctx, ConversationState& state, const Message& message) {
        if (message.kind != MessageKind::Text) {
            ctx.reply("Please type the name " + child() + " calls you.");
            return Outcome::Continue;
        }
        std::string name = message.text;
        for (char& c : name) {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        }
        name = text::trim(name);
        if (name.empty()) {
            ctx.reply("Please type the name " + child() + " calls you.");
            return Outcome::Continue;
        }
        if (name[0] == '/') {
            ctx.reply("That looks like a command. Type the name " + child() +
                      " calls you, or send /cancel to stop.");
            return Outcome::Continue;
        }
        if (characterCount(name) > kMaxNameLength) {
            ctx.reply("That's a bit long. Please keep it to " + std::to_string(kMaxNameLength) + " letters or fewer.");
            return Outcome::Continue;
        }
        // Two contacts with the same name would be confusing on the box.
        for (const auto& c : flows_.storage_.people.contacts()) {
            if (c.telegramId != ctx.sender().userId && lower(c.name) == lower(name)) {
                ctx.reply("Someone on " + child() + "'s box is already called " + c.name +
                          ". Please choose a different name.");
                return Outcome::Continue;
            }
        }
        state.set("name", name);
        return finishedStep(ctx, state, "name");
    }

    // ---- Country and time zone ----

    void askCountry(Context& ctx, ConversationState& state) {
        state.step = "country";
        // Offer the box's own country and those of other contacts first: family
        // often live in the same few places.
        std::vector<std::string> suggestions;
        const TimeZoneInfo* own = timezones::find(flows_.storage_.settings.timezone());
        if (own) suggestions.push_back(own->country);
        for (const auto& c : flows_.storage_.people.contacts()) {
            if (const TimeZoneInfo* zone = timezones::find(c.timezone)) suggestions.push_back(zone->country);
        }
        std::vector<std::string> unique;
        std::set<std::string> seen;
        for (const auto& code : suggestions) {
            if (unique.size() < 6 && seen.insert(code).second) unique.push_back(code);
        }
        timezonePicker::ask(ctx, "Which country do you live in?", unique);
    }

    Outcome zoneChosen(Context& ctx, ConversationState& state, const std::string& zone) {
        state.set("zone", zone);
        return finishedStep(ctx, state, "country");
    }

    // ---- Sleep hours ----

    void askSleep(Context& ctx, ConversationState& state) {
        state.step = "sleep";
        InlineKeyboard keyboard = {{ctx.button("Keep these", "sleep:keep"), ctx.button("Change", "sleep:change")}};
        ctx.reply("Your sleep hours are " + currentSleep(state) + ", in your own time. " + child() +
                      "'s box shows you as asleep then. " + child() + " can still send you voice notes.",
                  &keyboard);
    }

    // ---- Photo ----

    void askPhoto(Context& ctx, ConversationState& state) {
        state.step = "photo";
        state.set("profile", "");
        telegram::PhotoSize profile;
        bool hasProfile = false;
        if (!ctx.telegram().getProfilePhoto(ctx.sender().userId, profile, hasProfile, kPhotoWidth)) {
            ctx.log().warning("Couldn't look up a profile picture");
            hasProfile = false;
        }
        InlineKeyboard keyboard;
        // The child can't read, so the photo is how they tell people apart.
        std::string question = "Now a photo of you for " + child() + "'s box, so " + child() +
                               " can see who they're talking to. ";
        if (hasProfile) {
            state.set("profile", profile.fileId);
            keyboard.push_back({ctx.button("Use my profile picture", "photo:profile")});
            question += "You can use your Telegram profile picture, or send me a different photo.";
        } else {
            question += "Send me a photo of yourself.";
        }
        if (!setup_) {
            keyboard.push_back(
                {ctx.button(hasExistingPhoto(ctx) ? "Keep my current photo" : "Not now", "photo:none")});
        }
        ctx.reply(question, keyboard.empty() ? nullptr : &keyboard);
    }

    bool hasExistingPhoto(Context& ctx) const {
        Contact c;
        return flows_.storage_.people.findContactByTelegramId(ctx.sender().userId, c) &&
               flows_.storage_.hasContactPhoto(c.number);
    }

    Outcome onPhoto(Context& ctx, ConversationState& state, const Message& message) {
        if (message.kind == MessageKind::PhotoAsFile) {
            ctx.reply("That came through as a file. Please send it again as a normal photo, not as a file.");
            return Outcome::Continue;
        }
        if (message.kind != MessageKind::Photo) {
            const bool hasButtons = !setup_ || !state.get("profile").empty();
            ctx.reply(hasButtons ? "Send me a photo, or tap the button above." : "Please send me a photo of yourself.");
            return Outcome::Continue;
        }
        const telegram::PhotoSize* best = telegram::closestToWidth(message.photos, kPhotoWidth);
        if (!best) {
            ctx.reply("I couldn't read that photo. Please try another one.");
            return Outcome::Continue;
        }
        state.set("photo", best->fileId);
        ctx.reply("Thanks, got it.");
        return finishedStep(ctx, state, "photo");
    }

    // ---- The notice, and finishing ----

    std::string noticeText() const {
        return "One last thing: " + child() + "'s parents may be able to hear all voice notes to and from you.";
    }

    void askNotice(Context& ctx, ConversationState& state) {
        state.step = "notice";
        InlineKeyboard keyboard = {{ctx.button("I understand", "notice:ok")}};
        ctx.reply(noticeText(), &keyboard);
    }

    Outcome submit(Context& ctx, ConversationState& state) {
        PendingApproval request;
        request.role = InviteRole::Contact;
        request.userId = ctx.sender().userId;
        request.telegramName = telegramName(ctx.sender());
        request.createdAt = ctx.now();
        request.details.telegramId = ctx.sender().userId;
        request.details.name = state.get("name");
        request.details.timezone = state.get("zone");
        request.details.sleepStart = sleepStart(state);
        request.details.sleepEnd = sleepEnd(state);
        request.photoFileId = state.get("photo");
        if (!flows_.submit(ctx, request, state.get("invite"))) {
            ctx.reply("Sorry, I couldn't save your details. Please tap \"I understand\" again in a minute.");
            return Outcome::Continue;
        }
        return Outcome::Finished;
    }

    // ---- Moving on ----

    // A step has its answer. Setup asks the next question; /me saves the change
    // and shows the menu again.
    Outcome finishedStep(Context& ctx, ConversationState& state, const std::string& step) {
        if (setup_) {
            if (step == "name") askCountry(ctx, state);
            if (step == "country") askSleep(ctx, state);
            if (step == "sleep") askPhoto(ctx, state);
            if (step == "photo") askNotice(ctx, state);
            return Outcome::Continue;
        }
        const bool saved = saveChange(ctx, state, step);
        showMenu(ctx, state, saved ? "Saved. Anything else?" : "Sorry, I couldn't save that. Anything else?");
        return Outcome::Continue;
    }

    bool saveChange(Context& ctx, ConversationState& state, const std::string& step) {
        Contact c;
        auto& people = flows_.storage_.people;
        if (!people.findContactByTelegramId(ctx.sender().userId, c)) return false;
        if (step == "name") c.name = state.get("name");
        if (step == "country") c.timezone = state.get("zone");
        if (step == "sleep") {
            c.sleepStart = sleepStart(state);
            c.sleepEnd = sleepEnd(state);
        }
        if (step == "photo") {
            const std::string fileId = state.get("photo");
            if (fileId.empty()) return true;  // kept the one they had
            if (!ctx.telegram().downloadFile(fileId, paths::contactPhoto(c.number))) {
                ctx.log().warning("Couldn't download a contact's new photo");
                return false;
            }
            ctx.log().info("Contact " + std::to_string(c.number) + " changed their photo");
            return true;
        }
        if (!people.updateContact(c)) return false;
        ctx.log().info("Contact " + std::to_string(c.number) + " changed their " + step);
        return true;
    }

    void showMenu(Context& ctx, ConversationState& state, const std::string& text) {
        state.step = "menu";
        InlineKeyboard keyboard = {
            {ctx.button("Name", "me:name"), ctx.button("Time zone", "me:zone")},
            {ctx.button("Sleep hours", "me:sleep"), ctx.button("Photo", "me:photo")},
            {ctx.button("Done", "me:done")},
        };
        ctx.reply(text, &keyboard);
    }

    Outcome onMenu(Context& ctx, ConversationState& state, const std::string& value) {
        if (value == "me:done") {
            ctx.editButtonMessage("All done.");
            return Outcome::Finished;
        }
        if (value == "me:name") {
            ctx.editButtonMessage("Changing your name.");
            askName(ctx, state);
        } else if (value == "me:zone") {
            ctx.editButtonMessage("Changing your time zone.");
            askCountry(ctx, state);
        } else if (value == "me:sleep") {
            ctx.editButtonMessage("Changing your sleep hours.");
            askSleep(ctx, state);
        } else if (value == "me:photo") {
            ctx.editButtonMessage("Changing your photo.");
            askPhoto(ctx, state);
        } else {
            ctx.popup(kButtonGone);
        }
        return Outcome::Continue;
    }

    InviteFlows& flows_;
    bool setup_;
};

// ---------------------------------------------------------------------------
// Monitor and admin setup: explain the role, then Join or No thanks.

class InviteFlows::JoinRole : public ConversationType {
public:
    JoinRole(InviteFlows& flows, InviteRole role) : flows_(flows), role_(role) {}

    const char* name() const override { return role_ == InviteRole::Admin ? kAdminSetup : kMonitorSetup; }
    bool allows(const Roles&) const override { return true; }

    Outcome begin(Context& ctx, ConversationState& state) override {
        state.step = "join";
        InlineKeyboard keyboard = {{ctx.button("Join", "join:yes"), ctx.button("No thanks", "join:no")}};
        ctx.reply(intro(), &keyboard);
        return Outcome::Continue;
    }

    Outcome onMessage(Context& ctx, ConversationState&, const Message&) override {
        ctx.reply("Tap Join to carry on, or No thanks to stop.");
        return Outcome::Continue;
    }

    Outcome onButton(Context& ctx, ConversationState& state, const std::string& value) override {
        if (value == "join:no") {
            ctx.editButtonMessage("No problem. Nothing has been set up.");
            flows_.invites_.remove(state.get("invite"));
            return Outcome::Finished;
        }
        if (value != "join:yes") {
            ctx.popup(kButtonGone);
            return Outcome::Continue;
        }
        ctx.editButtonMessage(intro());
        PendingApproval request;
        request.role = role_;
        request.userId = ctx.sender().userId;
        request.telegramName = telegramName(ctx.sender());
        request.createdAt = ctx.now();
        request.details.name = ctx.sender().firstName;
        if (!flows_.submit(ctx, request, state.get("invite"))) {
            InlineKeyboard keyboard = {{ctx.button("Join", "join:yes")}};
            ctx.reply("Sorry, I couldn't save that. Please tap Join again in a minute.", &keyboard);
            return Outcome::Continue;
        }
        return Outcome::Finished;
    }

    void onTimedOut(const ConversationState& state, int64_t now) override { flows_.setupTimedOut(state, now); }

private:
    std::string intro() const {
        const std::string child = flows_.childName();
        if (role_ == InviteRole::Admin) {
            return "Hello! You've been invited to be an admin of " + child +
                   "'s voice box. Admins can invite and approve family members, change the box's settings, "
                   "install updates and get alerts about the box, such as low battery.";
        }
        return "Hello! You've been invited to be a monitor on " + child +
               "'s voice box. When monitoring is switched on, you get a copy of voice notes " + child +
               " sends or receives, labelled with who they're from and to.";
    }

    InviteFlows& flows_;
    InviteRole role_;
};

// ---------------------------------------------------------------------------
// Setting up

InviteFlows::InviteFlows(Bot& bot, Storage& storage, SettingsStore& store, Random& random)
    : bot_(bot),
      storage_(storage),
      random_(random),
      invites_(store),
      pending_(store),
      contactSetup_(new ContactDetails(*this, true)),
      me_(new ContactDetails(*this, false)),
      monitorSetup_(new JoinRole(*this, InviteRole::Monitor)),
      adminSetup_(new JoinRole(*this, InviteRole::Admin)) {
    bot_.addConversation(*contactSetup_);
    bot_.addConversation(*me_);
    bot_.addConversation(*monitorSetup_);
    bot_.addConversation(*adminSetup_);

    bot_.addCommand("addcontact", Roles::kAdmin, "invite a family member to send voice notes",
                    [this](Context& ctx, const std::string&) { makeInvite(ctx, InviteRole::Contact); });
    bot_.addCommand("addmonitor", Roles::kAdmin, "invite someone to get copies of voice notes",
                    [this](Context& ctx, const std::string&) { makeInvite(ctx, InviteRole::Monitor); });
    bot_.addCommand("addadmin", Roles::kAdmin, "invite another admin",
                    [this](Context& ctx, const std::string&) { makeInvite(ctx, InviteRole::Admin); });
    bot_.addCommand("me", Roles::kContact, "change your name, time zone, sleep hours or photo",
                    [](Context& ctx, const std::string&) { ctx.startConversation(kMe); });

    bot_.addButtons(kApprovePrefix, Roles::kAdmin,
                    [this](Context& ctx, const std::string& data) { onApproveButton(ctx, data); });
    bot_.setStartLinkHandler([this](Context& ctx, const std::string& code) { return onStartLink(ctx, code); });
    bot_.setStrangerHandler([this](Context& ctx) { return onStranger(ctx); });
}

InviteFlows::~InviteFlows() = default;

std::string InviteFlows::childName() const {
    const std::string name = storage_.settings.childName();
    return name.empty() ? "the child" : name;
}

void InviteFlows::tidy(int64_t now) {
    invites_.tidy(now, [this](const Invite& invite) {
        ConversationState state;
        return bot_.conversations().find(invite.userId, state) && state.get("invite") == invite.code;
    });
}

// ---------------------------------------------------------------------------
// Making invite links

std::string InviteFlows::newCode() {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string code;
    while (code.size() < kCodeLength) {
        uint32_t bits = random_.next();
        for (int i = 0; i < 4 && code.size() < kCodeLength; ++i, bits >>= 8) {
            const uint32_t byte = bits & 0xFF;
            if (byte < 62 * 4) code += kAlphabet[byte % 62];  // skip the rest so every letter is equally likely
        }
    }
    return code;
}

void InviteFlows::makeInvite(Context& ctx, InviteRole role) {
    const std::string child = childName();
    const People& people = storage_.people;
    if (role == InviteRole::Contact && people.contacts().size() >= People::kMaxContacts) {
        ctx.reply(child + "'s box already has " + std::to_string(People::kMaxContacts) +
                  " contacts, which is the most it can hold. Remove one before inviting someone new.");
        return;
    }
    if (role == InviteRole::Monitor && people.monitors().size() >= People::kMaxMonitors) {
        ctx.reply("There are already " + std::to_string(People::kMaxMonitors) +
                  " monitors, which is the most the box can hold. Remove one before inviting someone new.");
        return;
    }
    if (role == InviteRole::Admin && people.admins().size() >= People::kMaxAdmins) {
        ctx.reply("There are already " + std::to_string(People::kMaxAdmins) +
                  " admins, which is the most the box can hold. Remove one before inviting someone new.");
        return;
    }

    const int64_t now = ctx.now();
    tidy(now);
    Invite invite;
    invite.code = newCode();
    invite.role = role;
    invite.createdAt = now;
    invite.createdBy = ctx.sender().userId;

    const std::string link = ctx.telegram().inviteLink(invite.code);
    if (link.empty()) {
        ctx.reply("Sorry, I can't make invite links until I've finished connecting to Telegram. Try again in a minute.");
        return;
    }
    if (!invites_.save(invite)) {
        ctx.reply("There are too many invite links waiting to be used. Unused links run out after 48 hours; "
                  "try again after that.");
        return;
    }
    ctx.log().info(std::string("Made a ") + toString(role) + " invite link");

    std::string who, forwarded;
    switch (role) {
        case InviteRole::Contact:
            who = "a new contact";
            forwarded = "You're invited to send voice notes to " + child + ". Tap this link to get started:\n" + link;
            break;
        case InviteRole::Monitor:
            who = "a new monitor";
            forwarded = "You're invited to get copies of " + child + "'s voice notes. Tap this link to set up:\n" + link;
            break;
        case InviteRole::Admin:
            who = "a new admin";
            forwarded = "You're invited to help look after " + child + "'s voice box. Tap this link to set up:\n" + link;
            break;
    }
    ctx.reply("Here's an invite for " + who +
              ". Forward the next message to them however you like. The link works once and runs out in 48 "
              "hours. Once they've set up, every admin will be asked to approve them.");
    ctx.reply(forwarded);
}

// ---------------------------------------------------------------------------
// Opening a link, and people who come back later

bool InviteFlows::onStartLink(Context& ctx, const std::string& code) {
    if (!telegram::TelegramClient::isValidStartCode(code)) return false;
    const int64_t now = ctx.now();
    tidy(now);

    Invite invite;
    if (!invites_.find(code, invite)) return false;  // not one of ours: strangers stay ignored
    const int64_t userId = ctx.sender().userId;
    const std::string child = childName();

    if (invite.userId != 0 && invite.userId != userId) {
        ctx.reply("This invite link has already been used. Ask " + child + "'s parents for a new one.");
        return true;
    }
    if (InviteStore::isExpired(invite, now)) {
        ctx.reply("This invite link has run out. Ask " + child + "'s parents for a new one.");
        if (invite.userId == userId) invites_.remove(code);
        return true;
    }

    const Roles roles = ctx.sender().roles;
    if ((invite.role == InviteRole::Contact && roles.isContact()) ||
        (invite.role == InviteRole::Monitor && roles.isMonitor()) ||
        (invite.role == InviteRole::Admin && roles.isAdmin())) {
        // Left unused, so it can still go to whoever it was meant for.
        ctx.reply(std::string("You're already ") + (invite.role == InviteRole::Admin ? "an " : "a ") +
                  toString(invite.role) + " on " + child + "'s box, so this link isn't for you.");
        return true;
    }

    invite.userId = userId;
    invite.timedOutAt = 0;
    if (!invites_.save(invite)) {
        ctx.log().warning("Couldn't save an invite link that was opened");
        return true;
    }
    ctx.log().info(std::string("Someone opened a ") + toString(invite.role) + " invite link");
    const char* type = invite.role == InviteRole::Contact   ? kContactSetup
                       : invite.role == InviteRole::Monitor ? kMonitorSetup
                                                            : kAdminSetup;
    ctx.startConversation(type, {{"invite", code}});
    return true;
}

bool InviteFlows::onStranger(Context& ctx) {
    const int64_t userId = ctx.sender().userId;
    const int64_t now = ctx.now();
    const std::string child = childName();

    PendingApproval request;
    if (pending_.findByUser(userId, request)) {
        ctx.reply("Thanks, you're all set up. " + child +
                  "'s parents just need to approve you. I'll message you as soon as they have.");
        return true;
    }

    tidy(now);
    Invite invite;
    if (!invites_.findByUser(userId, invite)) {
        // A contact an admin removed. Kept neutral: it doesn't say they were
        // removed, or by whom, and it's sent at most once a day.
        // The rest of the time they're treated like any stranger.
        if (storage_.people.shouldTellRemoved(userId, now)) {
            ctx.reply("This box isn't taking voice notes from you at the moment.");
            ctx.log().info("Told a removed contact the box isn't taking their voice notes");
            return true;
        }
        return false;  // nothing to do with us: stay silent
    }

    if (invite.timedOutAt > 0) {
        ctx.reply("You didn't finish setting up, so I stopped after a week with no reply. Ask " + child +
                  "'s parents for a new invite link to start again.");
        invites_.remove(invite.code);
    } else if (InviteStore::isExpired(invite, now)) {
        ctx.reply("You didn't finish setting up, and your invite link has run out. Ask " + child +
                  "'s parents for a new one.");
        invites_.remove(invite.code);
    } else {
        ctx.reply("To start setting up again, tap your invite link.");
    }
    return true;
}

void InviteFlows::setupTimedOut(const ConversationState& state, int64_t now) {
    Invite invite;
    if (!invites_.find(state.get("invite"), invite) || invite.userId != state.userId) return;
    invite.timedOutAt = now;
    invites_.save(invite);
}

// ---------------------------------------------------------------------------
// Approval

bool InviteFlows::submit(Context& ctx, PendingApproval request, const std::string& inviteCode) {
    const uint32_t id = pending_.save(request);
    if (id == 0) {
        ctx.log().warning("Couldn't save a request for approval");
        return false;
    }
    request.id = id;
    invites_.remove(inviteCode);

    const std::string child = childName();
    const std::string who = request.role == InviteRole::Contact ? request.details.name : request.telegramName;
    ctx.reply("Thanks" + (request.role == InviteRole::Contact ? ", " + who : std::string()) + "! " + child +
              "'s parents now need to approve you. I'll message you as soon as they have.");

    std::string text;
    switch (request.role) {
        case InviteRole::Contact:
            text = who + " has finished setting up. Approve?\n\nTelegram name: " + request.telegramName +
                   "\nTime zone: " + timezonePicker::describe(request.details.timezone) +
                   "\nSleep hours: " + sleepHours(request.details.sleepStart, request.details.sleepEnd);
            break;
        case InviteRole::Monitor:
            text = who + " wants to be a monitor and get copies of " + child + "'s voice notes. Approve?";
            break;
        case InviteRole::Admin:
            text = who + " wants to be an admin of " + child + "'s box. Approve?";
            break;
    }
    const std::string base = std::string(kApprovePrefix) + ":" + std::to_string(id) + ":";
    const InlineKeyboard keyboard = {{InlineButton{"Yes", base + "y"}, InlineButton{"No", base + "n"}}};

    for (const auto& admin : storage_.people.admins()) {
        Message sent;
        if (ctx.telegram().sendMessage(admin.telegramId, text, &keyboard, &sent)) {
            request.adminMessages.emplace_back(admin.telegramId, sent.messageId);
        } else {
            ctx.log().warning("Couldn't ask an admin to approve someone");
        }
    }
    pending_.save(request);
    ctx.log().info(std::string("A new ") + toString(request.role) + " finished setting up and is waiting for approval");
    return true;
}

void InviteFlows::onApproveButton(Context& ctx, const std::string& data) {
    // data is "<id>:y" or "<id>:n"
    const size_t colon = data.find(':');
    uint64_t id = 0;
    const std::string answer = colon == std::string::npos ? "" : data.substr(colon + 1);
    PendingApproval request;
    if (colon == std::string::npos || !text::parseUint(data.substr(0, colon), id) || (answer != "y" && answer != "n") ||
        !pending_.find(static_cast<uint32_t>(id), request)) {
        ctx.popup("This has already been answered.");
        ctx.editButtonMessage("This has already been answered.");
        return;
    }

    const std::string child = childName();
    const std::string who = requestLabel(request);
    const std::string approver = ctx.sender().firstName.empty() ? "An admin" : ctx.sender().firstName;
    People& people = storage_.people;
    const bool yes = answer == "y";

    if (yes) {
        bool added = false;
        std::string welcome;
        switch (request.role) {
            case InviteRole::Contact: {
                Contact existing;
                if (people.findContactByTelegramId(request.userId, existing)) {
                    added = true;  // already approved some other way
                    break;
                }
                if (people.contacts().size() >= People::kMaxContacts) {
                    ctx.popup("The box already has the most contacts it can hold. Remove one first.");
                    return;
                }
                Contact c = request.details;
                c.telegramId = request.userId;
                const uint32_t number = people.addContact(c);
                added = number != 0;
                if (added && !request.photoFileId.empty() &&
                    !ctx.telegram().downloadFile(request.photoFileId, paths::contactPhoto(number))) {
                    ctx.log().warning("Couldn't download a new contact's photo");
                    ctx.reply("I couldn't download " + who + "'s photo. They can send it again with /me.");
                }
                welcome = "You're all set. Send " + child +
                          " a voice note any time.\n\nTo change your name, time zone, sleep hours or photo, send /me.";
                break;
            }
            case InviteRole::Monitor:
                if (!people.isMonitor(request.userId) && people.monitors().size() >= People::kMaxMonitors) {
                    ctx.popup("The box already has the most monitors it can hold. Remove one first.");
                    return;
                }
                added = people.addMonitor(Person{request.userId, request.details.name});
                welcome = "You're all set. While monitoring is switched on, you'll get copies of " + child +
                          "'s voice notes.";
                if (storage_.settings.monitorMode() == MonitorMode::Off) welcome += " It's switched off at the moment.";
                break;
            case InviteRole::Admin:
                if (!people.isAdmin(request.userId) && people.admins().size() >= People::kMaxAdmins) {
                    ctx.popup("The box already has the most admins it can hold. Remove one first.");
                    return;
                }
                added = people.addAdmin(Person{request.userId, request.details.name});
                welcome = "You're now an admin of " + child + "'s box. Send /help to see what you can do.";
                break;
        }
        if (!added) {
            ctx.popup("Sorry, I couldn't save that. Try again in a minute.");
            return;
        }
        if (!ctx.telegram().sendMessage(request.userId, welcome)) {
            ctx.log().warning("Couldn't tell someone they'd been approved");
        }
    } else if (!ctx.telegram().sendMessage(request.userId, "Sorry, " + child + "'s parents didn't approve this.")) {
        ctx.log().warning("Couldn't tell someone they weren't approved");
    }

    pending_.remove(request.id);
    ctx.log().info(std::string(yes ? "Approved" : "Turned down") + " a new " + toString(request.role));

    // Change every admin's "Approve?" message so nobody answers it twice. For a
    // new admin this is also how the other admins are told.
    std::string outcome;
    if (yes && request.role == InviteRole::Admin) {
        outcome = who + " is now an admin, approved by ";
    } else {
        outcome = who + (yes ? " was approved by " : " was turned down by ");
    }
    ctx.editButtonMessage(outcome + "you.");
    ctx.popup(yes ? "Approved" : "Turned down");
    for (const auto& m : request.adminMessages) {
        if (m.first == ctx.sender().userId) continue;
        if (!ctx.telegram().editMessageText(m.first, m.second, outcome + approver + ".")) {
            ctx.log().warning("Couldn't update another admin's approval message");
        }
    }
}

}  // namespace fvb

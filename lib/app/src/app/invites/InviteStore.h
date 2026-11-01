#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hal/SettingsStore.h"

namespace fvb {

// What an invite link lets someone become. Saved as a number, so never change these.
enum class InviteRole : uint8_t { Contact = 1, Monitor = 2, Admin = 3 };

const char* toString(InviteRole role);  // "contact", "monitor", "admin"

// One invite link made by /addcontact, /addmonitor or /addadmin.
struct Invite {
    std::string code;  // the part after ?start= in the link
    InviteRole role = InviteRole::Contact;
    int64_t createdAt = 0;  // seconds since 1970 (UTC), 0 if the clock wasn't set
    int64_t createdBy = 0;  // Telegram ID of the admin who made it
    // Telegram ID of the person who opened it, or 0 if nobody has yet. Once
    // opened, the link only works for them.
    int64_t userId = 0;
    // When their setup was forgotten after a week with no reply, so they can be
    // told to ask for a new link if they come back. 0 if it hasn't happened.
    int64_t timedOutAt = 0;
};

// Invite links, kept in internal memory (NVS) under one key.
//
// A link works for 48 hours after it's made, and only for the first person
// who opens it. The record is kept a while longer so someone who taps it late
// is told it has run out rather than being ignored. Records are removed once
// setup finishes, and tidy() clears out ones nothing needs any more.
class InviteStore {
public:
    static constexpr int64_t kLifetimeSeconds = 48 * 60 * 60;
    // How long an unused link is remembered after it runs out.
    static constexpr int64_t kKeepUnusedSeconds = 14 * 24 * 60 * 60;
    // How long someone whose setup was forgotten is remembered.
    static constexpr int64_t kKeepTimedOutSeconds = 30 * 24 * 60 * 60;
    static constexpr size_t kMaxInvites = 20;

    explicit InviteStore(SettingsStore& store) : store_(store) {}

    std::vector<Invite> all() const;
    bool find(const std::string& code, Invite& out) const;
    // The most recent invite this person opened.
    bool findByUser(int64_t userId, Invite& out) const;
    // Adds, or replaces the one with the same code. False if it can't be saved
    // (kMaxInvites already, or out of space).
    bool save(const Invite& invite);
    bool remove(const std::string& code);

    // True once the link is more than 48 hours old. Links made before the
    // clock was set never run out by time.
    static bool isExpired(const Invite& invite, int64_t now);

    // Removes records nothing needs any more. `inSetup` says whether the person
    // who opened an invite is still in its setup conversation. Does nothing if
    // the clock isn't set. Returns how many were removed.
    size_t tidy(int64_t now, const std::function<bool(const Invite&)>& inSetup);

private:
    bool saveAll(const std::vector<Invite>& invites);

    SettingsStore& store_;
};

}  // namespace fvb

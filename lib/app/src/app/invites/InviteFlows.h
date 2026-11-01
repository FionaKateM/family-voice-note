#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/bot/Bot.h"
#include "app/invites/InviteStore.h"
#include "app/invites/PendingStore.h"
#include "hal/Random.h"

namespace fvb {

// Adding people to the box (section 10 of the requirements):
//
//   /addcontact, /addmonitor, /addadmin   an admin gets an invite link to pass on
//   the invite link                       starts that person's setup conversation
//   approval                              every admin gets "Approve? [Yes] [No]"
//   /me                                   contacts change their own details
//
// Contact setup asks what the child calls them, their country and time zone,
// their sleep hours and their photo, then shows the notice that parents may hear
// their voice notes. Monitor and admin setup explain what the role means and
// ask them to join.
//
// Someone whose setup was stopped, or forgotten after a week with no reply,
// isn't ignored if they come back: they're told to tap their link again or to
// ask for a new one. Someone waiting for approval is told so.
//
// Create one after the Bot; it registers its commands, buttons and
// conversations with it. Both must stay alive together.
class InviteFlows {
public:
    // Letters and digits; 20 of them is about 119 bits, far too many to guess.
    static constexpr size_t kCodeLength = 20;
    // Longest name a contact can choose, in characters.
    static constexpr size_t kMaxNameLength = 30;
    // How wide a photo to ask Telegram for (the Core2 screen is 320 pixels wide).
    static constexpr int kPhotoWidth = 320;

    InviteFlows(Bot& bot, Storage& storage, SettingsStore& store, Random& random);
    ~InviteFlows();

    InviteStore& invites() { return invites_; }
    // Sends the admin an invite link for the role, as /addcontact and friends
    // do. Also used by the /settings menu.
    void makeInvite(Context& ctx, InviteRole role);
    PendingStore& pending() { return pending_; }

    // Conversation names. Saved with conversations, so never change them.
    static constexpr const char* kContactSetup = "contact_setup";
    static constexpr const char* kMonitorSetup = "monitor_setup";
    static constexpr const char* kAdminSetup = "admin_setup";
    static constexpr const char* kMe = "me";

private:
    class ContactDetails;
    class JoinRole;
    friend class ContactDetails;
    friend class JoinRole;

    std::string newCode();
    bool onStartLink(Context& ctx, const std::string& code);
    bool onStranger(Context& ctx);
    void onApproveButton(Context& ctx, const std::string& data);

    // Setup finished: saves the request and asks every admin to approve it.
    // Returns false if it couldn't be saved.
    bool submit(Context& ctx, PendingApproval request, const std::string& inviteCode);
    // A setup conversation was forgotten after a week: remember to tell them.
    void setupTimedOut(const ConversationState& state, int64_t now);
    void tidy(int64_t now);

    std::string childName() const;

    Bot& bot_;
    Storage& storage_;
    Random& random_;
    InviteStore invites_;
    PendingStore pending_;
    std::unique_ptr<ContactDetails> contactSetup_;
    std::unique_ptr<ContactDetails> me_;
    std::unique_ptr<JoinRole> monitorSetup_;
    std::unique_ptr<JoinRole> adminSetup_;
};

}  // namespace fvb

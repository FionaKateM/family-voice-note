#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "app/bot/Roles.h"
#include "telegram/Types.h"

namespace fvb {

class Context;

// Where one person is in a multi-step conversation with the bot, e.g. halfway
// through contact setup. Everything a conversation needs to carry on must be in
// here, because it's saved to internal memory after every step so it carries on
// after a restart.
struct ConversationState {
    uint32_t id = 0;        // unique on this box; old buttons carry it so they can be spotted
    std::string type;       // ConversationType::name()
    std::string step;       // whatever the type uses to know where it is
    int64_t userId = 0;     // who it's with (Telegram user ID, which is also their private chat ID)
    int64_t lastActive = 0;  // seconds since 1970 (UTC), 0 if the clock wasn't set
    std::map<std::string, std::string> data;  // answers collected so far

    std::string get(const std::string& key, const std::string& fallback = "") const {
        auto it = data.find(key);
        return it == data.end() ? fallback : it->second;
    }
    void set(const std::string& key, const std::string& value) { data[key] = value; }
};

// One kind of multi-step conversation (contact setup, /me, the timezone picker...).
//
// A type holds no state of its own: everything lives in ConversationState, so
// the same object handles everyone's conversation of that kind. Register it with
// Bot::addConversation, then start it from a command or button handler with
// Context::startConversation.
//
// Each method returns whether the conversation carries on or is finished.
// Finished conversations are forgotten.
class ConversationType {
public:
    enum class Outcome { Continue, Finished };

    virtual ~ConversationType() = default;

    // Saved with the state, so never change it once released.
    virtual const char* name() const = 0;

    // Checked before every step. If someone loses the role a conversation needs
    // (an admin is removed halfway through), the conversation is dropped.
    // Invite setup returns true for anyone, because the person isn't on any
    // list until they're approved.
    virtual bool allows(const Roles& roles) const = 0;

    // Called when the conversation starts. Usually asks the first question.
    virtual Outcome begin(Context& ctx, ConversationState& state) = 0;
    // Any message from them while the conversation is going, apart from
    // commands the box knows (those end the conversation) and /cancel.
    virtual Outcome onMessage(Context& ctx, ConversationState& state, const telegram::Message& message) = 0;
    // They pressed a button made with Context::button during this conversation.
    virtual Outcome onButton(Context& ctx, ConversationState& state, const std::string& value) = 0;
};

}  // namespace fvb

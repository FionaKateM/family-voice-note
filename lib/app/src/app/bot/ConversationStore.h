#pragma once

#include <cstdint>
#include <vector>

#include "app/bot/Conversation.h"
#include "hal/SettingsStore.h"

namespace fvb {

// Conversations in progress, kept in internal memory (NVS) so they carry on
// after a restart. Each person has at most one.
//
// They're all kept under one NVS key, which holds up to 4000 bytes. That's
// plenty for a family, but if it ever fills up the conversation that's been
// quiet longest is dropped to make room.
class ConversationStore {
public:
    explicit ConversationStore(SettingsStore& store) : store_(store) {}

    std::vector<ConversationState> all() const;
    bool find(int64_t userId, ConversationState& out) const;
    // Adds, or replaces the one already saved for that person.
    // False if it couldn't be saved at all.
    bool save(const ConversationState& state);
    bool remove(int64_t userId);
    // Drops conversations quiet for longer than maxAgeSeconds. Ones saved before
    // the clock was set are left alone. Returns the ones dropped.
    std::vector<ConversationState> removeExpired(int64_t now, int64_t maxAgeSeconds);

    // A new conversation ID. Never reused.
    uint32_t nextId();

private:
    bool saveAll(std::vector<ConversationState> states);

    SettingsStore& store_;
};

}  // namespace fvb

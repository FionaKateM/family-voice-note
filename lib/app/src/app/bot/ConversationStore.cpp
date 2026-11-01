#include "app/bot/ConversationStore.h"

#include <algorithm>

#include "app/storage/TextRecord.h"

namespace fvb {

namespace {
constexpr const char* kConversations = "convs";
constexpr const char* kNextId = "conv_next";

// One line per conversation: id, type, step, user, last active, then the data
// as key, value, key, value...
std::string encode(const ConversationState& s) {
    std::vector<std::string> fields = {std::to_string(s.id), s.type, s.step, std::to_string(s.userId),
                                       std::to_string(s.lastActive)};
    for (const auto& entry : s.data) {
        fields.push_back(entry.first);
        fields.push_back(entry.second);
    }
    return text::joinFields(fields);
}

bool decode(const std::string& line, ConversationState& s) {
    const auto f = text::splitFields(line);
    if (f.size() < 5 || (f.size() - 5) % 2 != 0) return false;
    uint64_t id;
    if (!text::parseUint(f[0], id) || !text::parseInt(f[3], s.userId) || !text::parseInt(f[4], s.lastActive)) {
        return false;
    }
    if (id == 0 || s.userId == 0 || f[1].empty()) return false;
    s.id = static_cast<uint32_t>(id);
    s.type = f[1];
    s.step = f[2];
    s.data.clear();
    for (size_t i = 5; i + 1 < f.size(); i += 2) s.data[f[i]] = f[i + 1];
    return true;
}

std::string encodeAll(const std::vector<ConversationState>& states) {
    std::string out;
    for (const auto& s : states) out += encode(s) + "\n";
    return out;
}
}  // namespace

std::vector<ConversationState> ConversationStore::all() const {
    std::vector<ConversationState> states;
    for (const auto& line : text::splitLines(store_.getString(kConversations))) {
        ConversationState s;
        if (decode(line, s)) states.push_back(s);
    }
    return states;
}

bool ConversationStore::find(int64_t userId, ConversationState& out) const {
    for (const auto& s : all()) {
        if (s.userId == userId) {
            out = s;
            return true;
        }
    }
    return false;
}

bool ConversationStore::saveAll(std::vector<ConversationState> states) {
    if (states.empty()) {
        store_.remove(kConversations);
        return true;
    }
    return store_.setString(kConversations, encodeAll(states));
}

bool ConversationStore::save(const ConversationState& state) {
    if (state.userId == 0 || state.id == 0 || state.type.empty()) return false;
    if (encode(state).size() + 1 > SettingsStore::kMaxValueLength) return false;

    auto states = all();
    states.erase(std::remove_if(states.begin(), states.end(),
                                [&](const ConversationState& s) { return s.userId == state.userId; }),
                 states.end());
    states.push_back(state);

    // Make room by dropping whoever has been quiet longest (never the one being saved).
    while (encodeAll(states).size() > SettingsStore::kMaxValueLength && states.size() > 1) {
        auto oldest = std::min_element(states.begin(), states.end() - 1,
                                       [](const ConversationState& a, const ConversationState& b) {
                                           return a.lastActive < b.lastActive;
                                       });
        states.erase(oldest);
    }
    return saveAll(states);
}

bool ConversationStore::remove(int64_t userId) {
    auto states = all();
    const auto before = states.size();
    states.erase(std::remove_if(states.begin(), states.end(),
                                [&](const ConversationState& s) { return s.userId == userId; }),
                 states.end());
    if (states.size() == before) return false;
    return saveAll(states);
}

size_t ConversationStore::removeExpired(int64_t now, int64_t maxAgeSeconds) {
    if (now <= 0) return 0;
    auto states = all();
    const auto before = states.size();
    states.erase(std::remove_if(states.begin(), states.end(),
                                [&](const ConversationState& s) {
                                    return s.lastActive > 0 && now - s.lastActive > maxAgeSeconds;
                                }),
                 states.end());
    const size_t dropped = before - states.size();
    if (dropped > 0) saveAll(states);
    return dropped;
}

uint32_t ConversationStore::nextId() {
    const int64_t id = std::max<int64_t>(1, store_.getInt(kNextId, 1));
    store_.setInt(kNextId, id + 1);
    return static_cast<uint32_t>(id);
}

}  // namespace fvb

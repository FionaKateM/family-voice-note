#include "app/invites/InviteStore.h"

#include <algorithm>

#include "app/storage/TextRecord.h"

namespace fvb {

namespace {
constexpr const char* kInvites = "invites";

// One line per invite: code, role, created, created by, user, timed out.
std::string encode(const Invite& i) {
    return text::joinFields({i.code, std::to_string(static_cast<int>(i.role)), std::to_string(i.createdAt),
                             std::to_string(i.createdBy), std::to_string(i.userId), std::to_string(i.timedOutAt)});
}

bool decode(const std::string& line, Invite& i) {
    const auto f = text::splitFields(line);
    if (f.size() != 6 || f[0].empty()) return false;
    int64_t role = 0;
    if (!text::parseInt(f[1], role) || role < 1 || role > 3) return false;
    if (!text::parseInt(f[2], i.createdAt) || !text::parseInt(f[3], i.createdBy) || !text::parseInt(f[4], i.userId) ||
        !text::parseInt(f[5], i.timedOutAt)) {
        return false;
    }
    i.code = f[0];
    i.role = static_cast<InviteRole>(role);
    return true;
}
}  // namespace

const char* toString(InviteRole role) {
    switch (role) {
        case InviteRole::Contact: return "contact";
        case InviteRole::Monitor: return "monitor";
        case InviteRole::Admin: return "admin";
    }
    return "?";
}

std::vector<Invite> InviteStore::all() const {
    std::vector<Invite> out;
    for (const auto& line : text::splitLines(store_.getString(kInvites))) {
        Invite i;
        if (decode(line, i)) out.push_back(i);
    }
    return out;
}

bool InviteStore::find(const std::string& code, Invite& out) const {
    for (const auto& i : all()) {
        if (i.code == code) {
            out = i;
            return true;
        }
    }
    return false;
}

bool InviteStore::findByUser(int64_t userId, Invite& out) const {
    if (userId == 0) return false;
    bool found = false;
    for (const auto& i : all()) {
        if (i.userId == userId && (!found || i.createdAt >= out.createdAt)) {
            out = i;
            found = true;
        }
    }
    return found;
}

bool InviteStore::saveAll(const std::vector<Invite>& invites) {
    if (invites.empty()) {
        store_.remove(kInvites);
        return true;
    }
    std::string out;
    for (const auto& i : invites) out += encode(i) + "\n";
    if (out.size() > SettingsStore::kMaxValueLength) return false;
    return store_.setString(kInvites, out);
}

bool InviteStore::save(const Invite& invite) {
    if (invite.code.empty()) return false;
    auto invites = all();
    auto it = std::find_if(invites.begin(), invites.end(), [&](const Invite& i) { return i.code == invite.code; });
    if (it != invites.end()) {
        *it = invite;
    } else {
        if (invites.size() >= kMaxInvites) return false;
        invites.push_back(invite);
    }
    return saveAll(invites);
}

bool InviteStore::remove(const std::string& code) {
    auto invites = all();
    const auto before = invites.size();
    invites.erase(std::remove_if(invites.begin(), invites.end(), [&](const Invite& i) { return i.code == code; }),
                  invites.end());
    if (invites.size() == before) return false;
    return saveAll(invites);
}

bool InviteStore::isExpired(const Invite& invite, int64_t now) {
    return now > 0 && invite.createdAt > 0 && now - invite.createdAt > kLifetimeSeconds;
}

size_t InviteStore::tidy(int64_t now, const std::function<bool(const Invite&)>& inSetup) {
    if (now <= 0) return 0;
    auto invites = all();
    const auto before = invites.size();
    invites.erase(std::remove_if(invites.begin(), invites.end(),
                                 [&](const Invite& i) {
                                     if (i.userId == 0) {
                                         // Never opened: keep a while after it runs out, to tell late tappers.
                                         return i.createdAt > 0 &&
                                                now - i.createdAt > kLifetimeSeconds + kKeepUnusedSeconds;
                                     }
                                     if (i.timedOutAt > 0) return now - i.timedOutAt > kKeepTimedOutSeconds;
                                     if (inSetup && inSetup(i)) return false;
                                     // Opened, then stopped with /cancel. Their link can be tapped
                                     // again to start over until it runs out.
                                     return isExpired(i, now);
                                 }),
                  invites.end());
    const size_t removed = before - invites.size();
    if (removed > 0) saveAll(invites);
    return removed;
}

}  // namespace fvb

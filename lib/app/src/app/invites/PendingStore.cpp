#include "app/invites/PendingStore.h"

#include <algorithm>

#include "app/storage/TextRecord.h"

namespace fvb {

namespace {
constexpr const char* kPending = "pending";
constexpr const char* kNextId = "pending_next";
constexpr size_t kFixedFields = 10;

// One line per request: id, role, user, Telegram name, created, contact name,
// timezone, sleep start, sleep end, photo, then (chat, message) pairs.
std::string encode(const PendingApproval& p) {
    std::vector<std::string> f = {std::to_string(p.id),
                                  std::to_string(static_cast<int>(p.role)),
                                  std::to_string(p.userId),
                                  p.telegramName,
                                  std::to_string(p.createdAt),
                                  p.details.name,
                                  p.details.timezone,
                                  std::to_string(p.details.sleepStart),
                                  std::to_string(p.details.sleepEnd),
                                  p.photoFileId};
    for (const auto& m : p.adminMessages) {
        f.push_back(std::to_string(m.first));
        f.push_back(std::to_string(m.second));
    }
    return text::joinFields(f);
}

bool decode(const std::string& line, PendingApproval& p) {
    const auto f = text::splitFields(line);
    if (f.size() < kFixedFields || (f.size() - kFixedFields) % 2 != 0) return false;
    uint64_t id = 0;
    int64_t role = 0, sleepStart = 0, sleepEnd = 0;
    if (!text::parseUint(f[0], id) || id == 0 || !text::parseInt(f[1], role) || role < 1 || role > 3 ||
        !text::parseInt(f[2], p.userId) || p.userId == 0 || !text::parseInt(f[4], p.createdAt) ||
        !text::parseInt(f[7], sleepStart) || !text::parseInt(f[8], sleepEnd) || sleepStart < 0 ||
        sleepStart >= 24 * 60 || sleepEnd < 0 || sleepEnd >= 24 * 60) {
        return false;
    }
    p.id = static_cast<uint32_t>(id);
    p.role = static_cast<InviteRole>(role);
    p.telegramName = f[3];
    p.details = Contact();
    p.details.telegramId = p.userId;
    p.details.name = f[5];
    p.details.timezone = f[6];
    p.details.sleepStart = static_cast<uint16_t>(sleepStart);
    p.details.sleepEnd = static_cast<uint16_t>(sleepEnd);
    p.photoFileId = f[9];
    p.adminMessages.clear();
    for (size_t i = kFixedFields; i + 1 < f.size(); i += 2) {
        int64_t chat = 0, message = 0;
        if (text::parseInt(f[i], chat) && text::parseInt(f[i + 1], message)) p.adminMessages.emplace_back(chat, message);
    }
    return true;
}

std::string encodeAll(const std::vector<PendingApproval>& requests) {
    std::string out;
    for (const auto& p : requests) out += encode(p) + "\n";
    return out;
}
}  // namespace

std::vector<PendingApproval> PendingStore::all() const {
    std::vector<PendingApproval> out;
    for (const auto& line : text::splitLines(store_.getString(kPending))) {
        PendingApproval p;
        if (decode(line, p)) out.push_back(p);
    }
    return out;
}

bool PendingStore::find(uint32_t id, PendingApproval& out) const {
    for (const auto& p : all()) {
        if (p.id == id) {
            out = p;
            return true;
        }
    }
    return false;
}

bool PendingStore::findByUser(int64_t userId, PendingApproval& out) const {
    for (const auto& p : all()) {
        if (p.userId == userId) {
            out = p;
            return true;
        }
    }
    return false;
}

bool PendingStore::saveAll(const std::vector<PendingApproval>& requests) {
    if (requests.empty()) {
        store_.remove(kPending);
        return true;
    }
    return store_.setString(kPending, encodeAll(requests));
}

uint32_t PendingStore::save(PendingApproval request) {
    if (request.userId == 0) return 0;
    if (request.id == 0) {
        const int64_t id = std::max<int64_t>(1, store_.getInt(kNextId, 1));
        store_.setInt(kNextId, id + 1);
        request.id = static_cast<uint32_t>(id);
    }
    if (encode(request).size() + 1 > SettingsStore::kMaxValueLength) return 0;

    auto requests = all();
    // A newer request from the same person for the same role replaces the old one.
    requests.erase(std::remove_if(requests.begin(), requests.end(),
                                  [&](const PendingApproval& p) {
                                      return p.id == request.id ||
                                             (p.userId == request.userId && p.role == request.role);
                                  }),
                   requests.end());
    requests.push_back(request);
    // Oldest first, so drop from the front to make room.
    while (requests.size() > 1 &&
           (requests.size() > kMaxPending || encodeAll(requests).size() > SettingsStore::kMaxValueLength)) {
        requests.erase(requests.begin());
    }
    return saveAll(requests) ? request.id : 0;
}

bool PendingStore::remove(uint32_t id) {
    auto requests = all();
    const auto before = requests.size();
    requests.erase(std::remove_if(requests.begin(), requests.end(), [&](const PendingApproval& p) { return p.id == id; }),
                   requests.end());
    if (requests.size() == before) return false;
    return saveAll(requests);
}

}  // namespace fvb

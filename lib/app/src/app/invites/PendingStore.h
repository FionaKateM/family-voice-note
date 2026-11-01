#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app/invites/InviteStore.h"
#include "app/storage/People.h"
#include "hal/SettingsStore.h"

namespace fvb {

// Someone who has finished invite setup and is waiting for an admin to say yes.
struct PendingApproval {
    uint32_t id = 0;  // the box's own number for the request; approve buttons carry it
    InviteRole role = InviteRole::Contact;
    int64_t userId = 0;
    std::string telegramName;  // e.g. "Pat Example (@patexample)", so admins can check who it is
    int64_t createdAt = 0;

    // Contacts only.
    Contact details;          // name, timezone and sleep hours (no number yet)
    std::string photoFileId;  // empty for no photo

    // The "Approve?" message each admin got: (chat ID, message ID). Used to
    // change them all once one admin has answered.
    std::vector<std::pair<int64_t, int64_t>> adminMessages;
};

// Requests waiting for approval, in internal memory (NVS) under one key.
// Each person has at most one per role; a newer one replaces it.
class PendingStore {
public:
    static constexpr size_t kMaxPending = 10;

    explicit PendingStore(SettingsStore& store) : store_(store) {}

    std::vector<PendingApproval> all() const;
    bool find(uint32_t id, PendingApproval& out) const;
    bool findByUser(int64_t userId, PendingApproval& out) const;
    // Adds (giving it an ID if it has none) or replaces. If full, the oldest
    // request is dropped to make room. Returns the ID, or 0 if it couldn't be saved.
    uint32_t save(PendingApproval request);
    bool remove(uint32_t id);

private:
    bool saveAll(const std::vector<PendingApproval>& requests);

    SettingsStore& store_;
};

}  // namespace fvb

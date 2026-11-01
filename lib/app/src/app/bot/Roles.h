#pragma once

#include <cstdint>

#include "app/storage/People.h"

namespace fvb {

// What someone is to this box. One person can be several at once (a parent
// could be an admin, a contact and a monitor). Nobody is "unknown": that's
// just having none of them.
struct Roles {
    static constexpr uint8_t kAdmin = 1;
    static constexpr uint8_t kContact = 2;
    static constexpr uint8_t kMonitor = 4;
    static constexpr uint8_t kAnyKnown = kAdmin | kContact | kMonitor;

    uint8_t bits = 0;

    bool isAdmin() const { return (bits & kAdmin) != 0; }
    bool isContact() const { return (bits & kContact) != 0; }
    bool isMonitor() const { return (bits & kMonitor) != 0; }
    bool isKnown() const { return bits != 0; }
    // True if they have at least one of the roles in `mask`.
    bool any(uint8_t mask) const { return (bits & mask) != 0; }

    // Looks the person up in the admin, contact and monitor lists.
    static Roles of(const People& people, int64_t telegramId) {
        Roles r;
        if (telegramId == 0) return r;
        if (people.isAdmin(telegramId)) r.bits |= kAdmin;
        if (people.isContact(telegramId)) r.bits |= kContact;
        if (people.isMonitor(telegramId)) r.bits |= kMonitor;
        return r;
    }
};

}  // namespace fvb

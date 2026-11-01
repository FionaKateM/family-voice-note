#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/SettingsStore.h"

namespace fvb {

// An admin or monitor.
struct Person {
    int64_t telegramId = 0;
    std::string name;  // their Telegram first name; may be empty until they first message the bot
};

// An approved family member who appears on the box.
struct Contact {
    static constexpr uint16_t kDefaultSleepStart = 21 * 60;  // 9pm, minutes after midnight
    static constexpr uint16_t kDefaultSleepEnd = 9 * 60;     // 9am

    uint32_t number = 0;  // the box's own number for them, used for their SD card folder
    int64_t telegramId = 0;
    std::string name;      // what the child calls them, e.g. "Granny"
    std::string timezone;  // e.g. "Europe/London"
    uint16_t sleepStart = kDefaultSleepStart;
    uint16_t sleepEnd = kDefaultSleepEnd;
};

// Admins, monitors and contacts, kept in internal memory (NVS).
//
// One person can be in more than one list (a parent could be all three).
class People {
public:
    // NVS strings are limited, so lists are capped. Far more than a family needs.
    static constexpr size_t kMaxAdmins = 10;
    static constexpr size_t kMaxMonitors = 10;
    static constexpr size_t kMaxContacts = 30;
    // Removed contacts remembered so they can be told the box isn't taking their
    // voice notes. When the list is full the oldest is forgotten.
    static constexpr size_t kMaxRemoved = 30;
    // The reply to a removed contact is sent at most this often.
    static constexpr int64_t kRemovedReplyGapSeconds = 24 * 60 * 60;

    explicit People(SettingsStore& store) : store_(store) {}

    // ---- Admins ----
    std::vector<Person> admins() const;
    bool isAdmin(int64_t telegramId) const;
    // Adds, or updates the name if already an admin. False if the list is full.
    bool addAdmin(const Person& person);
    // False if they aren't an admin, or if they're the last one (the last admin can't be removed).
    bool removeAdmin(int64_t telegramId);

    // ---- Monitors ----
    std::vector<Person> monitors() const;
    bool isMonitor(int64_t telegramId) const;
    bool addMonitor(const Person& person);
    bool removeMonitor(int64_t telegramId);

    // ---- Contacts ----
    std::vector<Contact> contacts() const;  // in the order they were added
    bool findContact(uint32_t number, Contact& out) const;
    bool findContactByTelegramId(int64_t telegramId, Contact& out) const;
    bool isContact(int64_t telegramId) const;
    // Gives the contact a new number and saves them. Returns the number, or 0 if
    // they're already a contact, the list is full, or saving failed. If they'd
    // been removed before, that's forgotten.
    uint32_t addContact(Contact contact);
    // Saves changes to an existing contact (matched by number).
    bool updateContact(const Contact& contact);
    // Only removes them from the list (and remembers that they were removed). Use
    // Storage::removeContact to also remove their messages and photo.
    bool removeContact(uint32_t number);

    // ---- Removed contacts ----
    // People who were contacts until an admin removed them, so they can be told
    // apart from strangers. Forgotten if they're approved as a contact again.
    bool wasRemoved(int64_t telegramId) const;
    // For a removed contact who has just messaged the box: true if they should
    // be told the box isn't taking their voice notes, which is at most once a
    // day. Saves that they were told. False for anyone else. now is 0 if the
    // clock isn't set; they're then told only if they've never been told.
    bool shouldTellRemoved(int64_t telegramId, int64_t now);

private:
    std::vector<Person> loadPeople(const char* key) const;
    bool savePeople(const char* key, const std::vector<Person>& people);
    bool addPerson(const char* key, const Person& person, size_t max);
    bool removePerson(const char* key, int64_t telegramId);
    std::vector<uint32_t> contactNumbers() const;
    bool saveContactNumbers(const std::vector<uint32_t>& numbers);

    struct Removed {
        int64_t telegramId = 0;
        int64_t lastTold = 0;  // 0 never, -1 told while the clock wasn't set
    };
    std::vector<Removed> loadRemoved() const;
    bool saveRemoved(const std::vector<Removed>& removed);
    void rememberRemoved(int64_t telegramId);
    void forgetRemoved(int64_t telegramId);

    SettingsStore& store_;
};

}  // namespace fvb

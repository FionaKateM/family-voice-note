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
    // they're already a contact, the list is full, or saving failed.
    uint32_t addContact(Contact contact);
    // Saves changes to an existing contact (matched by number).
    bool updateContact(const Contact& contact);
    // Only removes them from the list. Use Storage::removeContact to also remove
    // their messages and photo.
    bool removeContact(uint32_t number);

private:
    std::vector<Person> loadPeople(const char* key) const;
    bool savePeople(const char* key, const std::vector<Person>& people);
    bool addPerson(const char* key, const Person& person, size_t max);
    bool removePerson(const char* key, int64_t telegramId);
    std::vector<uint32_t> contactNumbers() const;
    bool saveContactNumbers(const std::vector<uint32_t>& numbers);

    SettingsStore& store_;
};

}  // namespace fvb

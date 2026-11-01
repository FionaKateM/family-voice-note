#include "app/storage/People.h"

#include <algorithm>

#include "app/storage/TextRecord.h"

namespace fvb {

namespace {
constexpr const char* kAdmins = "admins";
constexpr const char* kMonitors = "monitors";
constexpr const char* kContacts = "contacts";         // "1,2,5"
constexpr const char* kNextContact = "next_contact";  // never goes down, so numbers aren't reused
constexpr const char* kRemoved = "removed";           // "<telegram id>\t<last told>" lines, oldest first

std::string contactKey(uint32_t number) { return "ct" + std::to_string(number); }

std::string encodeContact(const Contact& c) {
    return text::joinFields({std::to_string(c.number), std::to_string(c.telegramId), c.name, c.timezone,
                             std::to_string(c.sleepStart), std::to_string(c.sleepEnd)});
}

bool decodeContact(const std::string& line, Contact& c) {
    const auto f = text::splitFields(line);
    if (f.size() < 6) return false;
    uint64_t number, sleepStart, sleepEnd;
    int64_t id;
    if (!text::parseUint(f[0], number) || !text::parseInt(f[1], id) || !text::parseUint(f[4], sleepStart) ||
        !text::parseUint(f[5], sleepEnd)) {
        return false;
    }
    c.number = static_cast<uint32_t>(number);
    c.telegramId = id;
    c.name = f[2];
    c.timezone = f[3];
    c.sleepStart = static_cast<uint16_t>(sleepStart);
    c.sleepEnd = static_cast<uint16_t>(sleepEnd);
    return true;
}
}  // namespace

// ---- Admins and monitors ----

std::vector<Person> People::loadPeople(const char* key) const {
    std::vector<Person> people;
    for (const auto& line : text::splitLines(store_.getString(key))) {
        const auto f = text::splitFields(line);
        Person p;
        if (f.empty() || !text::parseInt(f[0], p.telegramId) || p.telegramId == 0) continue;
        if (f.size() > 1) p.name = f[1];
        people.push_back(p);
    }
    return people;
}

bool People::savePeople(const char* key, const std::vector<Person>& people) {
    std::string out;
    for (const auto& p : people) out += text::joinFields({std::to_string(p.telegramId), p.name}) + "\n";
    return store_.setString(key, out);
}

bool People::addPerson(const char* key, const Person& person, size_t max) {
    if (person.telegramId == 0) return false;
    auto people = loadPeople(key);
    for (auto& p : people) {
        if (p.telegramId == person.telegramId) {
            if (!person.name.empty()) p.name = person.name;
            return savePeople(key, people);
        }
    }
    if (people.size() >= max) return false;
    people.push_back(person);
    return savePeople(key, people);
}

bool People::removePerson(const char* key, int64_t telegramId) {
    auto people = loadPeople(key);
    const auto before = people.size();
    people.erase(std::remove_if(people.begin(), people.end(),
                                [&](const Person& p) { return p.telegramId == telegramId; }),
                 people.end());
    if (people.size() == before) return false;
    return savePeople(key, people);
}

std::vector<Person> People::admins() const { return loadPeople(kAdmins); }

bool People::isAdmin(int64_t telegramId) const {
    const auto list = admins();
    return std::any_of(list.begin(), list.end(), [&](const Person& p) { return p.telegramId == telegramId; });
}

bool People::addAdmin(const Person& person) { return addPerson(kAdmins, person, kMaxAdmins); }

bool People::removeAdmin(int64_t telegramId) {
    const auto list = admins();
    if (list.size() <= 1) return false;
    return removePerson(kAdmins, telegramId);
}

std::vector<Person> People::monitors() const { return loadPeople(kMonitors); }

bool People::isMonitor(int64_t telegramId) const {
    const auto list = monitors();
    return std::any_of(list.begin(), list.end(), [&](const Person& p) { return p.telegramId == telegramId; });
}

bool People::addMonitor(const Person& person) { return addPerson(kMonitors, person, kMaxMonitors); }
bool People::removeMonitor(int64_t telegramId) { return removePerson(kMonitors, telegramId); }

// ---- Contacts ----
//
// Each contact is stored under its own key ("ct3"), with a separate list of
// numbers. When adding, the contact is saved before the list; when removing, the
// list is changed first. So a power cut part way through never leaves the list
// pointing at a contact that isn't there.

std::vector<uint32_t> People::contactNumbers() const {
    std::vector<uint32_t> numbers;
    const std::string list = store_.getString(kContacts);
    size_t start = 0;
    while (start < list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        uint64_t n;
        if (text::parseUint(list.substr(start, comma - start), n) && n > 0) numbers.push_back(static_cast<uint32_t>(n));
        start = comma + 1;
    }
    return numbers;
}

bool People::saveContactNumbers(const std::vector<uint32_t>& numbers) {
    std::string list;
    for (size_t i = 0; i < numbers.size(); ++i) {
        if (i > 0) list += ',';
        list += std::to_string(numbers[i]);
    }
    return store_.setString(kContacts, list);
}

std::vector<Contact> People::contacts() const {
    std::vector<Contact> result;
    for (uint32_t n : contactNumbers()) {
        Contact c;
        if (decodeContact(store_.getString(contactKey(n)), c) && c.number == n) result.push_back(c);
    }
    return result;
}

bool People::findContact(uint32_t number, Contact& out) const {
    for (const auto& c : contacts()) {
        if (c.number == number) {
            out = c;
            return true;
        }
    }
    return false;
}

bool People::findContactByTelegramId(int64_t telegramId, Contact& out) const {
    for (const auto& c : contacts()) {
        if (c.telegramId == telegramId) {
            out = c;
            return true;
        }
    }
    return false;
}

bool People::isContact(int64_t telegramId) const {
    Contact unused;
    return findContactByTelegramId(telegramId, unused);
}

uint32_t People::addContact(Contact contact) {
    if (contact.telegramId == 0 || isContact(contact.telegramId)) return 0;
    auto numbers = contactNumbers();
    if (numbers.size() >= kMaxContacts) return 0;

    const int64_t next = std::max<int64_t>(store_.getInt(kNextContact, 1), 1);
    contact.number = static_cast<uint32_t>(next);
    if (!store_.setInt(kNextContact, next + 1)) return 0;
    if (!store_.setString(contactKey(contact.number), encodeContact(contact))) return 0;
    numbers.push_back(contact.number);
    if (!saveContactNumbers(numbers)) return 0;
    forgetRemoved(contact.telegramId);
    return contact.number;
}

bool People::updateContact(const Contact& contact) {
    const auto numbers = contactNumbers();
    if (std::find(numbers.begin(), numbers.end(), contact.number) == numbers.end()) return false;
    return store_.setString(contactKey(contact.number), encodeContact(contact));
}

bool People::removeContact(uint32_t number) {
    auto numbers = contactNumbers();
    const auto it = std::find(numbers.begin(), numbers.end(), number);
    if (it == numbers.end()) return false;
    numbers.erase(it);
    if (!saveContactNumbers(numbers)) return false;
    Contact c;
    if (decodeContact(store_.getString(contactKey(number)), c)) rememberRemoved(c.telegramId);
    store_.remove(contactKey(number));
    return true;
}

// ---- Removed contacts ----

std::vector<People::Removed> People::loadRemoved() const {
    std::vector<Removed> removed;
    for (const auto& line : text::splitLines(store_.getString(kRemoved))) {
        const auto f = text::splitFields(line);
        Removed r;
        if (f.empty() || !text::parseInt(f[0], r.telegramId) || r.telegramId == 0) continue;
        if (f.size() > 1) text::parseInt(f[1], r.lastTold);
        removed.push_back(r);
    }
    return removed;
}

bool People::saveRemoved(const std::vector<Removed>& removed) {
    if (removed.empty()) return store_.remove(kRemoved);
    std::string out;
    for (const auto& r : removed) {
        out += text::joinFields({std::to_string(r.telegramId), std::to_string(r.lastTold)}) + "\n";
    }
    return store_.setString(kRemoved, out);
}

void People::rememberRemoved(int64_t telegramId) {
    if (telegramId == 0) return;
    auto removed = loadRemoved();
    removed.erase(std::remove_if(removed.begin(), removed.end(),
                                 [&](const Removed& r) { return r.telegramId == telegramId; }),
                  removed.end());
    while (removed.size() >= kMaxRemoved) removed.erase(removed.begin());
    removed.push_back(Removed{telegramId, 0});
    // If this doesn't save, they're treated as a stranger: no reply at all.
    saveRemoved(removed);
}

void People::forgetRemoved(int64_t telegramId) {
    auto removed = loadRemoved();
    const auto before = removed.size();
    removed.erase(std::remove_if(removed.begin(), removed.end(),
                                 [&](const Removed& r) { return r.telegramId == telegramId; }),
                  removed.end());
    if (removed.size() != before) saveRemoved(removed);
}

bool People::wasRemoved(int64_t telegramId) const {
    for (const auto& r : loadRemoved()) {
        if (r.telegramId == telegramId) return true;
    }
    return false;
}

bool People::shouldTellRemoved(int64_t telegramId, int64_t now) {
    auto removed = loadRemoved();
    for (auto& r : removed) {
        if (r.telegramId != telegramId) continue;
        if (now <= 0) {
            // No clock, so no way to count a day. Tell them once, then wait for the clock.
            if (r.lastTold != 0) return false;
            r.lastTold = -1;
        } else {
            // A clock that went backwards counts as long enough.
            if (r.lastTold > 0 && now >= r.lastTold && now - r.lastTold < kRemovedReplyGapSeconds) return false;
            r.lastTold = now;
        }
        // Only reply if this was saved, so a failing store can't cause a reply every time.
        return saveRemoved(removed);
    }
    return false;
}

}  // namespace fvb

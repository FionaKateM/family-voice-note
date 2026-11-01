#include "app/storage/MessageStore.h"

#include <algorithm>
#include <cstdio>

#include "app/storage/Paths.h"
#include "app/storage/TextRecord.h"

namespace fvb {

namespace {

// Index file format, one record per line:
//   next	<id>
//   msg	<id>	<R|S>	<status>	<time>	<durationMs>	<file>	<telegramMessageId>	<telegramFileId>
// Status letters: U unread, H heard, W waiting, D delivered.

char statusLetter(StoredMessage::Status s) {
    switch (s) {
        case StoredMessage::Status::Unread: return 'U';
        case StoredMessage::Status::Heard: return 'H';
        case StoredMessage::Status::Waiting: return 'W';
        case StoredMessage::Status::Delivered: return 'D';
    }
    return 'U';
}

bool statusFromLetter(const std::string& s, StoredMessage::Status& out) {
    if (s == "U") out = StoredMessage::Status::Unread;
    else if (s == "H") out = StoredMessage::Status::Heard;
    else if (s == "W") out = StoredMessage::Status::Waiting;
    else if (s == "D") out = StoredMessage::Status::Delivered;
    else return false;
    return true;
}

std::string encode(const StoredMessage& m) {
    return text::joinFields({"msg", std::to_string(m.id), m.direction == StoredMessage::Direction::Sent ? "S" : "R",
                             std::string(1, statusLetter(m.status)), std::to_string(m.time),
                             std::to_string(m.durationMs), m.fileName, std::to_string(m.telegramMessageId),
                             m.telegramFileId});
}

bool decode(const std::vector<std::string>& f, StoredMessage& m) {
    if (f.size() < 9 || f[0] != "msg") return false;
    uint64_t id, duration;
    if (!text::parseUint(f[1], id) || id == 0) return false;
    if (f[2] == "S") m.direction = StoredMessage::Direction::Sent;
    else if (f[2] == "R") m.direction = StoredMessage::Direction::Received;
    else return false;
    if (!statusFromLetter(f[3], m.status)) return false;
    if (!text::parseInt(f[4], m.time) || !text::parseUint(f[5], duration)) return false;
    if (f[6].empty() || f[6].find('/') != std::string::npos) return false;
    if (!text::parseInt(f[7], m.telegramMessageId)) return false;
    m.id = static_cast<uint32_t>(id);
    m.durationMs = static_cast<uint32_t>(duration);
    m.fileName = f[6];
    m.telegramFileId = f[8];
    return true;
}

std::string fileNameFor(uint32_t id, StoredMessage::Direction direction) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04u.%s", static_cast<unsigned>(id),
                  direction == StoredMessage::Direction::Sent ? "mp3" : "ogg");
    return buf;
}

std::string tempIndexPath(uint32_t contact) { return paths::contactDir(contact) + "/index.tmp"; }

}  // namespace

MessageStore::Index MessageStore::load(uint32_t contact) const {
    Index index;
    std::string contents;
    if (!files_.readFile(paths::contactIndex(contact), contents)) {
        // A power cut between writing index.tmp and renaming it leaves only index.tmp.
        if (!files_.readFile(tempIndexPath(contact), contents)) return index;
    }

    for (const auto& line : text::splitLines(contents)) {
        const auto f = text::splitFields(line);
        if (f.size() == 2 && f[0] == "next") {
            uint64_t next;
            if (text::parseUint(f[1], next) && next > 0) index.nextId = static_cast<uint32_t>(next);
            continue;
        }
        StoredMessage m;
        if (decode(f, m)) index.messages.push_back(m);
    }

    // Never hand out an id that's already in use, even if the "next" line was lost.
    for (const auto& m : index.messages) index.nextId = std::max(index.nextId, m.id + 1);
    std::sort(index.messages.begin(), index.messages.end(),
              [](const StoredMessage& a, const StoredMessage& b) { return a.id < b.id; });
    return index;
}

bool MessageStore::save(uint32_t contact, const Index& index) {
    std::string out = text::joinFields({"next", std::to_string(index.nextId)}) + "\n";
    for (const auto& m : index.messages) out += encode(m) + "\n";
    const std::string temp = tempIndexPath(contact);
    if (!files_.writeFile(temp, out)) return false;
    return files_.renameFile(temp, paths::contactIndex(contact));
}

std::vector<StoredMessage> MessageStore::messages(uint32_t contact) const { return load(contact).messages; }

bool MessageStore::find(uint32_t contact, uint32_t id, StoredMessage& out) const {
    for (const auto& m : load(contact).messages) {
        if (m.id == id) {
            out = m;
            return true;
        }
    }
    return false;
}

size_t MessageStore::unreadCount(uint32_t contact) const {
    const auto list = load(contact).messages;
    return static_cast<size_t>(std::count_if(list.begin(), list.end(), [](const StoredMessage& m) {
        return m.status == StoredMessage::Status::Unread;
    }));
}

std::string MessageStore::audioPath(uint32_t contact, const StoredMessage& message) const {
    return paths::contactFile(contact, message.fileName);
}

uint32_t MessageStore::add(uint32_t contact, StoredMessage message, const std::string& sourcePath) {
    if (contact == 0 || !files_.exists(sourcePath)) return 0;
    Index index = load(contact);

    message.id = index.nextId;
    message.fileName = fileNameFor(message.id, message.direction);
    // Should never happen, but don't overwrite a file the index has lost track of.
    while (files_.exists(paths::contactFile(contact, message.fileName))) {
        ++message.id;
        message.fileName = fileNameFor(message.id, message.direction);
    }

    const std::string destination = paths::contactFile(contact, message.fileName);
    if (!files_.renameFile(sourcePath, destination)) return 0;

    index.nextId = message.id + 1;
    index.messages.push_back(message);
    if (!save(contact, index)) {
        files_.renameFile(destination, sourcePath);  // put it back so nothing is lost
        return 0;
    }
    return message.id;
}

bool MessageStore::markHeard(uint32_t contact, uint32_t id) {
    Index index = load(contact);
    for (auto& m : index.messages) {
        if (m.id != id) continue;
        if (m.direction != StoredMessage::Direction::Received) return false;
        if (m.status == StoredMessage::Status::Heard) return true;
        m.status = StoredMessage::Status::Heard;
        return save(contact, index);
    }
    return false;
}

bool MessageStore::markDelivered(uint32_t contact, uint32_t id, int64_t telegramMessageId,
                                 const std::string& telegramFileId) {
    Index index = load(contact);
    for (auto& m : index.messages) {
        if (m.id != id) continue;
        if (m.direction != StoredMessage::Direction::Sent) return false;
        m.status = StoredMessage::Status::Delivered;
        m.telegramMessageId = telegramMessageId;
        m.telegramFileId = telegramFileId;
        return save(contact, index);
    }
    return false;
}

bool MessageStore::remove(uint32_t contact, uint32_t id) {
    Index index = load(contact);
    const auto it = std::find_if(index.messages.begin(), index.messages.end(),
                                 [&](const StoredMessage& m) { return m.id == id; });
    if (it == index.messages.end()) return false;
    const std::string audio = audioPath(contact, *it);
    index.messages.erase(it);
    // Index first: a leftover audio file is harmless, an index entry with no audio isn't.
    if (!save(contact, index)) return false;
    files_.removeFile(audio);
    return true;
}

bool MessageStore::removeAll(uint32_t contact) {
    if (contact == 0) return false;
    const std::string dir = paths::contactDir(contact);
    files_.removeDir(dir);
    return files_.listDir(dir).empty();
}

}  // namespace fvb

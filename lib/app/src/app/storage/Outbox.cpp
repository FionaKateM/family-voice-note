#include "app/storage/Outbox.h"

#include <algorithm>

#include "app/storage/Paths.h"
#include "app/storage/TextRecord.h"

namespace fvb {

namespace {
constexpr const char* kSuffix = ".txt";

// "12.txt" -> 12. Anything else (half-written ".tmp" files, stray files) -> false.
bool sequenceFromName(const std::string& name, uint32_t& out) {
    const std::string suffix = kSuffix;
    if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    uint64_t n;
    if (!text::parseUint(name.substr(0, name.size() - suffix.size()), n) || n == 0) return false;
    out = static_cast<uint32_t>(n);
    return true;
}
}  // namespace

std::string Outbox::pathFor(uint32_t sequence) const {
    return std::string(paths::kOutboxDir) + "/" + std::to_string(sequence) + kSuffix;
}

bool Outbox::load(const std::string& name, OutboxEntry& out) const {
    uint32_t sequence;
    if (!sequenceFromName(name, sequence)) return false;
    std::string contents;
    if (!files_.readFile(pathFor(sequence), contents)) return false;
    const auto f = text::splitFields(text::trim(contents));
    uint64_t contact, messageId, attempts, nextAt;
    int64_t queuedAt;
    if (f.size() < 5 || !text::parseUint(f[0], contact) || !text::parseUint(f[1], messageId) ||
        !text::parseInt(f[2], queuedAt) || !text::parseUint(f[3], attempts) || !text::parseUint(f[4], nextAt)) {
        return false;
    }
    out.sequence = sequence;
    out.contact = static_cast<uint32_t>(contact);
    out.messageId = static_cast<uint32_t>(messageId);
    out.queuedAt = queuedAt;
    out.attempts = static_cast<uint32_t>(attempts);
    out.nextAttemptAtMs = nextAt;
    return true;
}

bool Outbox::save(const OutboxEntry& e) {
    const std::string line =
        text::joinFields({std::to_string(e.contact), std::to_string(e.messageId), std::to_string(e.queuedAt),
                          std::to_string(e.attempts), std::to_string(e.nextAttemptAtMs)}) +
        "\n";
    // Write then rename, so an entry is never half-written.
    const std::string temp = std::string(paths::kOutboxDir) + "/" + std::to_string(e.sequence) + ".tmp";
    return files_.writeFile(temp, line) && files_.renameFile(temp, pathFor(e.sequence));
}

std::vector<OutboxEntry> Outbox::entries() const {
    std::vector<OutboxEntry> result;
    for (const auto& name : files_.listDir(paths::kOutboxDir)) {
        OutboxEntry e;
        if (load(name, e)) result.push_back(e);
    }
    std::sort(result.begin(), result.end(),
              [](const OutboxEntry& a, const OutboxEntry& b) { return a.sequence < b.sequence; });
    return result;
}

uint32_t Outbox::add(uint32_t contact, uint32_t messageId, int64_t queuedAt) {
    if (contact == 0 || messageId == 0) return 0;
    // Always one more than the highest still here, so order is kept.
    uint32_t highest = 0;
    for (const auto& name : files_.listDir(paths::kOutboxDir)) {
        uint32_t n;
        if (sequenceFromName(name, n)) highest = std::max(highest, n);
    }
    OutboxEntry e;
    e.sequence = highest + 1;
    e.contact = contact;
    e.messageId = messageId;
    e.queuedAt = queuedAt;
    return save(e) ? e.sequence : 0;
}

bool Outbox::recordFailure(uint32_t sequence, uint64_t nextAttemptAtMs) {
    OutboxEntry e;
    if (!load(std::to_string(sequence) + kSuffix, e)) return false;
    ++e.attempts;
    e.nextAttemptAtMs = nextAttemptAtMs;
    return save(e);
}

bool Outbox::remove(uint32_t sequence) { return files_.removeFile(pathFor(sequence)); }

size_t Outbox::removeForContact(uint32_t contact) {
    size_t removed = 0;
    for (const auto& e : entries()) {
        if (e.contact == contact && remove(e.sequence)) ++removed;
    }
    return removed;
}

void Outbox::resetRetryTimes() {
    for (auto e : entries()) {
        if (e.nextAttemptAtMs == 0) continue;
        e.nextAttemptAtMs = 0;
        save(e);
    }
}

}  // namespace fvb

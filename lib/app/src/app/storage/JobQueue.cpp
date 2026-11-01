#include "app/storage/JobQueue.h"

#include <algorithm>

#include "app/storage/Paths.h"
#include "app/storage/TextRecord.h"

namespace fvb {

namespace {
constexpr const char* kSuffix = ".txt";

// File format, one line:
//   <kind>	<contact>	<chatId>	<telegramMessageId>	<fileId>	<time>	<durationMs>	<text>	<attempts>	<nextAttemptAtMs>
// Kind letters: D download, R reaction, C copy, N notice.

char kindLetter(Job::Kind kind) {
    switch (kind) {
        case Job::Kind::Download: return 'D';
        case Job::Kind::Reaction: return 'R';
        case Job::Kind::Copy: return 'C';
        case Job::Kind::Notice: return 'N';
    }
    return 'D';
}

bool kindFromLetter(const std::string& s, Job::Kind& out) {
    if (s == "D") out = Job::Kind::Download;
    else if (s == "R") out = Job::Kind::Reaction;
    else if (s == "C") out = Job::Kind::Copy;
    else if (s == "N") out = Job::Kind::Notice;
    else return false;
    return true;
}

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

std::string JobQueue::pathFor(uint32_t sequence) const {
    return std::string(paths::kJobsDir) + "/" + std::to_string(sequence) + kSuffix;
}

bool JobQueue::load(const std::string& name, Job& out) const {
    uint32_t sequence;
    if (!sequenceFromName(name, sequence)) return false;
    std::string contents;
    if (!files_.readFile(pathFor(sequence), contents)) return false;
    const auto lines = text::splitLines(contents);
    if (lines.empty()) return false;
    const auto f = text::splitFields(lines[0]);
    uint64_t contact, duration, attempts, nextAt;
    if (f.size() < 10 || !kindFromLetter(f[0], out.kind) || !text::parseUint(f[1], contact) ||
        !text::parseInt(f[2], out.chatId) || !text::parseInt(f[3], out.telegramMessageId) ||
        !text::parseInt(f[5], out.time) || !text::parseUint(f[6], duration) || !text::parseUint(f[8], attempts) ||
        !text::parseUint(f[9], nextAt)) {
        return false;
    }
    out.sequence = sequence;
    out.contact = static_cast<uint32_t>(contact);
    out.fileId = f[4];
    out.durationMs = static_cast<uint32_t>(duration);
    out.text = f[7];
    out.attempts = static_cast<uint32_t>(attempts);
    out.nextAttemptAtMs = nextAt;
    return true;
}

bool JobQueue::save(const Job& j) {
    const std::string line =
        text::joinFields({std::string(1, kindLetter(j.kind)), std::to_string(j.contact), std::to_string(j.chatId),
                          std::to_string(j.telegramMessageId), j.fileId, std::to_string(j.time),
                          std::to_string(j.durationMs), j.text, std::to_string(j.attempts),
                          std::to_string(j.nextAttemptAtMs)}) +
        "\n";
    const std::string temp = std::string(paths::kJobsDir) + "/" + std::to_string(j.sequence) + ".tmp";
    return files_.writeFile(temp, line) && files_.renameFile(temp, pathFor(j.sequence));
}

std::vector<Job> JobQueue::jobs() const {
    std::vector<Job> result;
    for (const auto& name : files_.listDir(paths::kJobsDir)) {
        Job j;
        if (load(name, j)) result.push_back(j);
    }
    std::sort(result.begin(), result.end(), [](const Job& a, const Job& b) { return a.sequence < b.sequence; });
    return result;
}

uint32_t JobQueue::add(Job job) {
    uint32_t highest = 0;
    for (const auto& name : files_.listDir(paths::kJobsDir)) {
        uint32_t n;
        if (sequenceFromName(name, n)) highest = std::max(highest, n);
    }
    job.sequence = highest + 1;
    job.attempts = 0;
    return save(job) ? job.sequence : 0;
}

bool JobQueue::recordFailure(uint32_t sequence, uint64_t nextAttemptAtMs) {
    Job j;
    if (!load(std::to_string(sequence) + kSuffix, j)) return false;
    ++j.attempts;
    j.nextAttemptAtMs = nextAttemptAtMs;
    return save(j);
}

bool JobQueue::remove(uint32_t sequence) { return files_.removeFile(pathFor(sequence)); }

size_t JobQueue::removeForContact(uint32_t contact) {
    size_t removed = 0;
    for (const auto& j : jobs()) {
        const bool theirs = j.kind == Job::Kind::Download || j.kind == Job::Kind::Reaction;
        if (theirs && j.contact == contact && remove(j.sequence)) ++removed;
    }
    return removed;
}

void JobQueue::resetRetryTimes() {
    for (auto j : jobs()) {
        if (j.nextAttemptAtMs == 0) continue;
        j.nextAttemptAtMs = 0;
        save(j);
    }
}

}  // namespace fvb

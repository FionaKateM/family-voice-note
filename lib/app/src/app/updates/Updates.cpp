#include "app/updates/Updates.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "app/Version.h"
#include "app/storage/TextRecord.h"
#include "app/time/TimeChecks.h"
#include "app/updates/Sha256.h"

namespace fvb {

using telegram::InlineButton;
using telegram::InlineKeyboard;

namespace {

// NVS keys. 15 characters at most.
constexpr const char* kLastCheckDay = "upd_day";  // the box's local date the daily check last worked, as days since 1970
constexpr const char* kToldAbout = "upd_told";      // the version the daily check last told admins about
constexpr const char* kInstalling = "upd_installing";  // see Updates::finish

constexpr const char* kPrefix = "update";

constexpr const char* kRepo = "FionaKateM/family-voice-note";

bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool sameVersion(const std::string& a, const std::string& b) {
    std::vector<int> x, y;
    if (!version::parse(a, x) || !version::parse(b, y)) return a == b;
    while (x.size() < y.size()) x.push_back(0);
    while (y.size() < x.size()) y.push_back(0);
    return x == y;
}

// "1.3.0" -> "1.3.0", "v1.3" -> "1.3": how a version is shown to people.
std::string shown(const std::string& v) {
    return !v.empty() && (v[0] == 'v' || v[0] == 'V') ? v.substr(1) : v;
}

// Cuts text to `max` bytes without splitting a UTF-8 character, adding "…".
std::string shorten(const std::string& text, size_t max) {
    if (text.size() <= max) return text;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text::trim(text.substr(0, cut)) + "…";
}

std::string replaceVersion(const std::string& pattern, const std::string& version) {
    std::string out = pattern;
    const std::string marker = "{version}";
    const size_t at = out.find(marker);
    if (at != std::string::npos) out.replace(at, marker.size(), version);
    return out;
}

std::string httpProblem(const HttpResponse& response) {
    if (response.status == 0) return "GitHub couldn't be reached";
    return "GitHub answered with error " + std::to_string(response.status);
}

}  // namespace

// ---------------------------------------------------------------------------
// Releases

bool parseRelease(const std::string& json, Release& out, std::string& error) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonObject>()) {
        error = "firmware.json isn't JSON";
        return false;
    }
    Release r;
    r.version = doc["version"] | "";
    r.notes = text::trim(doc["notes"] | "");
    r.sha256 = doc["sha256"] | "";
    std::transform(r.sha256.begin(), r.sha256.end(), r.sha256.begin(),
                   [](char c) { return c >= 'A' && c <= 'F' ? static_cast<char>(c - 'A' + 'a') : c; });
    r.size = doc["size"] | static_cast<uint64_t>(0);

    std::vector<int> parts;
    if (!version::parse(r.version, parts)) {
        error = "firmware.json has no version, or it can't be read";
        return false;
    }
    if (!isHex64(r.sha256)) {
        error = "firmware.json has no proper checksum";
        return false;
    }
    if (r.size == 0 || r.size > Updates::kMaxFirmwareBytes) {
        error = "firmware.json gives a size that can't be right";
        return false;
    }
    out = r;
    return true;
}

ReleaseLocation ReleaseLocation::github() {
    const std::string base = std::string("https://github.com/") + kRepo + "/releases/";
    return ReleaseLocation{base + "latest/download/firmware.json", base + "download/v{version}/firmware.bin"};
}

ReleaseLocation ReleaseLocation::folder(const std::string& path) {
    std::string base = path;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return ReleaseLocation{"file://" + base + "/firmware.json", "file://" + base + "/firmware.bin"};
}

// ---------------------------------------------------------------------------
// Setting up

Updates::Updates(Bot& bot, telegram::TelegramClient& client, Storage& storage, SettingsStore& store, Clock& clock,
                 Logger& log, Network& network, FirmwareInstaller& installer, ReleaseLocation where)
    : client_(client), storage_(storage), store_(store), clock_(clock), log_(log), network_(network),
      installer_(installer), where_(std::move(where)) {
    // Not listed in /help, which only lists /settings, /status and /trash. The
    // daily message tells admins about /update when there's something to install.
    bot.addCommand("update", Roles::kAdmin, "", [this](Context& ctx, const std::string&) { ask(ctx); });
    bot.addButtons(kPrefix, Roles::kAdmin, [this](Context& ctx, const std::string& data) { onButton(ctx, data); });
}

void Updates::ask(Context& ctx) {
    if (installRequested_ || installing_) {
        ctx.reply("An update is already being installed.");
        return;
    }
    const int64_t who = ctx.sender().chatId;
    if (std::find(waitingForCheck_.begin(), waitingForCheck_.end(), who) == waitingForCheck_.end()) {
        waitingForCheck_.push_back(who);
    }
    // The check itself happens in run(), so the box isn't held up waiting for
    // GitHub. The answer follows in a second or two.
}

// ---------------------------------------------------------------------------
// The three steps

Updates::Task Updates::prepare() {
    Task task;
    if (installRequested_) {
        installRequested_ = false;
        installing_ = true;
        return install_;
    }
    if (!waitingForCheck_.empty()) {
        task.kind = Task::Kind::Check;
        task.askedBy = waitingForCheck_;
        waitingForCheck_.clear();
        return task;
    }
    if (dailyCheckDue()) task.kind = Task::Kind::Check;
    return task;
}

Updates::Result Updates::run(const Task& task) {
    switch (task.kind) {
        case Task::Kind::Check:
            return check();
        case Task::Kind::Install:
            return download(task.release);
        case Task::Kind::None:
            break;
    }
    return Result();
}

void Updates::finish(const Task& task, const Result& result) {
    if (task.kind == Task::Kind::Check) {
        const bool daily = task.askedBy.empty();
        if (!result.ok) {
            log_.warning("Couldn't check for updates: " + result.error);
            if (daily) retryAtMs_ = clock_.millisSinceBoot() + kRetryAfterFailureMs;
            for (int64_t admin : task.askedBy) {
                client_.sendMessage(admin, "I couldn't check for updates just now (" + result.error +
                                               "). Try /update again in a few minutes.");
            }
            return;
        }
        LocalTime local;
        if (daily && deviceLocalTime(storage_.settings, clock_, local)) {
            store_.setInt(kLastCheckDay, civil::daysFromDate(local.year, local.month, local.day));
        }

        if (!result.newer) {
            for (int64_t admin : task.askedBy) {
                client_.sendMessage(admin, "Already up to date (version " + shown(firmwareVersion()) + ").");
            }
            return;
        }

        const std::string v = shown(result.release.version);
        if (!task.askedBy.empty()) {
            hasOffer_ = true;
            offer_ = result.release;
            const std::string base = std::string(kPrefix) + ":" + v + ":";
            const InlineKeyboard keyboard = {{InlineButton{"Yes", base + "y"}, InlineButton{"No", base + "n"}}};
            for (int64_t admin : task.askedBy) client_.sendMessage(admin, offerText(result.release), &keyboard);
        }
        // Each new version is announced once, whether the daily check or an
        // admin's /update found it first.
        if (!sameVersion(toldAbout(), v)) {
            store_.setString(kToldAbout, v);
            if (daily) {
                tellAdmins("Version " + v + " is available. Send /update to install.");
                log_.info("Told the admins version " + v + " is available");
            }
        }
        return;
    }

    if (task.kind == Task::Kind::Install) {
        const std::string v = shown(task.release.version);
        if (!result.ok) {
            installing_ = false;
            log_.warning("Update to " + v + " failed: " + result.error);
            client_.sendMessage(task.installer, "The update to version " + v + " didn't work: " + result.error +
                                                    ". Nothing has changed. You can try again with /update.");
            return;
        }
        // Remembered so the box can say how it went once it's back:
        // the version it's going to, the version it was on, and who asked.
        store_.setString(kInstalling, text::joinFields({v, shown(firmwareVersion()),
                                                        std::to_string(task.installer), task.installerName}));
        client_.sendMessage(task.installer, "Installing version " + v +
                                                ". The box will restart, and I'll message you when it's back.");
        log_.info("Installed version " + v + "; restarting");
        installer_.restart();
        // The device never gets here. The desktop build stops instead.
        installing_ = false;
    }
}

void Updates::afterStart() {
    // The firmware that's running has started and reached Telegram, so keep it.
    installer_.markGood();

    const std::string saved = store_.getString(kInstalling);
    if (saved.empty()) return;
    store_.remove(kInstalling);
    const auto fields = text::splitFields(saved);
    if (fields.size() < 4) return;
    const std::string target = fields[0];
    const std::string before = fields[1];
    int64_t asker = 0;
    text::parseInt(fields[2], asker);
    const std::string who = fields[3].empty() ? "An admin" : fields[3];
    const std::string now = shown(firmwareVersion());

    if (sameVersion(now, target)) {
        if (asker != 0) client_.sendMessage(asker, "Updated to " + now + ".");
        tellAdmins(who + " updated the box to version " + now + ".", asker);
        log_.info("Updated to " + now);
    } else {
        tellAdmins("The update to version " + target + " didn't start properly, so the box went back to version " +
                   now + " by itself. Everything else is as it was.");
        log_.warning("The update to " + target + " didn't start; running " + now + " (was " + before + ")");
    }
}

// ---------------------------------------------------------------------------
// Checking and downloading

bool Updates::dailyCheckDue() const {
    LocalTime local;
    if (!deviceLocalTime(storage_.settings, clock_, local)) return false;
    if (!isWithinDailyHours(kCheckFrom, kCheckUntil, local.minuteOfDay())) return false;
    if (clock_.millisSinceBoot() < retryAtMs_) return false;
    return store_.getInt(kLastCheckDay, -1) != civil::daysFromDate(local.year, local.month, local.day);
}

std::string Updates::toldAbout() const { return store_.getString(kToldAbout); }

Updates::Result Updates::check() {
    Result result;
    HttpRequest request;
    request.url = where_.latestUrl;
    request.followRedirects = true;  // GitHub sends release files from another address
    request.headers["User-Agent"] = "FamilyVoiceBox";
    request.timeoutMs = 20000;
    const HttpResponse response = network_.request(request);
    if (response.status == 404) {
        // No release published yet.
        result.ok = true;
        return result;
    }
    if (!response.ok()) {
        result.error = httpProblem(response);
        return result;
    }
    std::string problem;
    if (!parseRelease(response.body, result.release, problem)) {
        result.error = "the release on GitHub is missing information (" + problem + ")";
        return result;
    }
    result.ok = true;
    result.newer = version::isNewer(result.release.version, firmwareVersion());
    return result;
}

Updates::Result Updates::download(const Release& release) {
    Result result;
    result.release = release;
    std::string problem;
    if (!installer_.begin(static_cast<size_t>(release.size), shown(release.version), problem)) {
        result.error = "the box couldn't get ready to install it (" + problem + ")";
        return result;
    }

    Sha256 sha;
    uint64_t received = 0;
    bool writeFailed = false;
    bool tooBig = false;

    HttpRequest request;
    request.url = replaceVersion(where_.firmwareUrl, shown(release.version));
    request.followRedirects = true;
    request.headers["User-Agent"] = "FamilyVoiceBox";
    request.timeoutMs = 10 * 60 * 1000;
    request.onBodyChunk = [&](const char* data, size_t length) {
        if (received + length > release.size) {
            tooBig = true;
            return false;
        }
        sha.add(data, length);
        received += length;
        if (!installer_.write(reinterpret_cast<const uint8_t*>(data), length)) {
            writeFailed = true;
            return false;
        }
        return true;
    };
    const HttpResponse response = network_.request(request);

    if (!response.ok()) {
        result.error = "the download didn't work (" + httpProblem(response) + ")";
    } else if (writeFailed) {
        result.error = "the box couldn't save the download";
    } else if (tooBig || received != release.size) {
        result.error = "the download was the wrong size";
    } else if (sha.finishHex() != release.sha256) {
        result.error = "the download was damaged on the way (its checksum didn't match)";
    } else if (!installer_.finish(problem)) {
        result.error = "the box couldn't install it (" + problem + ")";
    } else {
        result.ok = true;
        return result;
    }
    installer_.abort();
    return result;
}

// ---------------------------------------------------------------------------
// Buttons and messages

void Updates::onButton(Context& ctx, const std::string& data) {
    // data is "<version>:y" or "<version>:n"
    const size_t colon = data.rfind(':');
    const std::string v = colon == std::string::npos ? "" : data.substr(0, colon);
    const std::string answer = colon == std::string::npos ? "" : data.substr(colon + 1);

    if (answer == "n") {
        ctx.editButtonMessage("OK, staying on version " + shown(firmwareVersion()) +
                              ". Send /update whenever you'd like to install it.");
        ctx.popup("Not now");
        return;
    }
    if (installRequested_ || installing_) {
        ctx.editButtonMessage("An update is already being installed.");
        ctx.popup("Already installing");
        return;
    }
    if (answer != "y" || !hasOffer_ || !sameVersion(offer_.version, v)) {
        ctx.editButtonMessage("This has expired. Send /update to check again.");
        ctx.popup("Expired");
        return;
    }
    if (!version::isNewer(offer_.version, firmwareVersion())) {
        ctx.editButtonMessage("Already up to date (version " + shown(firmwareVersion()) + ").");
        return;
    }

    install_ = Task();
    install_.kind = Task::Kind::Install;
    install_.release = offer_;
    install_.installer = ctx.sender().chatId;
    install_.installerName = ctx.sender().firstName;
    installRequested_ = true;
    ctx.editButtonMessage("Downloading version " + shown(offer_.version) + ". This can take a few minutes.");
    ctx.popup("Downloading");
}

std::string Updates::offerText(const Release& release) const {
    std::string text = "Version " + shown(release.version) + " is available";
    const std::string notes = shorten(release.notes, kMaxNotesLength);
    if (!notes.empty()) {
        text += ": " + notes;
        const char last = notes.back();
        if (last != '.' && last != '!' && last != '?' && notes.rfind("…") != notes.size() - 3) text += ".";
    } else {
        text += ".";
    }
    text += " Install?\n\nThe box is on version " + shown(firmwareVersion()) + " at the moment. It restarts to install.";
    return text;
}

void Updates::tellAdmins(const std::string& text, int64_t skip) {
    for (const auto& admin : storage_.people.admins()) {
        if (admin.telegramId == skip) continue;
        if (!client_.sendMessage(admin.telegramId, text)) log_.warning("Couldn't send an admin a message about updates");
    }
}

}  // namespace fvb

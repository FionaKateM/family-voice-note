#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/bot/Bot.h"
#include "hal/Clock.h"
#include "hal/FirmwareInstaller.h"
#include "hal/Logger.h"
#include "hal/Network.h"
#include "hal/SettingsStore.h"

namespace fvb {

// One published release, as described by the firmware.json file attached to it
// (made by scripts/make-release.sh):
//
//   {"version":"1.3.0","notes":"What's new","size":1234567,"sha256":"<64 hex characters>"}
struct Release {
    std::string version;
    std::string notes;   // short, plain text; may be empty
    std::string sha256;  // 64 lower-case hex characters
    uint64_t size = 0;   // bytes
};

// Reads a firmware.json. False, with a reason in `error`, if anything is
// missing or doesn't look right.
bool parseRelease(const std::string& json, Release& out, std::string& error);

// Where releases are looked for.
struct ReleaseLocation {
    std::string latestUrl;    // firmware.json of the newest release
    std::string firmwareUrl;  // the firmware file, with {version} where the version goes

    // GitHub Releases on this repo. The real device only ever uses this: it's
    // built into the firmware and can't be changed by a message.
    static ReleaseLocation github();
    // A folder on the laptop holding firmware.json and firmware.bin. For trying
    // updates on the desktop build without publishing a release.
    static ReleaseLocation folder(const std::string& path);
};

// Software updates (section 13 of the requirements).
//
//  - Once a day, at 6pm in the box's time zone, the box checks for a new
//    version. The first time it sees one, every admin gets "Version 1.3 is
//    available. Send /update to install." If the box is off or offline at 6pm,
//    it checks when it's next able to that evening, up to 10pm. After that it
//    waits for 6pm the next day, so nobody gets the message late at night.
//  - /update, or the Update button in /settings (admins only), checks straight
//    away and replies with the version, the note on what's new, and Yes and No
//    buttons, or "Already up to date".
//  - Yes downloads the firmware, checks its size and SHA-256 checksum, installs
//    it and restarts. Once the box is back and has reached Telegram, the admin
//    who said yes gets "Updated to 1.3" and the other admins are told who
//    updated it.
//  - If the new version didn't start properly and the box went back to the old
//    one by itself, every admin is told.
//
// Talking to GitHub is slow, so it's split up the same way as Messaging. From
// the network loop:
//
//   Updates::Task task = updates.prepare();   // quick, needs the lock
//   if (task.kind != Updates::Task::Kind::None) {
//       auto result = updates.run(task);       // slow, without the lock
//       updates.finish(task, result);          // quick, needs the lock; may restart
//   }
//
// and once, as soon as the bot is Running after start-up:
//
//   updates.afterStart();
//
// Create one after the Bot; it registers /update and its buttons. Both must
// stay alive together.
class Updates {
public:
    // The daily check happens between these times, in the box's time zone
    // (minutes after midnight): from 18:00 up to, but not including, 22:00.
    static constexpr uint16_t kCheckFrom = 18 * 60;
    static constexpr uint16_t kCheckUntil = 22 * 60;
    // After a daily check fails (GitHub can't be reached), wait this long
    // before trying again (still only between kCheckFrom and kCheckUntil).
    static constexpr uint32_t kRetryAfterFailureMs = 60 * 60 * 1000;
    // Longest note on what's new that's shown in Telegram. Longer ones are cut.
    static constexpr size_t kMaxNotesLength = 300;
    // Anything bigger can't be firmware for this box.
    static constexpr uint64_t kMaxFirmwareBytes = 8 * 1024 * 1024;

    struct Task {
        enum class Kind { None, Check, Install };
        Kind kind = Kind::None;
        // Check: admins who sent /update and are waiting for an answer. Empty
        // for the daily check.
        std::vector<int64_t> askedBy;
        // Install: what to install, and who said yes.
        Release release;
        int64_t installer = 0;
        std::string installerName;
    };

    struct Result {
        bool ok = false;
        std::string error;  // plain words, safe to show an admin
        // Check: whether `release` is newer than what's running. A repo with no
        // releases yet counts as up to date.
        bool newer = false;
        Release release;
    };

    Updates(Bot& bot, telegram::TelegramClient& client, Storage& storage, SettingsStore& store, Clock& clock,
            Logger& log, Network& network, FirmwareInstaller& installer,
            ReleaseLocation where = ReleaseLocation::github());

    // What /update and the Update button in /settings do: checks for a new
    // version and answers the admin in a second or two.
    void ask(Context& ctx);

    Task prepare();
    Result run(const Task& task);
    void finish(const Task& task, const Result& result);
    void afterStart();

    // ---- Smaller pieces, public for tests ----

    // True if it's between 18:00 and 22:00 in the box's time zone and the daily
    // check hasn't worked yet today. False until the clock and the box's time
    // zone are set.
    bool dailyCheckDue() const;
    Result check();
    Result download(const Release& release);
    // The version admins were last told about by the daily check, or "".
    std::string toldAbout() const;

private:
    void onButton(Context& ctx, const std::string& data);
    void tellAdmins(const std::string& text, int64_t skip = 0);
    std::string offerText(const Release& release) const;

    telegram::TelegramClient& client_;
    Storage& storage_;
    SettingsStore& store_;
    Clock& clock_;
    Logger& log_;
    Network& network_;
    FirmwareInstaller& installer_;
    ReleaseLocation where_;

    // Kept in memory only. After a restart, an admin just sends /update again.
    std::vector<int64_t> waitingForCheck_;
    bool hasOffer_ = false;
    Release offer_;  // the release last offered by /update
    bool installRequested_ = false;
    bool installing_ = false;
    Task install_;
    uint64_t retryAtMs_ = 0;
};

}  // namespace fvb

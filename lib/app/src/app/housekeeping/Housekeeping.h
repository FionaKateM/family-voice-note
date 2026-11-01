#pragma once

#include <cstdint>
#include <string>

#include "app/storage/Storage.h"
#include "hal/Clock.h"
#include "hal/Logger.h"
#include "hal/Power.h"
#include "hal/SettingsStore.h"

namespace fvb {

// Jobs the box does by itself in the background (section 7 of the
// requirements, and low battery from section 4):
//
//   - Clearing the bin: recordings binned more than 30 days ago are deleted.
//   - Removing old messages: messages older than the "keep messages" setting
//     (default 60 days, 0 means forever) are deleted from the card. They stay in
//     Telegram on family members' phones.
//   - Storage alert: admins are told when the card is 80% full.
//   - Offline alert: when the box gets back online after more than 24 hours
//     away, admins are told when it was offline and which voice notes might
//     not have arrived.
//   - Low battery: at 20%, 10% and 5% admins get a message, and the box should
//     play a sound and show an LED pattern (held until quiet hours end).
//
// Messages to admins go in the job queue like any other notice, so they wait
// and retry if Telegram can't be reached.
//
// Nothing that depends on the date happens until the clock has been set from
// the internet. Messages and recordings dated before the clock was ever set
// are kept, because their real age is unknown.
//
// What it has already told the admins is saved in internal memory, so a
// restart doesn't send the same alert twice.
//
// Call tick() often (from the main loop). It does real work only every so
// often and returns straight away otherwise.
class Housekeeping {
public:
    // How often the bin and old messages are cleared out, and the card checked.
    static constexpr uint64_t kCleanUpEveryMs = 60ULL * 60 * 1000;
    // How often the battery is read.
    static constexpr uint64_t kBatteryCheckEveryMs = 10ULL * 1000;
    // How often "last seen online" is saved. Saving to internal memory wears it,
    // so not every loop. The offline alert's start time can be this much early.
    static constexpr int64_t kSaveOnlineEverySeconds = 5 * 60;

    static constexpr int kStorageAlertPercent = 80;
    // The storage alert can only come again after the card has dropped below this.
    static constexpr int kStorageRearmPercent = 75;

    // Offline for longer than this gets an alert. It's also roughly how long
    // Telegram holds messages for a bot that isn't collecting them.
    static constexpr int64_t kOfflineAlertSeconds = 24 * 60 * 60;

    // Low battery levels, highest first.
    static constexpr int kBatteryLevels[] = {20, 10, 5};
    // A level can alert again once the battery has gone this far above it
    // (normally by being charged). Stops small wobbles in the reading from
    // sending the same alert again.
    static constexpr int kBatteryRearmMargin = 5;

    Housekeeping(Storage& storage, SettingsStore& store, Clock& clock, Power& power, Logger& log);

    // `online` is whether the box can reach the internet right now.
    void tick(bool online);

    // The pieces tick() runs, for tests. Each is safe to call at any time.

    // Clears the bin and old messages, then checks how full the card is.
    void cleanUp();
    // Deletes received and sent messages older than the keep messages setting.
    // Unread messages and ones still waiting to send are kept. Returns how many.
    size_t removeOldMessages(int64_t now);
    // Sends the storage alert if the card has just reached 80%.
    void checkStorage();
    // Notes that the box is online, and sends the offline alert if it's been
    // away for more than a day.
    void checkOffline(bool online);
    void checkBattery();

    // True once, when a low battery sound and LED pattern is due. Held during
    // quiet hours and given out once they end. The sound itself comes with the
    // device's audio (tickets 2.6 and 2.8).
    bool takeBatteryAlert(int& level);

    // The offline alert text, e.g. "Child's box was offline from Friday 18:00 to
    // Sunday 10:00. ...". Times are in the box's timezone (UTC if it isn't set).
    std::string offlineText(int64_t from, int64_t to) const;

    // When the box was last known to be online, or 0. For tests and the
    // desktop's --offline-hours option.
    int64_t lastOnline() const;
    void setLastOnline(int64_t unixTime);

private:
    void tellAdmins(const std::string& text);
    std::string childName() const;
    std::string formatMoment(int64_t unixTime, bool withDate) const;

    Storage& storage_;
    SettingsStore& store_;
    Clock& clock_;
    Power& power_;
    Logger& log_;

    bool cleanedOnce_ = false;
    uint64_t nextCleanUpMs_ = 0;
    uint64_t nextBatteryCheckMs_ = 0;
    int64_t lastOnlineSaved_ = 0;  // what's in internal memory, to avoid reading it each loop
    bool lastOnlineLoaded_ = false;
    bool wasOnline_ = false;
    int pendingBatteryAlert_ = 0;  // level, or 0 for none
};

}  // namespace fvb

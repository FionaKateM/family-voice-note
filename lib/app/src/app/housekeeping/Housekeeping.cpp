#include "app/housekeeping/Housekeeping.h"

#include <vector>

#include "app/storage/TextRecord.h"
#include "app/time/PosixTimeZone.h"
#include "app/time/TimeChecks.h"
#include "app/time/TimeZones.h"

namespace fvb {

namespace {

// Internal memory keys (15 characters at most).
constexpr const char* kLastOnlineKey = "hk_online";    // seconds since 1970
constexpr const char* kStorageAlertKey = "hk_storage";  // 1 once the 80% alert has gone
constexpr const char* kBatteryKey = "hk_battery";       // lowest level alerted since the last charge, or 0

constexpr int64_t kDaySeconds = 24 * 60 * 60;

}  // namespace

Housekeeping::Housekeeping(Storage& storage, SettingsStore& store, Clock& clock, Power& power, Logger& log)
    : storage_(storage), store_(store), clock_(clock), power_(power), log_(log) {}

void Housekeeping::tick(bool online) {
    const uint64_t ms = clock_.millisSinceBoot();

    checkOffline(online);

    if (ms >= nextBatteryCheckMs_) {
        nextBatteryCheckMs_ = ms + kBatteryCheckEveryMs;
        checkBattery();
    }

    // The first clear-out waits for the clock, so the bin and old messages are
    // cleared as soon as dates mean something, then once an hour.
    if (ms >= nextCleanUpMs_ && (cleanedOnce_ || clock_.isTimeSet())) {
        nextCleanUpMs_ = ms + kCleanUpEveryMs;
        cleanedOnce_ = true;
        cleanUp();
    }
}

// ---------------------------------------------------------------------------
// The bin, old messages and the storage alert

void Housekeeping::cleanUp() {
    if (!storage_.files().isMounted()) return;

    if (clock_.isTimeSet() && clock_.unixTime() >= Bin::kEarliestRealTime) {
        const int64_t now = clock_.unixTime();
        const size_t binned = storage_.bin.removeExpired(now);
        if (binned > 0) log_.info("Cleared " + std::to_string(binned) + " old recordings from the bin");
        const size_t old = removeOldMessages(now);
        if (old > 0) log_.info("Removed " + std::to_string(old) + " old messages");
    }

    checkStorage();
}

size_t Housekeeping::removeOldMessages(int64_t now) {
    const uint32_t keepDays = storage_.settings.keepDays();
    if (keepDays == 0) return 0;  // keep forever
    const int64_t cutoff = now - static_cast<int64_t>(keepDays) * kDaySeconds;

    size_t removed = 0;
    for (const auto& contact : storage_.people.contacts()) {
        for (const auto& m : storage_.messages.messages(contact.number)) {
            // Dated before the clock was ever set: real age unknown, so keep it.
            if (m.time < Bin::kEarliestRealTime) continue;
            if (m.time >= cutoff) continue;
            // Never take away something the child hasn't heard yet, or a
            // recording the outbox still needs to send.
            if (m.status == StoredMessage::Status::Unread || m.status == StoredMessage::Status::Waiting) continue;
            if (storage_.messages.remove(contact.number, m.id)) ++removed;
        }
    }
    return removed;
}

void Housekeeping::checkStorage() {
    FileStorage& files = storage_.files();
    if (!files.isMounted()) return;
    const uint64_t total = files.totalBytes();
    if (total == 0) return;
    const uint64_t used = files.usedBytes();
    const int percent = static_cast<int>(used * 100 / total);

    const bool alerted = store_.getInt(kStorageAlertKey, 0) != 0;
    if (!alerted && percent >= kStorageAlertPercent) {
        const uint64_t mb = 1000 * 1000;
        std::string text = childName() + "'s box is " + std::to_string(percent) + "% full (" +
                           text::thousands((used + mb / 2) / mb) + " MB of " + text::thousands(total / mb) +
                           " MB). ";
        const uint32_t keepDays = storage_.settings.keepDays();
        if (keepDays == 0) {
            text += "Messages are kept forever at the moment. To free up space, send /keepmessages with a number "
                    "of days, for example /keepmessages 60, and older messages will be removed from the box.";
        } else {
            text += "Messages older than " + std::to_string(keepDays) +
                    " days are removed from the box. To free up space sooner, send /keepmessages with a smaller "
                    "number of days.";
        }
        text += " They stay in Telegram on family members' phones.";
        tellAdmins(text);
        store_.setInt(kStorageAlertKey, 1);
        log_.info("Storage alert sent at " + std::to_string(percent) + "%");
    } else if (alerted && percent < kStorageRearmPercent) {
        store_.setInt(kStorageAlertKey, 0);
    }
}

// ---------------------------------------------------------------------------
// Offline alert

int64_t Housekeeping::lastOnline() const { return store_.getInt(kLastOnlineKey, 0); }

void Housekeeping::setLastOnline(int64_t unixTime) {
    store_.setInt(kLastOnlineKey, unixTime);
    lastOnlineSaved_ = unixTime;
    lastOnlineLoaded_ = true;
}

void Housekeeping::checkOffline(bool online) {
    // Without a real clock there's nothing to measure against.
    if (!online || !clock_.isTimeSet() || clock_.unixTime() < Bin::kEarliestRealTime) {
        wasOnline_ = false;
        return;
    }
    const int64_t now = clock_.unixTime();
    if (!lastOnlineLoaded_) {
        lastOnlineSaved_ = lastOnline();
        lastOnlineLoaded_ = true;
    }

    if (!wasOnline_) {
        // Just started, or just got back online.
        wasOnline_ = true;
        const int64_t last = lastOnlineSaved_;
        if (last >= Bin::kEarliestRealTime && now - last > kOfflineAlertSeconds) {
            tellAdmins(offlineText(last, now));
            log_.info("Offline alert sent: away for " + std::to_string((now - last) / 3600) + " hours");
        }
        setLastOnline(now);
        return;
    }

    // The clock going backwards (it was wrong before) also saves straight away.
    if (now - lastOnlineSaved_ >= kSaveOnlineEverySeconds || now < lastOnlineSaved_) setLastOnline(now);
}

std::string Housekeeping::formatMoment(int64_t unixTime, bool withDate) const {
    const std::string zone = storage_.settings.timezone();
    LocalTime t;
    const bool local = !zone.empty() && timezones::toLocal(zone, unixTime, t);
    if (!local) t = PosixTimeZone().toLocal(unixTime);  // UTC
    std::string out = weekdayName(t.weekday) + " ";
    if (withDate) out += std::to_string(t.day) + " " + monthName(t.month) + " ";
    out += formatHourMinute(t);
    if (!local) out += " UTC";
    return out;
}

std::string Housekeeping::offlineText(int64_t from, int64_t to) const {
    // Day names alone are only clear within a week.
    const bool withDate = to - from > 6 * kDaySeconds;
    return childName() + "'s box was offline from " + formatMoment(from, withDate) + " to " +
           formatMoment(to, withDate) + ". Voice notes sent before " +
           formatMoment(to - kOfflineAlertSeconds, withDate) +
           " may not have arrived. Ask family to resend if needed.";
}

// ---------------------------------------------------------------------------
// Low battery

void Housekeeping::checkBattery() {
    const int percent = power_.batteryPercent();
    if (percent < 0) return;  // can't be read

    const int saved = static_cast<int>(store_.getInt(kBatteryKey, 0));
    int alerted = saved;

    // Once the battery has gone well above a level again, that level can alert
    // again. Levels are highest first, so step back up one at a time.
    while (alerted != 0 && percent >= alerted + kBatteryRearmMargin) {
        int higher = 0;
        for (int level : kBatteryLevels) {
            if (level > alerted) higher = level;
        }
        alerted = higher;
    }

    const bool onMains = power_.isPluggedIn() || power_.isCharging();
    if (!onMains) {
        // The lowest level it's at or below. Switching on at 8% gives one alert
        // for 10%, not one for 20% followed by one for 10%.
        int reached = 0;
        for (int level : kBatteryLevels) {
            if (percent <= level) reached = level;
        }
        if (reached != 0 && (alerted == 0 || reached < alerted)) {
            alerted = reached;
            pendingBatteryAlert_ = reached;
            std::string text = childName() + "'s box battery is at " + std::to_string(percent) + "%. ";
            text += reached <= kBatteryLevels[2] ? "It will switch off soon unless it's charged."
                                                 : "Please charge it soon.";
            tellAdmins(text);
            log_.info("Low battery alert at " + std::to_string(percent) + "%");
        }
    }

    if (alerted != saved) store_.setInt(kBatteryKey, alerted);
}

bool Housekeeping::takeBatteryAlert(int& level) {
    if (pendingBatteryAlert_ == 0) return false;
    if (isQuietHours(storage_.settings, clock_)) return false;  // waits for quiet hours to end
    level = pendingBatteryAlert_;
    pendingBatteryAlert_ = 0;
    return true;
}

// ---------------------------------------------------------------------------

void Housekeeping::tellAdmins(const std::string& text) {
    for (const auto& admin : storage_.people.admins()) {
        Job job;
        job.kind = Job::Kind::Notice;
        job.chatId = admin.telegramId;
        job.text = text;
        if (storage_.jobs.add(job) == 0) log_.warning("Couldn't queue a message to an admin");
    }
}

std::string Housekeeping::childName() const { return storage_.settings.childName(); }

}  // namespace fvb

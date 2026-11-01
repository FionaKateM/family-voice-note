#include "app/time/TimeChecks.h"

#include <cstdio>

#include "app/time/TimeZones.h"

namespace fvb {

namespace {
constexpr uint16_t kMinutesInDay = 24 * 60;
}

bool isWithinDailyHours(uint16_t start, uint16_t end, uint16_t minute) {
    if (start >= kMinutesInDay || end >= kMinutesInDay || minute >= kMinutesInDay) return false;
    if (start == end) return false;
    if (start < end) return minute >= start && minute < end;
    return minute >= start || minute < end;  // crosses midnight
}

bool deviceLocalTime(const DeviceSettings& settings, const Clock& clock, LocalTime& out) {
    if (!clock.isTimeSet()) return false;
    return timezones::toLocal(settings.timezone(), clock.unixTime(), out);
}

bool isQuietHours(const DeviceSettings& settings, const Clock& clock) {
    LocalTime now;
    if (!deviceLocalTime(settings, clock, now)) return false;
    return isWithinDailyHours(settings.quietStart(), settings.quietEnd(), now.minuteOfDay());
}

bool isContactAsleep(const Contact& contact, int64_t unixTime) {
    LocalTime theirs;
    if (!timezones::toLocal(contact.timezone, unixTime, theirs)) return false;
    return isWithinDailyHours(contact.sleepStart, contact.sleepEnd, theirs.minuteOfDay());
}

bool isContactAsleep(const Contact& contact, const Clock& clock) {
    if (!clock.isTimeSet()) return false;
    return isContactAsleep(contact, clock.unixTime());
}

QuietHoursWatcher::Change QuietHoursWatcher::update(bool quietNow) {
    const State next = quietNow ? State::Quiet : State::Awake;
    const State before = state_;
    state_ = next;
    if (before == next) return Change::None;
    if (next == State::Quiet) return Change::Started;
    return before == State::Unknown ? Change::None : Change::Ended;
}

std::string formatHourMinute(uint16_t minuteOfDay) {
    char text[8];
    std::snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>((minuteOfDay / 60) % 24),
                  static_cast<unsigned>(minuteOfDay % 60));
    return text;
}

std::string formatHourMinute(const LocalTime& time) { return formatHourMinute(time.minuteOfDay()); }

std::string weekdayName(int weekday) {
    static const char* names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    if (weekday < 0 || weekday > 6) return "";
    return names[weekday];
}

}  // namespace fvb

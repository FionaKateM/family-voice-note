#pragma once

#include <cstdint>
#include <string>

#include "app/storage/DeviceSettings.h"
#include "app/storage/People.h"
#include "app/time/PosixTimeZone.h"
#include "hal/Clock.h"

namespace fvb {

// True if `minute` (minutes after midnight) falls in the daily window from `start`
// up to, but not including, `end`. The window can cross midnight: 19:00 to 07:00
// covers 21:00 and 03:00 but not 12:00.
//
// A window that starts and ends at the same time is empty, so setting quiet hours
// to 00:00 to 00:00 turns them off.
bool isWithinDailyHours(uint16_t start, uint16_t end, uint16_t minute);

// The box's own local time. False if the clock hasn't been set from the internet
// yet, or the box's timezone hasn't been confirmed.
bool deviceLocalTime(const DeviceSettings& settings, const Clock& clock, LocalTime& out);

// The child's quiet hours (section 5 of the requirements), in the box's timezone.
// False when the time isn't known: better to be awake by mistake than to go dark
// and silent by mistake.
bool isQuietHours(const DeviceSettings& settings, const Clock& clock);

// Whether it's within the contact's sleep hours in their own timezone, for the
// asleep picture. False when the time or their timezone isn't known.
bool isContactAsleep(const Contact& contact, const Clock& clock);
bool isContactAsleep(const Contact& contact, int64_t unixTime);

// Spots quiet hours starting and ending, so the box can turn the screen and LEDs
// off, then back on (with the unread LEDs and any missed low battery sound).
class QuietHoursWatcher {
public:
    enum class Change { None, Started, Ended };

    // Call regularly with the current answer from isQuietHours. The first call
    // reports Started if it's already quiet hours (e.g. the box was switched on
    // at night), and None otherwise.
    Change update(bool quietNow);
    bool isQuiet() const { return state_ == State::Quiet; }

private:
    enum class State { Unknown, Awake, Quiet };
    State state_ = State::Unknown;
};

// "07:05"
std::string formatHourMinute(uint16_t minuteOfDay);
std::string formatHourMinute(const LocalTime& time);
// "Friday". 0 is Sunday.
std::string weekdayName(int weekday);
// "November". 1 is January.
std::string monthName(int month);

}  // namespace fvb

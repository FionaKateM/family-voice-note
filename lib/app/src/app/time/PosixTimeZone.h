#pragma once

#include <cstdint>
#include <string>

namespace fvb {

// A date and time on someone's wall clock.
struct LocalTime {
    int year = 1970;
    int month = 1;    // 1 to 12
    int day = 1;      // 1 to 31
    int hour = 0;     // 0 to 23
    int minute = 0;   // 0 to 59
    int second = 0;   // 0 to 59
    int weekday = 4;  // 0 is Sunday. 1 January 1970 was a Thursday.
    int32_t utcOffsetSeconds = 0;  // east of Greenwich is positive, e.g. 3600 for BST
    bool daylightSaving = false;

    uint16_t minuteOfDay() const { return static_cast<uint16_t>(hour * 60 + minute); }
};

namespace civil {
// Days since 1 January 1970 for a date, and back again. Work for any year.
int64_t daysFromDate(int year, int month, int day);
void dateFromDays(int64_t days, int& year, int& month, int& day);
// 0 is Sunday.
int weekdayFromDays(int64_t days);
bool isLeapYear(int year);
int daysInMonth(int year, int month);
// Splits a time (seconds since 1970, already shifted to the wanted offset) into fields.
LocalTime split(int64_t seconds);
}  // namespace civil

// The clock rules for one place, read from a POSIX TZ string such as
// "GMT0BST,M3.5.0/1,M10.5.0" (UK: GMT, or BST from the last Sunday in March at
// 01:00 to the last Sunday in October at 02:00).
//
// The box works out local time itself instead of using the C library, because it
// needs several places at once (the box and each contact) and the ESP32's library
// only holds one at a time.
//
// Note the sign: in POSIX strings, "EST5" means five hours *behind* UTC. Everything
// this class returns uses the usual sign, so New York in winter is -18000.
class PosixTimeZone {
public:
    // False if the string isn't a rule this understands. `out` is left alone then.
    static bool parse(const std::string& rule, PosixTimeZone& out);

    // Fixed UTC (+0, no daylight saving) until parsed.
    PosixTimeZone() = default;

    bool hasDaylightSaving() const { return hasDst_; }
    int32_t standardOffsetSeconds() const { return stdOffset_; }

    // The offset from UTC in force at this moment (seconds since 1970, UTC).
    int32_t utcOffsetAt(int64_t unixTime, bool* daylightSaving = nullptr) const;
    LocalTime toLocal(int64_t unixTime) const;

private:
    struct Change {
        enum class Kind { JulianNoLeap, ZeroBasedDay, MonthWeekDay } kind = Kind::MonthWeekDay;
        int day = 0;  // Jn: 1 to 365. n: 0 to 365.
        int month = 0, week = 0, weekday = 0;  // Mm.w.d
        int32_t time = 2 * 3600;  // seconds after local midnight; can be negative or over 24 hours
    };

    static bool parseChange(const char*& p, Change& out);
    // The moment (UTC) this change happens in the given year.
    static int64_t changeUtc(const Change& change, int year, int32_t offsetBefore);

    int32_t stdOffset_ = 0;  // seconds east of UTC
    int32_t dstOffset_ = 0;
    bool hasDst_ = false;
    Change start_, end_;
};

}  // namespace fvb

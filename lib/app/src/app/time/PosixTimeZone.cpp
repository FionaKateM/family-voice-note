#include "app/time/PosixTimeZone.h"

#include <cctype>

namespace fvb {

namespace {

constexpr int64_t kSecondsPerDay = 86400;

int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

bool readNumber(const char*& p, int& out, int maxDigits = 3) {
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    int value = 0;
    int digits = 0;
    while (std::isdigit(static_cast<unsigned char>(*p)) && digits < maxDigits) {
        value = value * 10 + (*p - '0');
        ++p;
        ++digits;
    }
    out = value;
    return true;
}

// A zone abbreviation: "GMT", or anything in angle brackets like "<+0530>".
bool readName(const char*& p) {
    if (*p == '<') {
        ++p;
        while (*p && *p != '>') ++p;
        if (*p != '>') return false;
        ++p;
        return true;
    }
    const char* start = p;
    while (std::isalpha(static_cast<unsigned char>(*p))) ++p;
    return p - start >= 3;
}

// [+|-]hh[:mm[:ss]], allowing up to 167 hours as POSIX extensions do.
bool readTime(const char*& p, int32_t& seconds) {
    int sign = 1;
    if (*p == '+' || *p == '-') {
        if (*p == '-') sign = -1;
        ++p;
    }
    int h = 0, m = 0, s = 0;
    if (!readNumber(p, h) || h > 167) return false;
    if (*p == ':') {
        ++p;
        if (!readNumber(p, m, 2) || m > 59) return false;
        if (*p == ':') {
            ++p;
            if (!readNumber(p, s, 2) || s > 59) return false;
        }
    }
    seconds = sign * (h * 3600 + m * 60 + s);
    return true;
}

}  // namespace

namespace civil {

// Howard Hinnant's date algorithms (public domain).
int64_t daysFromDate(int year, int month, int day) {
    const int64_t y = static_cast<int64_t>(year) - (month <= 2 ? 1 : 0);
    const int64_t era = floorDiv(y, 400);
    const int64_t yoe = y - era * 400;
    const int64_t mp = (month + 9) % 12;
    const int64_t doy = (153 * mp + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void dateFromDays(int64_t days, int& year, int& month, int& day) {
    const int64_t z = days + 719468;
    const int64_t era = floorDiv(z, 146097);
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

int weekdayFromDays(int64_t days) {
    const int64_t w = (days + 4) % 7;  // 1970-01-01 was a Thursday
    return static_cast<int>(w < 0 ? w + 7 : w);
}

bool isLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) return 29;
    return days[month - 1];
}

LocalTime split(int64_t seconds) {
    LocalTime t;
    const int64_t days = floorDiv(seconds, kSecondsPerDay);
    const int64_t rest = seconds - days * kSecondsPerDay;
    dateFromDays(days, t.year, t.month, t.day);
    t.weekday = weekdayFromDays(days);
    t.hour = static_cast<int>(rest / 3600);
    t.minute = static_cast<int>((rest % 3600) / 60);
    t.second = static_cast<int>(rest % 60);
    return t;
}

}  // namespace civil

bool PosixTimeZone::parseChange(const char*& p, Change& out) {
    Change c;
    if (*p == 'M') {
        ++p;
        c.kind = Change::Kind::MonthWeekDay;
        if (!readNumber(p, c.month, 2) || c.month < 1 || c.month > 12 || *p++ != '.') return false;
        if (!readNumber(p, c.week, 1) || c.week < 1 || c.week > 5 || *p++ != '.') return false;
        if (!readNumber(p, c.weekday, 1) || c.weekday > 6) return false;
    } else if (*p == 'J') {
        ++p;
        c.kind = Change::Kind::JulianNoLeap;
        if (!readNumber(p, c.day) || c.day < 1 || c.day > 365) return false;
    } else {
        c.kind = Change::Kind::ZeroBasedDay;
        if (!readNumber(p, c.day) || c.day > 365) return false;
    }
    if (*p == '/') {
        ++p;
        if (!readTime(p, c.time)) return false;
    }
    out = c;
    return true;
}

bool PosixTimeZone::parse(const std::string& rule, PosixTimeZone& out) {
    PosixTimeZone z;
    const char* p = rule.c_str();
    int32_t posixStd = 0;
    if (!readName(p) || !readTime(p, posixStd)) return false;
    z.stdOffset_ = -posixStd;
    z.dstOffset_ = z.stdOffset_;
    if (*p == '\0') {
        out = z;
        return true;
    }

    if (!readName(p)) return false;
    z.hasDst_ = true;
    z.dstOffset_ = z.stdOffset_ + 3600;
    if (*p != ',' && *p != '\0') {
        int32_t posixDst = 0;
        if (!readTime(p, posixDst)) return false;
        z.dstOffset_ = -posixDst;
    }
    // The rules are optional in POSIX, but every tzdata rule has them, and guessing
    // them would be wrong for most of the world.
    if (*p++ != ',' || !parseChange(p, z.start_)) return false;
    if (*p++ != ',' || !parseChange(p, z.end_)) return false;
    if (*p != '\0') return false;
    out = z;
    return true;
}

int64_t PosixTimeZone::changeUtc(const Change& c, int year, int32_t offsetBefore) {
    int64_t day = civil::daysFromDate(year, 1, 1);
    switch (c.kind) {
        case Change::Kind::JulianNoLeap:
            day += c.day - 1;
            if (civil::isLeapYear(year) && c.day >= 60) ++day;  // 29 February isn't counted
            break;
        case Change::Kind::ZeroBasedDay:
            day += c.day;
            break;
        case Change::Kind::MonthWeekDay: {
            const int64_t first = civil::daysFromDate(year, c.month, 1);
            int date = 1 + (c.weekday - civil::weekdayFromDays(first) + 7) % 7 + (c.week - 1) * 7;
            while (date > civil::daysInMonth(year, c.month)) date -= 7;  // week 5 means "the last"
            day = first + date - 1;
            break;
        }
    }
    return day * kSecondsPerDay + c.time - offsetBefore;
}

int32_t PosixTimeZone::utcOffsetAt(int64_t unixTime, bool* daylightSaving) const {
    bool dst = false;
    if (hasDst_) {
        int year, month, day;
        civil::dateFromDays(floorDiv(unixTime + stdOffset_, kSecondsPerDay), year, month, day);
        const int64_t start = changeUtc(start_, year, stdOffset_);
        const int64_t end = changeUtc(end_, year, dstOffset_);
        if (start < end) {
            dst = unixTime >= start && unixTime < end;
        } else {
            // Southern hemisphere: daylight saving runs across new year.
            dst = unixTime < end || unixTime >= start;
        }
    }
    if (daylightSaving) *daylightSaving = dst;
    return dst ? dstOffset_ : stdOffset_;
}

LocalTime PosixTimeZone::toLocal(int64_t unixTime) const {
    bool dst = false;
    const int32_t offset = utcOffsetAt(unixTime, &dst);
    LocalTime t = civil::split(unixTime + offset);
    t.utcOffsetSeconds = offset;
    t.daylightSaving = dst;
    return t;
}

}  // namespace fvb

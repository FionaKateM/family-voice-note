#include "app/storage/Paths.h"

#include <cstdio>

namespace fvb {
namespace paths {

std::string contactDir(uint32_t contact) { return std::string(kContactsDir) + "/" + std::to_string(contact); }
std::string contactIndex(uint32_t contact) { return contactDir(contact) + "/index.txt"; }
std::string contactPhoto(uint32_t contact) { return contactDir(contact) + "/photo.jpg"; }
std::string contactFile(uint32_t contact, const std::string& fileName) {
    return contactDir(contact) + "/" + fileName;
}

}  // namespace paths

namespace {

// Days since 1970-01-01 to a calendar date, and back. From Howard Hinnant's
// public domain date algorithms. Works for any date, no time library needed.
void civilFromDays(int64_t z, int& year, unsigned& month, unsigned& day) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(y + (month <= 2));
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

}  // namespace

std::string formatCompactTime(int64_t unixSeconds) {
    int64_t days = unixSeconds / 86400;
    int64_t rest = unixSeconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    int year;
    unsigned month, day;
    civilFromDays(days, year, month, day);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02u%02u-%02d%02d%02d", year, month, day, static_cast<int>(rest / 3600),
                  static_cast<int>(rest / 60 % 60), static_cast<int>(rest % 60));
    return buf;
}

bool parseCompactTime(const std::string& text, int64_t& unixSeconds) {
    if (text.size() != 15 || text[8] != '-') return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i != 8 && (text[i] < '0' || text[i] > '9')) return false;
    }
    auto num = [&](size_t pos, size_t len) {
        int value = 0;
        for (size_t i = pos; i < pos + len; ++i) value = value * 10 + (text[i] - '0');
        return value;
    };
    const int year = num(0, 4), month = num(4, 2), day = num(6, 2);
    const int hour = num(9, 2), minute = num(11, 2), second = num(13, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return false;
    unixSeconds = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
                  hour * 3600 + minute * 60 + second;
    return true;
}

}  // namespace fvb

#include "app/storage/TextRecord.h"

#include <cerrno>
#include <cstdlib>

namespace fvb {
namespace text {

std::string escape(const std::string& field) {
    std::string out;
    out.reserve(field.size());
    for (char c : field) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out;
}

std::string unescape(const std::string& field) {
    std::string out;
    out.reserve(field.size());
    for (size_t i = 0; i < field.size(); ++i) {
        const char c = field[i];
        if (c != '\\' || i + 1 == field.size()) {
            out += c;
            continue;
        }
        const char next = field[++i];
        switch (next) {
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            default: out += next;  // covers "\\" and anything unexpected
        }
    }
    return out;
}

std::string joinFields(const std::vector<std::string>& fields) {
    std::string line;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) line += '\t';
        line += escape(fields[i]);
    }
    return line;
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(unescape(line.substr(start)));
            break;
        }
        fields.push_back(unescape(line.substr(start, tab - start)));
        start = tab + 1;
    }
    return fields;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

bool parseInt(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    const char* begin = s.c_str();
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(begin, &end, 10);
    if (errno != 0 || end != begin + s.size()) return false;
    // strtoll skips leading spaces; we don't want that.
    if (s[0] == ' ' || s[0] == '\t' || s[0] == '+') return false;
    out = value;
    return true;
}

bool parseUint(const std::string& s, uint64_t& out) {
    if (s.empty() || s[0] < '0' || s[0] > '9') return false;
    const char* begin = s.c_str();
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(begin, &end, 10);
    if (errno != 0 || end != begin + s.size()) return false;
    out = value;
    return true;
}

std::string trim(const std::string& s) {
    const char* space = " \t\r\n";
    const size_t first = s.find_first_not_of(space);
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(space);
    return s.substr(first, last - first + 1);
}

}  // namespace text
}  // namespace fvb

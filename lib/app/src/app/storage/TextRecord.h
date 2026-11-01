#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fvb {
namespace text {

// Everything the box stores (settings, people, index files, outbox entries) uses
// the same simple format: one record per line, fields separated by tabs.
//
// Tabs, newlines and backslashes inside a field are written as \t, \n and \\,
// so a name or Wi-Fi password can contain anything and still round-trip.
//
// Why not JSON: index files are read line by line, a half-written last line
// (power cut) can be skipped without losing the rest, and it needs no library.

std::string escape(const std::string& field);
std::string unescape(const std::string& field);

// Joins fields into one line (no newline on the end).
std::string joinFields(const std::vector<std::string>& fields);
// Splits one line back into fields. An empty line gives one empty field.
std::vector<std::string> splitFields(const std::string& line);

// Splits text into lines. Handles \n and \r\n. Drops a trailing empty line.
std::vector<std::string> splitLines(const std::string& text);

// Strict number parsing: the whole string must be a number.
bool parseInt(const std::string& s, int64_t& out);
bool parseUint(const std::string& s, uint64_t& out);

// Removes spaces, tabs, \r and \n from both ends.
std::string trim(const std::string& s);

}  // namespace text
}  // namespace fvb

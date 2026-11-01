#include "app/storage/SetupFile.h"

#include <cctype>

#include "app/storage/Paths.h"
#include "app/storage/TextRecord.h"

namespace fvb {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool parseAdminIds(const std::string& value, std::vector<int64_t>& out) {
    out.clear();
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(',', start);
        if (comma == std::string::npos) comma = value.size();
        int64_t id;
        if (!text::parseInt(text::trim(value.substr(start, comma - start)), id) || id <= 0) return false;
        out.push_back(id);
        start = comma + 1;
    }
    return !out.empty();
}

}  // namespace

bool looksLikeBotToken(const std::string& token) {
    const size_t colon = token.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    for (size_t i = 0; i < colon; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
    }
    const std::string secret = token.substr(colon + 1);
    if (secret.size() < 30) return false;
    for (char c : secret) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    }
    return true;
}

bool parseSetupText(const std::string& raw, SetupValues& out, std::vector<std::string>& problems,
                    std::vector<std::string>& warnings) {
    out = SetupValues{};
    std::string contents = raw;
    if (contents.compare(0, 3, "\xEF\xBB\xBF") == 0) contents.erase(0, 3);  // byte order mark

    std::vector<std::string> seen;
    int lineNumber = 0;
    for (const auto& rawLine : text::splitLines(contents)) {
        ++lineNumber;
        const std::string line = text::trim(rawLine);
        if (line.empty() || line[0] == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            problems.push_back("Line " + std::to_string(lineNumber) + " should look like NAME=value");
            continue;
        }
        const std::string key = upper(text::trim(line.substr(0, eq)));
        const std::string value = stripQuotes(text::trim(line.substr(eq + 1)));

        for (const auto& k : seen) {
            if (k == key) warnings.push_back(key + " appears more than once. Using the last one.");
        }
        seen.push_back(key);

        if (key == "WIFI_SSID") {
            out.wifiSsid = value;
        } else if (key == "WIFI_PASSWORD") {
            out.wifiPassword = value;
            out.hasWifiPassword = true;
        } else if (key == "BOT_TOKEN") {
            out.botToken = value;
        } else if (key == "CHILD_NAME") {
            out.childName = value;
        } else if (key == "ADMIN_ID") {
            if (!parseAdminIds(value, out.adminIds)) {
                problems.push_back("ADMIN_ID should be a Telegram user ID (a number), or several separated by commas");
            }
        } else {
            warnings.push_back("Ignoring unknown setting " + key);
        }
    }

    if (!out.botToken.empty() && !looksLikeBotToken(out.botToken)) {
        problems.push_back("BOT_TOKEN doesn't look like a Telegram bot token");
    }
    if (out.hasWifiPassword && !out.wifiPassword.empty() && out.wifiSsid.empty()) {
        problems.push_back("WIFI_PASSWORD is set but WIFI_SSID is missing");
    }
    return problems.empty();
}

SetupResult applySetupFile(FileStorage& files, DeviceSettings& settings, People& people) {
    SetupResult result;
    std::string contents;
    if (!files.readFile(paths::kSetupFile, contents)) return result;

    SetupValues values;
    parseSetupText(contents, values, result.problems, result.warnings);

    // The very first setup needs enough to get the bot going.
    if (!settings.hasBotToken() && values.botToken.empty()) result.problems.push_back("BOT_TOKEN is missing");
    if (people.admins().empty() && values.adminIds.empty()) result.problems.push_back("ADMIN_ID is missing");
    if (!settings.hasBotToken() && values.childName.empty()) result.problems.push_back("CHILD_NAME is missing");

    if (!result.problems.empty()) {
        result.outcome = SetupResult::Outcome::Rejected;
        return result;
    }

    bool saved = true;
    if (!values.botToken.empty()) saved &= settings.setBotToken(values.botToken);
    if (!values.childName.empty()) saved &= settings.setChildName(values.childName);
    if (!values.wifiSsid.empty()) saved &= settings.addWifiNetwork({values.wifiSsid, values.wifiPassword});
    for (int64_t id : values.adminIds) saved &= people.addAdmin(Person{id, ""});

    if (!saved) {
        // Keep the file so nothing is lost; the next start will try again.
        result.problems.push_back("Couldn't save the settings to internal memory");
        result.outcome = SetupResult::Outcome::Rejected;
        return result;
    }

    // Deleting a file on an SD card leaves its contents readable with recovery
    // tools, so blank it out first.
    files.writeFile(paths::kSetupFile, std::string(contents.size(), ' '));
    result.fileDeleted = files.removeFile(paths::kSetupFile);
    if (!result.fileDeleted) result.warnings.push_back("Saved the settings but couldn't delete setup.txt");
    result.outcome = SetupResult::Outcome::Applied;
    return result;
}

}  // namespace fvb

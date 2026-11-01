#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/storage/DeviceSettings.h"
#include "app/storage/People.h"
#include "hal/FileStorage.h"

namespace fvb {

// What was found in a setup.txt. Empty strings / empty lists mean "not in the file".
struct SetupValues {
    std::string wifiSsid;
    std::string wifiPassword;
    bool hasWifiPassword = false;
    std::string botToken;
    std::string childName;
    std::vector<int64_t> adminIds;
};

// Reads the text of a setup.txt. Forgiving about the things people's text editors
// do: Windows line endings, a byte order mark, spaces around "=", lower-case names,
// quotes around values, # comment lines.
//
// Problems stop the file being used. Warnings don't. Neither ever includes the
// value of the bot token or Wi-Fi password, so both are safe to log or show.
bool parseSetupText(const std::string& text, SetupValues& out, std::vector<std::string>& problems,
                    std::vector<std::string>& warnings);

// True if it looks like a Telegram bot token: digits, a colon, then at least 30
// letters, digits, "-" or "_".
bool looksLikeBotToken(const std::string& token);

struct SetupResult {
    enum class Outcome {
        NoFile,   // no setup.txt on the card: nothing to do
        Applied,  // values saved and the file deleted
        Rejected  // file has problems: nothing saved, file left on the card to be fixed
    };
    Outcome outcome = Outcome::NoFile;
    std::vector<std::string> problems;
    std::vector<std::string> warnings;
    bool fileDeleted = false;
};

// Looks for /setup.txt on the SD card. If it's there and valid, saves its values
// to internal memory, then overwrites the file with blanks and deletes it, so the
// bot token isn't left on the card.
//
// The first time (no bot token saved yet) BOT_TOKEN, CHILD_NAME and ADMIN_ID are
// all needed. After that, a new setup.txt only needs the lines being changed:
// a new Wi-Fi network is added to the known list, a new admin is added to the
// admin list, and a new token or child's name replaces the old one.
SetupResult applySetupFile(FileStorage& files, DeviceSettings& settings, People& people);

}  // namespace fvb

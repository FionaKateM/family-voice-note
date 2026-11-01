#include "platform/desktop/TryTelegram.h"

#include <fstream>
#include <random>
#include <string>

#include "platform/desktop/DesktopBasics.h"
#include "platform/desktop/DesktopFileStorage.h"
#include "platform/desktop/DesktopNetwork.h"
#include "telegram/TelegramClient.h"
#include "telegram/TryOut.h"

namespace fvb {

namespace {

// Just enough to pull BOT_TOKEN out of a setup file. The box's real setup file
// handling is part of ticket 1.2.
std::string readBotToken(const std::filesystem::path& setupFile) {
    std::ifstream in(setupFile);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("BOT_TOKEN=", 0) != 0) continue;
        std::string value = line.substr(10);
        const char* space = " \t\r\n";
        size_t first = value.find_first_not_of(space);
        size_t last = value.find_last_not_of(space);
        return first == std::string::npos ? "" : value.substr(first, last - first + 1);
    }
    return "";
}

std::string randomCode() {
    static const char letters[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::uniform_int_distribution<size_t> pick(0, sizeof(letters) - 2);
    std::string code;
    for (int i = 0; i < 16; ++i) code += letters[pick(rd)];
    return code;
}

}  // namespace

int runTelegramTryOut(const std::filesystem::path& setupFile, const std::filesystem::path& dataFolder) {
    DesktopLogger log;
    const std::string token = readBotToken(setupFile);
    if (token.empty() || token.find("FAKE") != std::string::npos) {
        log.error("No bot token found. Put a BOT_TOKEN= line in " + setupFile.string() +
                  " (copy secrets/setup.example.txt to start).");
        return 1;
    }

    DesktopNetwork network;
    DesktopFileStorage files(dataFolder / "sdcard");
    DesktopClock clock;
    telegram::TelegramClient client(network, files, log, token);
    telegram::TryOut tryOut(client, log, randomCode());

    if (!tryOut.start()) return 1;
    log.info("Waiting for messages. Press Ctrl+C to stop.");
    for (;;) {
        uint32_t waitMs = tryOut.step();
        if (waitMs > 0) clock.sleepMs(waitMs);
    }
}

}  // namespace fvb

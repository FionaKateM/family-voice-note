#include "platform/desktop/TryTelegram.h"

#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "app/storage/Storage.h"
#include "platform/desktop/DesktopBasics.h"
#include "platform/desktop/DesktopFileStorage.h"
#include "platform/desktop/DesktopNetwork.h"
#include "platform/desktop/DesktopSettingsStore.h"
#include "telegram/TelegramClient.h"
#include "telegram/TryOut.h"

namespace fvb {

namespace {

std::string randomCode() {
    static const char letters[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::uniform_int_distribution<size_t> pick(0, sizeof(letters) - 2);
    std::string code;
    for (int i = 0; i < 16; ++i) code += letters[pick(rd)];
    return code;
}

// Puts the committed test tone on the pretend SD card for /mp3.
// The path is relative to where the program is run from (the repo folder).
void copyTestTone(DesktopFileStorage& files, const std::string& sdPath, Logger& log) {
    const char* source = "assets/test-audio/test-tone.mp3";
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        log.warning(std::string("Couldn't find ") + source + ", so /mp3 won't work. Run from the repo folder.");
        return;
    }
    const std::string audio((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!files.writeFile(sdPath, audio)) log.warning("Couldn't copy the test MP3 to the pretend SD card");
}

}  // namespace

int runTelegramTryOut(const std::filesystem::path& dataFolder) {
    DesktopLogger log;
    DesktopFileStorage files(dataFolder / "sdcard");
    DesktopSettingsStore store(dataFolder / "nvs");
    Storage storage(files, store);

    // Same setup file handling as the box itself (ticket 1.2).
    const SetupResult setup = storage.applySetupFile();
    for (const auto& p : setup.problems) log.error("setup.txt: " + p);
    if (setup.outcome == SetupResult::Outcome::Rejected) return 1;

    const std::string token = storage.settings.botToken();
    if (token.empty()) {
        log.error("No bot token saved yet. Run once with --setup secrets/setup.txt "
                  "(copy secrets/setup.example.txt to start).");
        return 1;
    }

    DesktopNetwork network;
    DesktopClock clock;
    telegram::TelegramClient client(network, files, log, token);
    telegram::TryOut tryOut(client, log, randomCode());
    copyTestTone(files, tryOut.testTonePath(), log);

    if (!tryOut.start()) return 1;
    log.info("Waiting for messages. Press Ctrl+C to stop.");
    for (;;) {
        uint32_t waitMs = tryOut.step();
        if (waitMs > 0) clock.sleepMs(waitMs);
    }
}

}  // namespace fvb

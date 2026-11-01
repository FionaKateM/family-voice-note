// Entry point for the laptop version of the box.
//
//   .pio/build/desktop/program                 run until Ctrl+C
//   .pio/build/desktop/program --data <folder> use a different data folder
//   .pio/build/desktop/program --ticks <n>     stop after n loops (used by CI)
//   .pio/build/desktop/program --try-telegram  run a test bot against real Telegram,
//                                              using BOT_TOKEN from secrets/setup.txt
//
// The data folder (default "desktop-data") holds:
//   sdcard/  stands in for the SD card
//   nvs/     stands in for the ESP32's internal memory

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "app/App.h"
#include "platform/desktop/DesktopBasics.h"
#include "platform/desktop/DesktopFileStorage.h"
#include "platform/desktop/DesktopNetwork.h"
#include "platform/desktop/DesktopSettingsStore.h"
#include "platform/desktop/TryTelegram.h"

namespace {

volatile std::sig_atomic_t stopRequested = 0;

void onSignal(int) { stopRequested = 1; }

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path dataFolder = "desktop-data";
    long maxTicks = -1;
    bool tryTelegram = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            dataFolder = argv[++i];
        } else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            maxTicks = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--try-telegram") == 0) {
            tryTelegram = true;
        } else {
            std::fprintf(stderr, "Usage: %s [--data <folder>] [--ticks <n>] [--try-telegram]\n", argv[0]);
            return 2;
        }
    }

    if (tryTelegram) return fvb::runTelegramTryOut("secrets/setup.txt", dataFolder);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    fvb::DesktopPlaceholderAudio audio;
    fvb::DesktopTerminalDisplay display;
    fvb::DesktopFileStorage files(dataFolder / "sdcard");
    fvb::DesktopSettingsStore settings(dataFolder / "nvs");
    fvb::DesktopNetwork network;
    fvb::DesktopClock clock;
    fvb::DesktopLeds leds;
    fvb::DesktopPower power;
    fvb::DesktopPlaceholderInput input;
    fvb::DesktopLogger logger;

    fvb::Hardware hardware{audio, display, files, settings, network, clock, leds, power, input, logger};
    fvb::App app(hardware);

    app.begin();
    while (!stopRequested && (maxTicks < 0 || static_cast<long>(app.tickCount()) < maxTicks)) {
        app.tick();
        clock.sleepMs(10);
    }
    logger.info("Stopped after " + std::to_string(app.tickCount()) + " loops");
    return 0;
}

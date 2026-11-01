// Entry point for the laptop version of the box.
//
//   .pio/build/desktop/program                 run until Ctrl+C (press it twice to quit
//                                              without waiting). Once a bot token has been
//                                              saved, the box answers on Telegram
//   .pio/build/desktop/program --data <folder> use a different data folder
//   .pio/build/desktop/program --ticks <n>     stop after n loops (used by CI)
//   .pio/build/desktop/program --setup <file>  copy a setup file onto the pretend SD card
//                                              first, e.g. --setup secrets/setup.txt
//   .pio/build/desktop/program --scale <n>     make the window n times the real screen size
//                                              (default 2)
//   .pio/build/desktop/program --try-telegram  run a test bot against real Telegram
//                                              instead of the box (see docs/telegram.md)
//
// The box's screen opens in a window. Keys 1, 2 and 3 (or the left, down and
// right arrows) are the three buttons. Click the screen to tap it, drag across
// it to swipe. Closing the window stops the box. Sound needs ffmpeg installed.
//
// --setup copies the file rather than moving it, because the box deletes setup.txt
// from its card once read. Your original stays where it is.
//
// The data folder (default "desktop-data") holds:
//   sdcard/  stands in for the SD card
//   nvs/     stands in for the ESP32's internal memory

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "app/App.h"
#include "app/bot/Bot.h"
#include "platform/desktop/DesktopAudio.h"
#include "platform/desktop/DesktopBasics.h"
#include "platform/desktop/DesktopFileStorage.h"
#include "platform/desktop/DesktopNetwork.h"
#include "platform/desktop/DesktopSettingsStore.h"
#include "platform/desktop/DesktopWindow.h"
#include "platform/desktop/PixelCanvas.h"
#include "platform/desktop/StopRequest.h"
#include "platform/desktop/TryTelegram.h"
#include "telegram/TelegramClient.h"

int main(int argc, char** argv) {
    std::filesystem::path dataFolder = "desktop-data";
    long maxTicks = -1;
    std::filesystem::path setupFile;
    bool tryTelegram = false;
    int windowScale = 2;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            dataFolder = argv[++i];
        } else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            maxTicks = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--setup") == 0 && i + 1 < argc) {
            setupFile = argv[++i];
        } else if (std::strcmp(argv[i], "--try-telegram") == 0) {
            tryTelegram = true;
        } else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            windowScale = std::atoi(argv[++i]);
            if (windowScale < 1 || windowScale > 6) windowScale = 2;
        } else {
            std::fprintf(stderr,
                         "Usage: %s [--data <folder>] [--ticks <n>] [--setup <file>] [--scale <n>] [--try-telegram]\n",
                         argv[0]);
            return 2;
        }
    }

    fvb::installStopHandler();

    fvb::DesktopFileStorage files(dataFolder / "sdcard");
    fvb::DesktopSettingsStore settings(dataFolder / "nvs");
    fvb::DesktopNetwork network;
    fvb::DesktopClock clock;
    fvb::DesktopLeds leds;
    fvb::DesktopPower power;
    fvb::DesktopLogger logger;

    if (!setupFile.empty()) {
        std::error_code ec;
        std::filesystem::copy_file(setupFile, dataFolder / "sdcard" / "setup.txt",
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "Couldn't copy %s: %s\n", setupFile.string().c_str(), ec.message().c_str());
            return 2;
        }
    }

    if (tryTelegram) return fvb::runTelegramTryOut(dataFolder);

    // The screen, buttons and LEDs, in a window.
    fvb::PixelCanvas display;
    fvb::DesktopWindow window(display, leds);
    std::string windowError;
    if (!window.open(windowScale, windowError)) {
        std::fprintf(stderr, "Couldn't open the window: %s\n", windowError.c_str());
        return 1;
    }
    fvb::DesktopWindowInput input(window);
    fvb::DesktopAudio audio(dataFolder / "sdcard", logger);
    audio.begin();  // if sound doesn't work, the box carries on without it

    fvb::Hardware hardware{audio, display, files, settings, network, clock, leds, power, input, logger};
    fvb::App app(hardware);

    app.begin();

    // Telegram runs on its own thread, the way the device will run network work
    // on its own core (ticket 2.3). Waiting for messages happens without the lock;
    // anything that touches storage or sends replies holds it, and so does tick().
    std::mutex boxLock;
    std::thread telegramThread;
    fvb::telegram::TelegramClient telegramClient(network, files, logger, app.storage().settings.botToken());
    fvb::Bot bot(telegramClient, app.storage(), settings, clock, logger);

    // Ctrl+C abandons a request only while connecting or waiting for new
    // messages. A reply that's being sent is let finish, so a message is never
    // marked as handled with its answer cut off.
    std::atomic<bool> telegramWaiting{false};
    network.setCancelCheck([&telegramWaiting] { return fvb::stopRequested() && telegramWaiting.load(); });

    if (app.storage().settings.hasBotToken()) {
        telegramThread = std::thread([&] {
            while (!fvb::stopRequested() && bot.state() != fvb::Bot::State::TokenRejected) {
                uint32_t waitMs;
                if (bot.state() == fvb::Bot::State::NotConnected) {
                    telegramWaiting = true;
                    waitMs = bot.connect();
                    telegramWaiting = false;
                } else if (bot.state() == fvb::Bot::State::Connected) {
                    std::lock_guard<std::mutex> lock(boxLock);
                    bot.resume();
                    waitMs = 0;
                } else {
                    std::vector<fvb::telegram::Update> updates;
                    telegramWaiting = true;
                    waitMs = bot.poll(updates);
                    telegramWaiting = false;
                    // Anything just received is left for next time: Telegram
                    // sends it again because the box never confirmed it.
                    if (fvb::stopRequested()) break;
                    std::lock_guard<std::mutex> lock(boxLock);
                    bot.handle(updates);
                }
                // Wait in small pieces so Ctrl+C is noticed quickly.
                for (uint32_t waited = 0; waited < waitMs && !fvb::stopRequested(); waited += 100) {
                    clock.sleepMs(100);
                }
            }
        });
    }

    while (!fvb::stopRequested() && (maxTicks < 0 || static_cast<long>(app.tickCount()) < maxTicks)) {
        {
            std::lock_guard<std::mutex> lock(boxLock);
            app.tick();
        }
        clock.sleepMs(10);
    }

    fvb::requestStop();  // for when --ticks ran out rather than Ctrl+C
    if (telegramThread.joinable()) telegramThread.join();
    logger.info("Stopped after " + std::to_string(app.tickCount()) + " loops");
    return 0;
}

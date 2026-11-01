#pragma once

#include <filesystem>

namespace fvb {

// Runs the Telegram try-out bot (see lib/telegram/src/telegram/TryOut.h) until
// Ctrl+C. Reads the bot token from the BOT_TOKEN line of `setupFile`, and saves
// downloads under `dataFolder`/sdcard/tryout. Returns the program's exit code.
int runTelegramTryOut(const std::filesystem::path& setupFile, const std::filesystem::path& dataFolder);

}  // namespace fvb

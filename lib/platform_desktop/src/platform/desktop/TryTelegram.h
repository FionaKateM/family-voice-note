#pragma once

#include <filesystem>

namespace fvb {

// Runs the Telegram try-out bot (see lib/telegram/src/telegram/TryOut.h) until
// Ctrl+C, instead of the box. Uses the bot token the box has saved, reading
// setup.txt from the pretend SD card first if one is there (so it works with
// --setup). Downloads go under `dataFolder`/sdcard/tryout. Returns the
// program's exit code.
int runTelegramTryOut(const std::filesystem::path& dataFolder);

}  // namespace fvb

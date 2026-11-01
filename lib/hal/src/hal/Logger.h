#pragma once

#include <string>

namespace fvb {

// Developer logging: the terminal on desktop, the USB serial port on the device.
// Never log bot tokens, Wi-Fi passwords or people's Telegram IDs.
class Logger {
public:
    enum class Level { Debug, Info, Warning, Error };

    virtual ~Logger() = default;
    virtual void log(Level level, const std::string& message) = 0;

    void debug(const std::string& m) { log(Level::Debug, m); }
    void info(const std::string& m) { log(Level::Info, m); }
    void warning(const std::string& m) { log(Level::Warning, m); }
    void error(const std::string& m) { log(Level::Error, m); }
};

}  // namespace fvb

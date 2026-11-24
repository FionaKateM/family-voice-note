// Hardware check for the Core2 (ticket 2.1).
//
//   pio run -e core2-check -t upload
//
// Shows a list on screen of each part of the device and whether it works:
// which Core2 it is, the SD card, internal memory, the setup file, Wi-Fi, the
// clock, Telegram and the battery. Then touch the screen and the three buttons
// to check them: the bottom rows show what was pressed, and the LEDs change
// colour.
//
// It reads setup.txt the same way the real firmware does, so the Wi-Fi and bot
// token are saved and the file is deleted. Installing the real firmware
// afterwards (pio run -e core2 -t upload) keeps them.
//
// Everything shown is also written to the USB serial port (pio device monitor).
// Nothing secret is shown or logged: no token, no Wi-Fi password, no IDs.

#include <M5Unified.h>

#include <ctime>
#include <string>

#include "app/Version.h"
#include "app/storage/Storage.h"
#include "app/storage/TextRecord.h"
#include "platform/core2/Core2Hardware.h"
#include "telegram/TelegramClient.h"

namespace {

constexpr uint32_t kWifiTryMs = 15000;
constexpr uint32_t kClockWaitMs = 15000;

fvb::Core2Display display;
fvb::Core2FileStorage files;
fvb::Core2SettingsStore settings;
fvb::Core2Network network;
fvb::Core2Clock clock_;
fvb::Core2Leds leds;
fvb::Core2Power power;
fvb::Core2Input input;
fvb::Core2SerialLogger logger;
fvb::Storage storage(files, settings);

const fvb::Colour kWhite{255, 255, 255};
const fvb::Colour kGrey{150, 150, 150};
const fvb::Colour kGood{60, 200, 60};
const fvb::Colour kBad{240, 60, 60};
const fvb::Colour kWaiting{240, 170, 0};

enum Row { Model, SdCard, Memory, Setup, Wifi, Clock, Telegram, Battery, Touch, Buttons, kRowCount };
const char* const kLabels[kRowCount] = {"Model",  "SD card", "Memory",  "Setup", "Wi-Fi",
                                        "Clock",  "Telegram", "Battery", "Touch", "Buttons"};
constexpr int16_t kRowHeight = 24;
constexpr int16_t kValueX = 112;

// About as many characters as fit beside the labels at text size 2.
constexpr size_t kMaxValueChars = 17;

void showRow(Row row, const std::string& text, fvb::Colour colour) {
    const std::string value = text.size() > kMaxValueChars ? text.substr(0, kMaxValueChars - 2) + ".." : text;
    const int16_t y = static_cast<int16_t>(row * kRowHeight);
    display.fillRect(kValueX, y, static_cast<int16_t>(display.width() - kValueX), kRowHeight, fvb::Colour{});
    display.drawText(kValueX, static_cast<int16_t>(y + 4), value, colour);
    logger.info(std::string(kLabels[row]) + ": " + text);
}

void drawLabels() {
    display.clear();
    for (int i = 0; i < kRowCount; ++i) {
        display.drawText(4, static_cast<int16_t>(i * kRowHeight + 4), kLabels[i], kGrey);
    }
}

void checkSdCard() {
    if (!files.begin()) {
        showRow(SdCard, "not found", kBad);
        return;
    }
    const uint64_t mb = 1000ULL * 1000ULL;
    const bool canWrite = files.writeFile("/check/test.txt", "hello") && files.removeDir("/check");
    showRow(SdCard,
            fvb::text::thousands(files.totalBytes() / mb) + " MB" + (canWrite ? "" : ", can't write"),
            canWrite ? kGood : kBad);
}

void checkMemory() {
    const bool ok = settings.begin() && settings.setInt("chk", 42) && settings.getInt("chk") == 42 &&
                    settings.remove("chk");
    showRow(Memory, ok ? "OK" : "not working", ok ? kGood : kBad);
}

void readSetupFile() {
    if (!files.isMounted()) {
        showRow(Setup, storage.settings.hasBotToken() ? "saved earlier" : "no SD card", kWaiting);
        return;
    }
    const fvb::SetupResult result = storage.applySetupFile();
    for (const auto& w : result.warnings) logger.warning("setup.txt: " + w);
    for (const auto& p : result.problems) logger.error("setup.txt: " + p);
    switch (result.outcome) {
        case fvb::SetupResult::Outcome::Applied:
            showRow(Setup, result.fileDeleted ? "read and saved" : "saved, not deleted", kGood);
            break;
        case fvb::SetupResult::Outcome::Rejected:
            showRow(Setup, "has problems", kBad);  // the details are on the serial port
            break;
        case fvb::SetupResult::Outcome::NoFile:
            if (storage.settings.hasBotToken()) showRow(Setup, "saved earlier", kGood);
            else showRow(Setup, "no setup.txt", kBad);
            break;
    }
}

void joinWifi() {
    const auto known = storage.settings.wifiNetworks();
    if (known.empty()) {
        showRow(Wifi, "none saved", kBad);
        return;
    }
    for (const auto& wifi : known) {
        showRow(Wifi, "trying " + wifi.ssid, kWaiting);
        if (fvb::core2JoinWifi(wifi.ssid, wifi.password, kWifiTryMs)) {
            showRow(Wifi, wifi.ssid + " " + std::to_string(network.signalStrength()) + "dBm", kGood);
            clock_.startInternetTime();
            return;
        }
    }
    showRow(Wifi, "couldn't join", kBad);
}

std::string clockText() {
    const time_t now = static_cast<time_t>(clock_.unixTime());
    struct tm utc;
    gmtime_r(&now, &utc);
    char text[24];
    strftime(text, sizeof(text), "%H:%M:%S UTC", &utc);
    return text;
}

void waitForClock() {
    if (!network.isConnected()) {
        showRow(Clock, "needs Wi-Fi", kWaiting);
        return;
    }
    showRow(Clock, "setting...", kWaiting);
    const uint32_t start = millis();
    while (!clock_.isTimeSet() && millis() - start < kClockWaitMs) delay(200);
    if (!clock_.isTimeSet()) showRow(Clock, "not set", kBad);
}

void checkTelegram() {
    if (!storage.settings.hasBotToken()) {
        showRow(Telegram, "no bot token", kBad);
        return;
    }
    if (!network.isConnected()) {
        showRow(Telegram, "needs Wi-Fi", kWaiting);
        return;
    }
    showRow(Telegram, "asking...", kWaiting);
    fvb::telegram::TelegramClient client(network, files, logger, storage.settings.botToken());
    fvb::telegram::BotInfo info;
    if (client.getMe(info)) {
        showRow(Telegram, "@" + info.username, kGood);
    } else {
        logger.error("Telegram: " + client.lastError().description);
        showRow(Telegram, "failed (see serial)", kBad);
    }
}

std::string batteryText() {
    const int8_t level = power.batteryPercent();
    std::string text = level < 0 ? "can't read" : std::to_string(level) + "%";
    if (power.isCharging()) text += " charging";
    else if (power.isPluggedIn()) text += " plugged in";
    else text += " on battery";
    return text;
}

// ---- Live part: battery, clock, touch, buttons and LEDs ----

uint32_t lastRefreshMs = 0;
uint32_t ledsChangedMs = 0;
uint32_t lastChaseMs = 0;
uint8_t chaseStep = 0;
std::string lastBattery;

void chaseLeds() {
    // One LED at a time going round, so a dead LED is easy to spot.
    for (uint8_t i = 0; i < leds.count(); ++i) {
        leds.setPixel(i, i == chaseStep ? fvb::Colour{255, 140, 0} : fvb::Colour{20, 10, 0});
    }
    leds.show();
    chaseStep = static_cast<uint8_t>((chaseStep + 1) % leds.count());
}

void flashLeds(fvb::Colour colour) {
    leds.setAll(colour);
    leds.show();
    ledsChangedMs = millis();
}

const char* swipeName(fvb::SwipeDirection d) {
    switch (d) {
        case fvb::SwipeDirection::Left: return "left";
        case fvb::SwipeDirection::Right: return "right";
        case fvb::SwipeDirection::Up: return "up";
        case fvb::SwipeDirection::Down: return "down";
    }
    return "?";
}

}  // namespace

void setup() {
    auto config = M5.config();
    M5.begin(config);
    Serial.begin(115200);
    leds.begin();
    flashLeds(fvb::Colour{255, 255, 255});  // all ten should light white

    drawLabels();
    logger.info("Hardware check, firmware " + fvb::firmwareVersion());

    fvb::setDeviceDescription(fvb::core2Description());
    const bool newVersion = M5.Power.getType() == m5::Power_Class::pmic_axp2101;
    showRow(Model, newVersion ? "Core2 v1.1" : "Core2 (v1.0)", kWhite);
    logger.info(fvb::deviceDescription());

    checkSdCard();
    checkMemory();
    readSetupFile();
    joinWifi();
    waitForClock();
    checkTelegram();

    showRow(Touch, "tap or swipe", kGrey);
    showRow(Buttons, "press each one", kGrey);
}

void loop() {
    input.update();
    fvb::InputEvent event;
    while (input.pollEvent(event)) {
        switch (event.type) {
            case fvb::InputEvent::Type::Tap:
                showRow(Touch, "tap at " + std::to_string(event.x) + "," + std::to_string(event.y), kGood);
                display.fillCircle(event.x, event.y, 6, kGood);
                flashLeds(fvb::Colour{255, 255, 255});
                break;
            case fvb::InputEvent::Type::Swipe:
                showRow(Touch, std::string("swipe ") + swipeName(event.swipe), kGood);
                flashLeds(fvb::Colour{160, 0, 255});
                break;
            case fvb::InputEvent::Type::ButtonPress:
                if (event.button == fvb::Button::Left) {
                    showRow(Buttons, "left (red)", kGood);
                    flashLeds(fvb::Colour{255, 0, 0});
                } else if (event.button == fvb::Button::Middle) {
                    showRow(Buttons, "middle (green)", kGood);
                    flashLeds(fvb::Colour{0, 255, 0});
                } else {
                    showRow(Buttons, "right (blue)", kGood);
                    flashLeds(fvb::Colour{0, 0, 255});
                }
                break;
        }
    }

    const uint32_t now = millis();
    if (now - lastRefreshMs >= 1000) {
        lastRefreshMs = now;
        if (clock_.isTimeSet()) {
            // Straight to the screen, not the log, so the serial port isn't flooded.
            display.fillRect(kValueX, Clock * kRowHeight, static_cast<int16_t>(display.width() - kValueX), kRowHeight,
                             fvb::Colour{});
            display.drawText(kValueX, static_cast<int16_t>(Clock * kRowHeight + 4), clockText(), kGood);
        }
        const std::string battery = batteryText();
        if (battery != lastBattery) {
            lastBattery = battery;
            const int8_t level = power.batteryPercent();
            showRow(Battery, battery, level >= 0 && level < 20 ? kBad : kWhite);
        }
    }
    // A press shows its colour for a second, then the LEDs go back to the chase.
    if (now - ledsChangedMs >= 1000 && now - lastChaseMs >= 150) {
        lastChaseMs = now;
        chaseLeds();
    }

    delay(10);
}

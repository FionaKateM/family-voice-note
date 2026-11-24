// Entry point for the real device (M5Stack Core2).
//
// As of ticket 2.1 the screen, touch, buttons, LEDs, SD card, internal memory
// and Wi-Fi are real. Sound comes in ticket 2.2, and Telegram moves onto the
// device in ticket 2.3, so for now the box shows its screens but doesn't send
// or receive anything. To check the hardware, use the core2-check build
// instead (src/core2_check.cpp).

#include <M5Unified.h>

#include "app/App.h"
#include "app/Version.h"
#include "platform/core2/Core2Hardware.h"

namespace {

// How long to try each known Wi-Fi network at start-up. Ticket 2.4 replaces
// this with the full Wi-Fi setup (the setup hotspot when none work).
constexpr uint32_t kWifiTryMs = 15000;

fvb::Core2PlaceholderAudio audio;
fvb::Core2Display display;
fvb::Core2FileStorage files;
fvb::Core2SettingsStore settings;
fvb::Core2Network network;
fvb::Core2Clock clock_;
fvb::Core2Leds leds;
fvb::Core2Power power;
fvb::Core2Input input;
fvb::Core2SerialLogger logger;

fvb::Hardware hardware{audio, display, files, settings, network, clock_, leds, power, input, logger};
fvb::App app(hardware);

void joinKnownWifi() {
    for (const auto& wifi : app.storage().settings.wifiNetworks()) {
        logger.info("Trying Wi-Fi network " + wifi.ssid);
        if (fvb::core2JoinWifi(wifi.ssid, wifi.password, kWifiTryMs)) {
            logger.info("On Wi-Fi, signal " + std::to_string(network.signalStrength()) + " dBm");
            clock_.startInternetTime();
            return;
        }
    }
    logger.warning("Couldn't join any known Wi-Fi network");
}

}  // namespace

void setup() {
    auto config = M5.config();
    M5.begin(config);
    Serial.begin(115200);

    fvb::setDeviceDescription(fvb::core2Description());
    logger.info(fvb::deviceDescription());

    if (!settings.begin()) logger.error("Internal memory (NVS) couldn't be opened");
    if (!files.begin()) logger.warning("No SD card found");
    leds.begin();

    app.begin();  // reads setup.txt from the SD card, if there is one
    joinKnownWifi();
}

void loop() {
    app.tick();
    delay(10);
}

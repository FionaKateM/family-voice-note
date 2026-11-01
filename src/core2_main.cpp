// Entry point for the real device (M5Stack Core2).

#include <M5Unified.h>

#include "app/App.h"
#include "platform/core2/Core2Hardware.h"

namespace {

fvb::Core2PlaceholderAudio audio;
fvb::Core2Display display;
fvb::Core2PlaceholderFileStorage files;
fvb::Core2PlaceholderSettingsStore settings;
fvb::Core2PlaceholderNetwork network;
fvb::Core2Clock clock_;
fvb::Core2PlaceholderLeds leds;
fvb::Core2Power power;
fvb::Core2Input input;
fvb::Core2SerialLogger logger;

fvb::Hardware hardware{audio, display, files, settings, network, clock_, leds, power, input, logger};
fvb::App app(hardware);

}  // namespace

void setup() {
    auto config = M5.config();
    M5.begin(config);
    Serial.begin(115200);
    app.begin();
}

void loop() {
    app.tick();
    delay(10);
}

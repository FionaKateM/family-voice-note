#include "app/App.h"

#include <string>

#include "app/Version.h"

namespace fvb {

void App::begin() {
    hw_.log.info(std::string("Family Voice Box ") + kFirmwareVersion + " starting");

    hw_.leds.setAll(Colour{0, 0, 0});
    hw_.leds.show();

    hw_.display.setOn(true);
    hw_.display.clear();
    hw_.display.drawText(10, 10, "Family Voice Box");
    hw_.display.drawText(10, 30, std::string("v") + kFirmwareVersion);
    hw_.display.present();

    if (!hw_.files.isMounted()) {
        hw_.log.warning("SD card not available");
    }
}

void App::tick() {
    ++ticks_;

    hw_.input.update();
    InputEvent event;
    while (hw_.input.pollEvent(event)) {
        handleInput(event);
    }
}

void App::handleInput(const InputEvent& event) {
    // Nothing reacts to input yet. Logged so the desktop build shows it's wired up.
    switch (event.type) {
        case InputEvent::Type::ButtonPress:
            hw_.log.debug("Button " + std::to_string(static_cast<int>(event.button)));
            break;
        case InputEvent::Type::Tap:
            hw_.log.debug("Tap at " + std::to_string(event.x) + "," + std::to_string(event.y));
            break;
        case InputEvent::Type::Swipe:
            hw_.log.debug("Swipe " + std::to_string(static_cast<int>(event.swipe)));
            break;
    }
}

}  // namespace fvb

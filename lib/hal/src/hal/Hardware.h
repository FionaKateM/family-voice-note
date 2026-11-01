#pragma once

#include "hal/Audio.h"
#include "hal/Clock.h"
#include "hal/Display.h"
#include "hal/FileStorage.h"
#include "hal/Input.h"
#include "hal/Leds.h"
#include "hal/Logger.h"
#include "hal/Network.h"
#include "hal/Power.h"
#include "hal/SettingsStore.h"

namespace fvb {

// Everything the box's logic is allowed to touch. Each build fills this in with
// its own versions; tests fill it in with fakes.
struct Hardware {
    Audio& audio;
    Display& display;
    FileStorage& files;
    SettingsStore& settings;
    Network& network;
    Clock& clock;
    Leds& leds;
    Power& power;
    Input& input;
    Logger& log;
};

}  // namespace fvb

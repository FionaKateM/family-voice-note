#pragma once

#include <cstdint>
#include <string>

namespace fvb {

// Speaker and microphone.
// Paths are on the SD card (see FileStorage), e.g. "/contacts/1/in/0005.ogg".
class Audio {
public:
    virtual ~Audio() = default;

    // Playback. Returns false if the file can't be played.
    virtual bool play(const std::string& path) = 0;
    virtual void stopPlayback() = 0;
    virtual bool isPlaying() const = 0;

    // 0 to 100. The hearing safety cap is applied below this, whatever the value.
    virtual void setVolume(uint8_t percent) = 0;

    // Recording to an MP3 file. Returns false if recording can't start.
    virtual bool startRecording(const std::string& path) = 0;
    // Stops recording and returns its length in milliseconds (0 if nothing was recorded).
    virtual uint32_t stopRecording() = 0;
    virtual bool isRecording() const = 0;

    // Current mic loudness, 0 to 100. Used to make the LEDs react to the child's voice.
    virtual uint8_t inputLevel() const = 0;
};

}  // namespace fvb

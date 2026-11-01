#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hal/Audio.h"
#include "hal/Logger.h"

namespace fvb {

// The laptop's speakers and microphone.
//
// SDL2 does the playing and recording. ffmpeg (a separate program, which must
// be installed) turns files into sound and back: it reads anything, including
// Telegram's Ogg Opus voice notes and the box's own MP3s, and writes the MP3
// recordings. The device will use its own decoder and encoder (ticket 2.2), so
// this is only a stand-in.
//
// Paths are on the pretend SD card, so "/work/recording.mp3" means
// <sdcard folder>/work/recording.mp3.
//
// Everything except the sound callbacks runs on the main thread.
class DesktopAudio : public Audio {
public:
    static constexpr int kPlaybackRate = 48000;
    // Recordings are saved in the same format the device will use: mono MP3,
    // 16 kHz, 32 kbit/s (see scripts/make-test-tone.sh).
    static constexpr int kRecordingRate = 16000;

    DesktopAudio(std::filesystem::path sdcardRoot, Logger& log) : root_(std::move(sdcardRoot)), log_(log) {}
    ~DesktopAudio() override;
    DesktopAudio(const DesktopAudio&) = delete;
    DesktopAudio& operator=(const DesktopAudio&) = delete;

    // Starts SDL's sound and checks ffmpeg is there. Returns false, after
    // logging why, if sound won't work. The box still runs, just silently.
    bool begin();

    bool play(const std::string& path) override;
    void stopPlayback() override;
    bool isPlaying() const override;
    void setVolume(uint8_t percent) override { volume_ = percent > 100 ? 100 : percent; }

    bool startRecording(const std::string& path) override;
    uint32_t stopRecording() override;
    bool isRecording() const override { return recording_; }
    uint8_t inputLevel() const override { return level_; }

    // Turns mono 16-bit samples into a loudness from 0 to 100 (the loudest sample,
    // scaled so normal speech reaches the top half). Public for tests.
    static uint8_t levelOf(const int16_t* samples, size_t count);

private:
    static void playbackCallback(void* self, uint8_t* stream, int length);
    static void captureCallback(void* self, uint8_t* stream, int length);
    std::filesystem::path realPath(const std::string& path) const;
    bool openOutput();

    std::filesystem::path root_;
    Logger& log_;
    bool started_ = false;
    bool ffmpeg_ = false;
    std::atomic<uint8_t> volume_{70};

    // Playback. The decoder thread runs ffmpeg and adds samples; the sound
    // callback takes them.
    uint32_t output_ = 0;  // SDL device, 0 when closed
    mutable std::mutex playMutex_;
    std::vector<int16_t> playSamples_;
    size_t playPosition_ = 0;
    bool decoding_ = false;
    bool playing_ = false;
    std::thread decoder_;
    std::atomic<bool> cancelDecode_{false};

    // Recording. The sound callback adds samples; stopRecording turns them into an MP3.
    uint32_t input_ = 0;
    std::mutex recordMutex_;
    std::vector<int16_t> recordSamples_;
    std::string recordingPath_;
    bool recording_ = false;
    std::atomic<uint8_t> level_{0};
};

}  // namespace fvb

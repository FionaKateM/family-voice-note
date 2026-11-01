#include "platform/desktop/DesktopAudio.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include "platform/desktop/Subprocess.h"

namespace fvb {

DesktopAudio::~DesktopAudio() {
    stopPlayback();
    if (recording_) stopRecording();
    if (output_) SDL_CloseAudioDevice(output_);
    if (started_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool DesktopAudio::begin() {
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        log_.warning(std::string("Sound isn't available: ") + SDL_GetError());
        return false;
    }
    started_ = true;
    ffmpeg_ = subprocess::isAvailable("ffmpeg");
    if (!ffmpeg_) {
        log_.warning("ffmpeg isn't installed, so messages can't be played or recorded. "
                     "On a Mac: brew install ffmpeg");
        return false;
    }
    return true;
}

std::filesystem::path DesktopAudio::realPath(const std::string& path) const {
    if (path.empty() || path[0] != '/' || path.find("..") != std::string::npos) return {};
    return root_ / path.substr(1);
}

uint8_t DesktopAudio::levelOf(const int16_t* samples, size_t count) {
    int peak = 0;
    for (size_t i = 0; i < count; ++i) peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
    // Speech rarely gets near full scale on a laptop mic, so double it.
    return static_cast<uint8_t>(std::min(100, peak * 200 / 32768));
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

bool DesktopAudio::openOutput() {
    if (output_) return true;
    SDL_AudioSpec want{};
    want.freq = kPlaybackRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = [](void* self, Uint8* stream, int length) { playbackCallback(self, stream, length); };
    want.userdata = this;
    output_ = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (!output_) {
        log_.warning(std::string("Couldn't open the speakers: ") + SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(output_, 0);
    return true;
}

void DesktopAudio::playbackCallback(void* self, uint8_t* stream, int length) {
    auto* audio = static_cast<DesktopAudio*>(self);
    auto* out = reinterpret_cast<int16_t*>(stream);
    const size_t wanted = static_cast<size_t>(length) / sizeof(int16_t);
    const int volume = audio->volume_;

    std::lock_guard<std::mutex> lock(audio->playMutex_);
    size_t i = 0;
    if (audio->playing_) {
        for (; i < wanted && audio->playPosition_ < audio->playSamples_.size(); ++i) {
            out[i] = static_cast<int16_t>(audio->playSamples_[audio->playPosition_++] * volume / 100);
        }
        if (!audio->decoding_ && audio->playPosition_ >= audio->playSamples_.size()) audio->playing_ = false;
    }
    for (; i < wanted; ++i) out[i] = 0;  // silence while waiting, or when nothing is playing
}

bool DesktopAudio::play(const std::string& path) {
    stopPlayback();
    if (!ffmpeg_ || !openOutput()) return false;
    const auto file = realPath(path);
    std::error_code ec;
    if (file.empty() || !std::filesystem::is_regular_file(file, ec)) {
        log_.warning("No audio file at " + path);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(playMutex_);
        playSamples_.clear();
        playPosition_ = 0;
        decoding_ = true;
        playing_ = true;
    }
    cancelDecode_ = false;
    decoder_ = std::thread([this, file] {
        // Leftover bytes when a piece ends half way through a sample.
        std::string partial;
        const int code = subprocess::runReadingOutput(
            {"ffmpeg", "-nostdin", "-v", "error", "-i", file.string(), "-f", "s16le", "-ac", "1", "-ar",
             std::to_string(kPlaybackRate), "-"},
            [this, &partial](const char* data, size_t length) {
                if (cancelDecode_) return false;
                partial.append(data, length);
                const size_t whole = partial.size() / 2;
                std::lock_guard<std::mutex> lock(playMutex_);
                const size_t before = playSamples_.size();
                playSamples_.resize(before + whole);
                std::memcpy(playSamples_.data() + before, partial.data(), whole * 2);
                partial.erase(0, whole * 2);
                return true;
            });
        std::lock_guard<std::mutex> lock(playMutex_);
        decoding_ = false;
        if (code != 0 && !cancelDecode_) {
            log_.warning("ffmpeg couldn't read " + file.filename().string());
            if (playSamples_.empty()) playing_ = false;
        }
    });

    // Wait until there's sound to play, so a file that can't be read is reported
    // as a failure rather than looking like it played to the end.
    const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(playMutex_);
            if (!playSamples_.empty()) return true;
            if (!decoding_) break;
        }
        if (std::chrono::steady_clock::now() > giveUp) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    stopPlayback();
    return false;
}

void DesktopAudio::stopPlayback() {
    cancelDecode_ = true;
    if (decoder_.joinable()) decoder_.join();
    std::lock_guard<std::mutex> lock(playMutex_);
    playing_ = false;
    playSamples_.clear();
    playPosition_ = 0;
}

bool DesktopAudio::isPlaying() const {
    std::lock_guard<std::mutex> lock(playMutex_);
    return playing_;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void DesktopAudio::captureCallback(void* self, uint8_t* stream, int length) {
    auto* audio = static_cast<DesktopAudio*>(self);
    const auto* in = reinterpret_cast<const int16_t*>(stream);
    const size_t count = static_cast<size_t>(length) / sizeof(int16_t);
    audio->level_ = levelOf(in, count);
    std::lock_guard<std::mutex> lock(audio->recordMutex_);
    audio->recordSamples_.insert(audio->recordSamples_.end(), in, in + count);
}

bool DesktopAudio::startRecording(const std::string& path) {
    if (recording_) stopRecording();
    if (!started_ || !ffmpeg_) return false;
    if (realPath(path).empty()) return false;

    SDL_AudioSpec want{};
    want.freq = kRecordingRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = [](void* self, Uint8* stream, int length) { captureCallback(self, stream, length); };
    want.userdata = this;
    input_ = SDL_OpenAudioDevice(nullptr, 1, &want, nullptr, 0);
    if (!input_) {
        log_.warning(std::string("Couldn't open the microphone: ") + SDL_GetError());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(recordMutex_);
        recordSamples_.clear();
    }
    recordingPath_ = path;
    recording_ = true;
    level_ = 0;
    SDL_PauseAudioDevice(input_, 0);
    return true;
}

uint32_t DesktopAudio::stopRecording() {
    if (!recording_) return 0;
    recording_ = false;
    SDL_CloseAudioDevice(input_);  // waits for the callback to finish
    input_ = 0;
    level_ = 0;

    std::vector<int16_t> samples;
    {
        std::lock_guard<std::mutex> lock(recordMutex_);
        samples.swap(recordSamples_);
    }
    const uint32_t durationMs = static_cast<uint32_t>(samples.size() * 1000ULL / kRecordingRate);
    if (samples.empty()) return 0;

    const auto file = realPath(recordingPath_);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const int code = subprocess::runWithInput(
        {"ffmpeg", "-nostdin", "-v", "error", "-y", "-f", "s16le", "-ar", std::to_string(kRecordingRate), "-ac", "1", "-i",
         "-", "-c:a", "libmp3lame", "-b:a", "32k", "-abr", "0", "-map_metadata", "-1", "-id3v2_version", "0",
         "-write_id3v1", "0", "-write_xing", "0", "-f", "mp3", file.string()},
        reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(int16_t));
    if (code != 0) {
        log_.error("ffmpeg couldn't save the recording");
        return 0;
    }
    return durationMs;
}

}  // namespace fvb

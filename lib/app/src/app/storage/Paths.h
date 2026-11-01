#pragma once

#include <cstdint>
#include <string>

namespace fvb {

// Where things live on the SD card.
//
//   /setup.txt                    setup file, deleted once read
//   /contacts/<n>/index.txt       that contact's message list
//   /contacts/<n>/0001.ogg        a received voice note
//   /contacts/<n>/0002.mp3        a recording the child sent
//   /contacts/<n>/photo.jpg       the contact's photo
//   /outbox/<n>.txt               one file per message waiting to send
//   /jobs/<n>.txt                 other Telegram work waiting: downloads, 👀
//                                 reactions, monitor copies, notices
//   /bin/<date>-<time>-c<n>-<name>.mp3   deleted and re-recorded recordings
//   /work/                        files being recorded or downloaded
//
// <n> for contacts is the box's own number for that person (1, 2, 3...), never
// reused. Telegram IDs are kept out of file names.
namespace paths {

constexpr const char* kSetupFile = "/setup.txt";
constexpr const char* kContactsDir = "/contacts";
constexpr const char* kOutboxDir = "/outbox";
constexpr const char* kJobsDir = "/jobs";
constexpr const char* kBinDir = "/bin";
constexpr const char* kWorkDir = "/work";
constexpr const char* kRecording = "/work/recording.mp3";
constexpr const char* kDownload = "/work/download.tmp";

std::string contactDir(uint32_t contact);
std::string contactIndex(uint32_t contact);
std::string contactPhoto(uint32_t contact);
std::string contactFile(uint32_t contact, const std::string& fileName);

}  // namespace paths

// Turns seconds since 1970 (UTC) into "20261101-143205". Used for bin file names.
// Timezones come in ticket 1.8; until then these are UTC.
std::string formatCompactTime(int64_t unixSeconds);
// The reverse. Returns false if the text isn't in that shape.
bool parseCompactTime(const std::string& text, int64_t& unixSeconds);

}  // namespace fvb

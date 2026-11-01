#pragma once

#include <cstdint>
#include <string>

#include "hal/Logger.h"
#include "telegram/TelegramClient.h"

namespace fvb {
namespace telegram {

// A throwaway bot for trying the Telegram connection from a phone: voice notes,
// reactions and photos, which the real box can't do until tickets 1.5 and 1.7.
// Run it with `program --try-telegram` on the laptop.
//
// It prints an invite link. The first person to open that link becomes the
// only person it answers; everyone else is ignored. Then:
//   /buttons      shows two buttons; pressing one edits the message
//   /photo        saves your profile photo to the pretend SD card
//   a voice note  is saved, gets a 👀 reaction, and is sent back twice:
//                 once uploaded with its duration, once re-sent by file ID
//   anything else is described back to you
class TryOut {
public:
    TryOut(TelegramClient& client, Logger& log, std::string joinCode, std::string saveFolder = "/tryout");

    // Checks the bot token and prints the invite link. False if it didn't work.
    bool start();
    // Waits for one batch of messages and answers them. Returns how many
    // milliseconds to wait before calling again (non-zero after an error).
    uint32_t step();

    bool hasJoined() const { return chat_ != 0; }
    const std::string& link() const { return link_; }

private:
    void handle(const Update& update);
    void handleMessage(const Message& message);
    void handleButton(const CallbackQuery& query);
    void handleVoice(const Message& message);
    void handlePhotoCommand(const Message& message);
    void reply(const std::string& text, const InlineKeyboard* keyboard = nullptr);
    void warn(const std::string& what);

    TelegramClient& client_;
    Logger& log_;
    std::string joinCode_;
    std::string saveFolder_;
    std::string link_;
    ChatId chat_ = 0;
};

}  // namespace telegram
}  // namespace fvb

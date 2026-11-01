#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/bot/Bot.h"
#include "app/invites/InviteFlows.h"
#include "hal/FileStorage.h"
#include "hal/Network.h"
#include "hal/Power.h"

namespace fvb {

// Admin settings (section 12 of the requirements).
//
// /settings opens a menu of buttons. Each part of it can also be reached
// directly by typing, and most take the new value straight away:
//
//   /contacts                list, invite, remove
//   /monitors [mode]         list, invite, remove, and the monitoring mode
//                            (off, sent, received or all)
//   /admins                  list, invite, remove
//   /quiethours [from to]    e.g. /quiethours 19:00 07:00, or /quiethours off
//   /timezone [country]      e.g. /timezone France
//   /volume [percent]        voice volume
//   /alertvolume [percent]   chimes and alerts
//   /maxrecording [seconds]  longest voice note, 10 to 900 seconds
//   /keepmessages [days]     days before voice notes are removed, 0 to 3650 (0 is for ever)
//   /childname [name]
//   /status                  battery, Wi-Fi, storage, firmware, messages waiting
//   /trash getlast           sends the most recently deleted recording
//
// Only /settings, /status and /trash appear in /help; the others are typing
// shortcuts into the menu, listed in section 12 of the requirements.
//
// Everything here is for admins only. The menu is a conversation, so it carries
// on after a restart and is dropped if the admin is removed partway through.
//
// Create one after the Bot and InviteFlows; it registers its commands and
// conversation with the Bot. They must all stay alive together.
class AdminSettings {
public:
    // Conversation name. Saved with conversations, so never change it.
    static constexpr const char* kConversation = "settings";
    // Longest child's name, in characters. The same limit as contacts' names.
    static constexpr size_t kMaxChildNameLength = 30;

    AdminSettings(Bot& bot, Storage& storage, InviteFlows& invites, FileStorage& files, Network& network,
                  Power& power);
    ~AdminSettings();

    // What /status says.
    std::string statusText() const;

private:
    class Menu;
    friend class Menu;

    // Each of these is used by both the menu and a direct command. They return
    // the confirmation to show, or "" if the value wasn't accepted.
    std::string setQuietHours(uint16_t start, uint16_t end);
    std::string setVolume(bool alert, uint8_t percent);
    std::string setMaxRecording(uint32_t seconds);
    std::string setKeepDays(uint32_t days);
    std::string setChildName(const std::string& name, std::string& problem);
    std::string setMonitorMode(MonitorMode mode);
    std::string setTimezone(const std::string& zone);

    void sendLastDeleted(Context& ctx);
    // A direct command with no argument (or one it couldn't use): opens that
    // part of the menu.
    void open(Context& ctx, const std::string& page, const std::string& typed = "");

    Bot& bot_;
    Storage& storage_;
    InviteFlows& invites_;
    FileStorage& files_;
    Network& network_;
    Power& power_;
    std::unique_ptr<Menu> menu_;
};

// Reads a time of day typed by a person: "19:00", "7:30", "7pm", "7.30am",
// "19". Returns false if it doesn't make sense. For tests.
bool parseTimeOfDay(const std::string& text, uint16_t& minutes);

// "30 seconds", "1 minute", "6 minutes", "2 minutes 30 seconds".
std::string describeSeconds(uint32_t seconds);

}  // namespace fvb

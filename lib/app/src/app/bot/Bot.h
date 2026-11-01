#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/bot/Conversation.h"
#include "app/bot/ConversationStore.h"
#include "app/bot/Roles.h"
#include "app/storage/Storage.h"
#include "hal/Clock.h"
#include "hal/Logger.h"
#include "hal/SettingsStore.h"
#include "telegram/TelegramClient.h"

namespace fvb {

class Bot;

// The person a message or button press came from.
struct Sender {
    telegram::UserId userId = 0;
    // The private chat with them. The box only talks in private chats, where
    // this is the same number as userId.
    telegram::ChatId chatId = 0;
    std::string firstName;  // their Telegram first name
    Roles roles;
};

// Handed to every command, button and conversation handler. Says who they're
// dealing with and lets them answer.
class Context {
public:
    const Sender& sender() const { return sender_; }

    Storage& storage();
    telegram::TelegramClient& telegram();
    Clock& clock();
    Logger& log();
    // Seconds since 1970 (UTC), or 0 if the clock hasn't been set yet.
    int64_t now() const;

    // Sends a message to the person. `keyboard` is optional.
    bool reply(const std::string& text, const telegram::InlineKeyboard* keyboard = nullptr);

    // ---- Only for button presses ----
    bool isButtonPress() const { return buttonMessageId_ != 0; }
    // Changes the message the pressed button was on (for example to remove the
    // buttons once one has been chosen).
    bool editButtonMessage(const std::string& text, const telegram::InlineKeyboard* keyboard = nullptr);
    // Text that pops up briefly on their phone when the button press is answered.
    void popup(const std::string& text) { popup_ = text; }

    // ---- Conversations ----

    // Starts a conversation with this person, ending any they're already in.
    // `data` is put in the state before ConversationType::begin runs (for
    // example which invite they used). False if the type isn't registered or
    // doesn't allow this person.
    bool startConversation(const std::string& type, const std::map<std::string, std::string>& data = {});
    bool inConversation() const;
    // A button for use inside the current conversation. Pressing it calls the
    // conversation's onButton with `value`. Buttons from a conversation that has
    // since finished are answered with "This has finished" and otherwise ignored.
    // `value` can be up to 50 bytes.
    telegram::InlineButton button(const std::string& text, const std::string& value) const;

private:
    friend class Bot;
    explicit Context(Bot& bot) : bot_(bot) {}

    Bot& bot_;
    Sender sender_;
    int64_t buttonMessageId_ = 0;
    std::string popup_;
};

// The conversation engine. Reads messages and button presses from Telegram,
// works out who sent each one (admin, contact, monitor or nobody), and passes it
// to the right handler:
//
//   1. Anyone who isn't in a list and isn't partway through an invite is ignored,
//      with no reply. Group chats and other bots are ignored too.
//   2. "/start <code>" goes to the start link handler (invites, ticket 1.5).
//   3. /cancel stops the conversation they're in.
//   4. Other commands go to the handler registered for them, if the person has
//      a role that's allowed to use it. A command ends any conversation they
//      were in. Known people get a short reply for commands they can't use or
//      that don't exist.
//   5. Anything else goes to their conversation if they're in one, otherwise
//      to the message handler (voice notes, ticket 1.7).
//
// Button presses go to the conversation that made them, or to a handler
// registered for the start of their callback data ("settings:..." and so on).
//
// Conversations and how far it has got through Telegram's messages are saved in
// internal memory as it goes, so a restart neither loses a conversation nor
// handles the same message twice.
//
// Later tickets register their commands, buttons and conversations with the
// add... methods.
class Bot {
public:
    enum class State {
        NotConnected,   // the bot token hasn't been checked yet
        Connected,      // token checked; resume() still to do
        Running,
        TokenRejected,  // Telegram turned the token down; goes no further
    };

    // How long a conversation can sit with no reply before it's forgotten.
    static constexpr int64_t kConversationTimeoutSeconds = 7 * 24 * 60 * 60;
    // How long to wait after Telegram can't be reached.
    static constexpr uint32_t kRetryMs = 5000;
    // If the box restarts this many times while handling the same message, that
    // message is skipped, so one bad message can't keep restarting the box.
    static constexpr int64_t kMaxAttempts = 2;

    using CommandHandler = std::function<void(Context&, const std::string& argument)>;
    using ButtonHandler = std::function<void(Context&, const std::string& data)>;
    using StartLinkHandler = std::function<bool(Context&, const std::string& code)>;
    using MessageHandler = std::function<void(Context&, const telegram::Message&)>;

    Bot(telegram::TelegramClient& client, Storage& storage, SettingsStore& store, Clock& clock, Logger& log);

    // ---- Running ----

    // Checks the bot token with Telegram. Doesn't touch storage, so it can run
    // without holding any lock. Call until state() isn't NotConnected; on
    // success it becomes Connected. Returns how many milliseconds to wait
    // before trying again.
    uint32_t connect();
    // Once Connected: picks up where polling got to before the last restart,
    // and the state becomes Running. Touches storage, but doesn't wait on the
    // network.
    void resume();
    // Waits for Telegram to send new messages (up to 25 seconds). Doesn't touch
    // storage, so it can run without holding any lock. Returns how many
    // milliseconds to wait before polling again (0 normally).
    uint32_t poll(std::vector<telegram::Update>& updates);
    // Handles what poll returned. Touches storage and sends replies.
    void handle(const std::vector<telegram::Update>& updates);
    // connect() and resume() if needed, then poll() and handle(). Returns
    // milliseconds to wait.
    uint32_t step();

    State state() const { return state_; }

    // ---- Registering handlers (later tickets) ----

    // `roles` is a mask of Roles::kAdmin etc. `description` is shown in /help;
    // leave it empty to keep a command out of the list. Registering a name
    // again replaces it.
    void addCommand(const std::string& name, uint8_t roles, const std::string& description, CommandHandler handler);
    // For buttons that aren't part of a conversation, e.g. the approve buttons
    // sent to every admin. Handles callback data "<prefix>:<rest>" and is given
    // <rest>. The prefix can't start with '#'.
    void addButtons(const std::string& prefix, uint8_t roles, ButtonHandler handler);
    // The type must stay alive as long as the bot.
    void addConversation(ConversationType& type);
    // Called for "/start <code>", from anyone including strangers. Return true
    // if the code was recognised. If it returns false, strangers are ignored.
    void setStartLinkHandler(StartLinkHandler handler) { startLink_ = std::move(handler); }
    // Called for messages from known people that aren't commands and aren't part
    // of a conversation. By default it points them at /help.
    void setMessageHandler(MessageHandler handler) { onMessage_ = std::move(handler); }

    // For tests and /status.
    ConversationStore& conversations() { return conversations_; }

private:
    friend class Context;

    struct Command {
        uint8_t roles = 0;
        std::string description;
        CommandHandler handler;
    };
    struct Buttons {
        uint8_t roles = 0;
        ButtonHandler handler;
    };

    void handleUpdate(const telegram::Update& update);
    void handleMessage(const telegram::Message& message);
    void handleCommand(Context& ctx, const std::string& command, const std::string& argument,
                       const telegram::Message& message);
    void handleButton(const telegram::CallbackQuery& query);
    void handleConversationButton(Context& ctx, const std::string& data);

    // Loads the sender's conversation into current_, dropping it if it has
    // timed out or they're no longer allowed in it.
    void loadConversation(const Sender& sender);
    void runStep(Context& ctx, ConversationType::Outcome outcome, uint32_t idBefore);
    void endConversation();
    void saveConversation();
    ConversationType* typeNamed(const std::string& name) const;
    bool startConversation(Context& ctx, const std::string& type, const std::map<std::string, std::string>& data);

    void addBuiltInCommands();
    std::string helpText(const Roles& roles) const;
    void savePosition(int64_t nextUpdateId);
    void warnTelegram(const std::string& what);

    telegram::TelegramClient& client_;
    Storage& storage_;
    SettingsStore& store_;
    Clock& clock_;
    Logger& log_;
    ConversationStore conversations_;

    State state_ = State::NotConnected;
    int64_t botId_ = 0;  // from getMe
    int64_t savedPosition_ = 0;

    std::map<std::string, Command> commands_;
    std::map<std::string, Buttons> buttons_;
    std::vector<ConversationType*> types_;
    StartLinkHandler startLink_;
    MessageHandler onMessage_;

    // The conversation of the person currently being dealt with.
    bool hasCurrent_ = false;
    ConversationState current_;
};

}  // namespace fvb

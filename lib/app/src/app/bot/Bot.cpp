#include "app/bot/Bot.h"

#include <utility>

#include "app/storage/TextRecord.h"

namespace fvb {

using telegram::CallbackQuery;
using telegram::Error;
using telegram::InlineButton;
using telegram::InlineKeyboard;
using telegram::Message;
using telegram::MessageKind;
using telegram::Update;

namespace {
// NVS keys. 15 characters at most.
constexpr const char* kBotId = "tg_bot_id";    // which bot the saved position belongs to
constexpr const char* kPosition = "tg_offset";  // the next update ID to handle
constexpr const char* kBusy = "tg_busy";        // "<update ID>\t<attempts>" for the one being handled

// Conversation buttons carry "#<conversation id>:<value>" as their callback data.
constexpr char kConversationButton = '#';
constexpr size_t kMaxButtonValueBytes = 50;

const char* kNothingToStop = "There's nothing to stop.";
const char* kStopped = "Stopped.";
const char* kFinished = "This has finished.";
const char* kButtonGone = "This button no longer works.";
const char* kHelpHint = "Send /help to see what I can do.";
}  // namespace

// ---------------------------------------------------------------------------
// Context

Storage& Context::storage() { return bot_.storage_; }
telegram::TelegramClient& Context::telegram() { return bot_.client_; }
Clock& Context::clock() { return bot_.clock_; }
Logger& Context::log() { return bot_.log_; }
int64_t Context::now() const { return bot_.clock_.isTimeSet() ? bot_.clock_.unixTime() : 0; }

bool Context::reply(const std::string& text, const InlineKeyboard* keyboard) {
    if (bot_.client_.sendMessage(sender_.chatId, text, keyboard)) return true;
    bot_.warnTelegram("Couldn't send a reply");
    return false;
}

bool Context::editButtonMessage(const std::string& text, const InlineKeyboard* keyboard) {
    if (!isButtonPress()) return false;
    if (bot_.client_.editMessageText(sender_.chatId, buttonMessageId_, text, keyboard)) return true;
    bot_.warnTelegram("Couldn't change a message");
    return false;
}

bool Context::startConversation(const std::string& type, const std::map<std::string, std::string>& data) {
    return bot_.startConversation(*this, type, data);
}

bool Context::inConversation() const { return bot_.hasCurrent_; }

InlineButton Context::button(const std::string& text, const std::string& value) const {
    const uint32_t id = bot_.hasCurrent_ ? bot_.current_.id : 0;
    std::string v = value.substr(0, kMaxButtonValueBytes);
    return InlineButton{text, std::string(1, kConversationButton) + std::to_string(id) + ":" + v};
}

// ---------------------------------------------------------------------------
// Setting up

Bot::Bot(telegram::TelegramClient& client, Storage& storage, SettingsStore& store, Clock& clock, Logger& log)
    : client_(client), storage_(storage), store_(store), clock_(clock), log_(log), conversations_(store) {
    addBuiltInCommands();
}

void Bot::addCommand(const std::string& name, uint8_t roles, const std::string& description,
                     CommandHandler handler) {
    commands_[name] = Command{roles, description, std::move(handler)};
}

void Bot::addButtons(const std::string& prefix, uint8_t roles, ButtonHandler handler) {
    if (prefix.empty() || prefix[0] == kConversationButton || prefix.find(':') != std::string::npos) return;
    buttons_[prefix] = Buttons{roles, std::move(handler)};
}

void Bot::addConversation(ConversationType& type) {
    for (auto*& existing : types_) {
        if (std::string(existing->name()) == type.name()) {
            existing = &type;
            return;
        }
    }
    types_.push_back(&type);
}

ConversationType* Bot::typeNamed(const std::string& name) const {
    for (auto* type : types_) {
        if (name == type->name()) return type;
    }
    return nullptr;
}

void Bot::addBuiltInCommands() {
    addCommand("start", Roles::kAnyKnown, "", [this](Context& ctx, const std::string&) {
        ctx.reply("Hello! This is " + storage_.settings.childName() + "'s voice box.\n\n" +
                  helpText(ctx.sender().roles));
    });
    addCommand("help", Roles::kAnyKnown, "show this list",
               [this](Context& ctx, const std::string&) { ctx.reply(helpText(ctx.sender().roles)); });
}

std::string Bot::helpText(const Roles& roles) const {
    const std::string child = storage_.settings.childName();
    std::string out;
    if (roles.isContact()) out += "Send " + child + " a voice note any time.\n";
    if (roles.isMonitor()) out += "You get copies of " + child + "'s voice notes when monitoring is switched on.\n";
    if (!out.empty()) out += "\n";

    out += "Commands:";
    for (const auto& entry : commands_) {
        if (entry.second.description.empty() || !roles.any(entry.second.roles)) continue;
        out += "\n/" + entry.first + " \xE2\x80\x93 " + entry.second.description;  // en dash
    }
    out += "\n/cancel \xE2\x80\x93 stop what you're in the middle of";
    return out;
}

// ---------------------------------------------------------------------------
// Running

uint32_t Bot::connect() {
    telegram::BotInfo info;
    if (!client_.getMe(info)) {
        if (client_.lastError().kind == Error::Kind::Unauthorized) {
            state_ = State::TokenRejected;
            log_.error("Telegram rejected the bot token. Check BOT_TOKEN in setup.txt.");
            return 0;
        }
        warnTelegram("Couldn't reach Telegram");
        const int retryAfter = client_.lastError().retryAfterSeconds;
        return retryAfter > 0 ? static_cast<uint32_t>(retryAfter) * 1000 : kRetryMs;
    }

    botId_ = info.id;
    state_ = State::Connected;
    log_.info("Connected to Telegram as @" + info.username);
    return 0;
}

void Bot::resume() {
    if (state_ != State::Connected) return;

    // The saved position only means something for the bot it came from. A new
    // bot token (from a new setup.txt) starts again from that bot's first message.
    const int64_t savedBot = store_.getInt(kBotId, 0);
    if (savedBot == botId_) {
        savedPosition_ = store_.getInt(kPosition, 0);
    } else {
        if (savedBot != 0) log_.info("The bot token has changed, so starting from the new bot's first message");
        store_.setInt(kBotId, botId_);
        store_.remove(kPosition);
        store_.remove(kBusy);
        savedPosition_ = 0;
    }
    client_.setNextUpdateId(savedPosition_);
    state_ = State::Running;
}

uint32_t Bot::poll(std::vector<Update>& updates) {
    if (state_ != State::Running) return kRetryMs;
    if (client_.getUpdates(updates)) return 0;

    const Error& e = client_.lastError();
    if (e.kind == Error::Kind::Unauthorized) {
        state_ = State::TokenRejected;
        log_.error("Telegram rejected the bot token. Check BOT_TOKEN in setup.txt.");
        return 0;
    }
    warnTelegram("Couldn't get messages");
    if (e.kind == Error::Kind::RateLimited && e.retryAfterSeconds > 0) {
        return static_cast<uint32_t>(e.retryAfterSeconds) * 1000;
    }
    return kRetryMs;
}

uint32_t Bot::step() {
    switch (state_) {
        case State::NotConnected: {
            const uint32_t wait = connect();
            resume();
            return wait;
        }
        case State::Connected: resume(); break;
        case State::TokenRejected: return 12 * kRetryMs;  // nothing more to do until restarted with a new token
        case State::Running: break;
    }
    std::vector<Update> updates;
    const uint32_t wait = poll(updates);
    handle(updates);
    return wait;
}

void Bot::handle(const std::vector<Update>& updates) {
    for (const auto& update : updates) {
        if (update.updateId < savedPosition_) continue;  // already handled before a restart

        // Count attempts, so a message that makes the box crash or restart is
        // only tried kMaxAttempts times rather than for ever.
        int64_t attempts = 0;
        const auto busy = text::splitFields(store_.getString(kBusy));
        int64_t busyId = 0, busyAttempts = 0;
        if (busy.size() == 2 && text::parseInt(busy[0], busyId) && text::parseInt(busy[1], busyAttempts) &&
            busyId == update.updateId) {
            attempts = busyAttempts;
        }

        if (attempts >= kMaxAttempts) {
            log_.error("Skipped a message from Telegram because the box restarted while handling it " +
                       std::to_string(attempts) + " times");
        } else {
            store_.setString(kBusy, text::joinFields({std::to_string(update.updateId), std::to_string(attempts + 1)}));
            handleUpdate(update);
        }
        savePosition(update.updateId + 1);
    }
    // Also move past anything the client skipped because it couldn't be read.
    savePosition(client_.nextUpdateId());

    if (!updates.empty()) {
        const int64_t now = clock_.isTimeSet() ? clock_.unixTime() : 0;
        const auto expired = conversations_.removeExpired(now, kConversationTimeoutSeconds);
        if (!expired.empty()) log_.info(std::to_string(expired.size()) + " unfinished conversation(s) timed out");
        for (const auto& state : expired) timedOut(state, now);
    }
}

void Bot::savePosition(int64_t nextUpdateId) {
    if (nextUpdateId <= savedPosition_) return;
    if (!store_.setInt(kPosition, nextUpdateId)) log_.warning("Couldn't save how far through Telegram's messages the box is");
    savedPosition_ = nextUpdateId;
}

void Bot::handleUpdate(const Update& update) {
    hasCurrent_ = false;
    switch (update.type) {
        case Update::Type::Message: handleMessage(update.message); break;
        case Update::Type::CallbackQuery: handleButton(update.callbackQuery); break;
        case Update::Type::Other: break;
    }
    hasCurrent_ = false;
}

// ---------------------------------------------------------------------------
// Messages

void Bot::handleMessage(const Message& message) {
    // The box only ever talks one-to-one with people, never in groups or to other bots.
    if (!message.isPrivateChat() || message.from.isBot || message.from.id == 0) return;

    Context ctx(*this);
    ctx.sender_.userId = message.from.id;
    ctx.sender_.chatId = message.chatId;
    ctx.sender_.firstName = message.from.firstName;
    ctx.sender_.lastName = message.from.lastName;
    ctx.sender_.username = message.from.username;
    ctx.sender_.roles = Roles::of(storage_.people, message.from.id);
    loadConversation(ctx.sender_);

    std::string command, argument;
    const bool isCommand = message.kind == MessageKind::Text && telegram::parseCommand(message.text, command, argument);

    if (!ctx.sender_.roles.isKnown() && !hasCurrent_) {
        // A stranger. Only a valid invite link gets a response, or someone the
        // stranger handler knows from an earlier invite; everything else is ignored.
        if (isCommand && command == "start" && !argument.empty() && startLink_ && startLink_(ctx, argument)) return;
        if (onStranger_) onStranger_(ctx);
        return;
    }

    if (isCommand) {
        handleCommand(ctx, command, argument, message);
    } else if (hasCurrent_) {
        ConversationType* type = typeNamed(current_.type);
        const uint32_t id = current_.id;
        runStep(ctx, type->onMessage(ctx, current_, message), id);
    } else if (onMessage_) {
        onMessage_(ctx, message);
    } else {
        ctx.reply(kHelpHint);
    }
}

void Bot::handleCommand(Context& ctx, const std::string& command, const std::string& argument,
                        const Message& message) {
    const Roles roles = ctx.sender().roles;

    if (command == "cancel") {
        if (hasCurrent_) {
            endConversation();
            ctx.reply(kStopped);
        } else {
            ctx.reply(kNothingToStop);
        }
        return;
    }

    if (command == "start" && !argument.empty() && startLink_ && startLink_(ctx, argument)) return;

    if (!roles.isKnown()) {
        // Someone partway through an invite. They're not on any list yet, so
        // commands mean nothing; let their conversation deal with it.
        if (!hasCurrent_) return;
        ConversationType* type = typeNamed(current_.type);
        const uint32_t id = current_.id;
        runStep(ctx, type->onMessage(ctx, current_, message), id);
        return;
    }

    auto it = commands_.find(command);
    if (it == commands_.end()) {
        ctx.reply("I don't know /" + command + ". " + kHelpHint);
        return;
    }
    if (!roles.any(it->second.roles)) {
        log_.info("Turned down /" + command + " from someone without the right role");
        ctx.reply(it->second.roles == Roles::kAdmin ? "Only admins can use /" + command + "."
                                                    : "You can't use /" + command + ".");
        return;
    }

    if (hasCurrent_) endConversation();  // a new command replaces whatever they were doing
    it->second.handler(ctx, argument);
}

// ---------------------------------------------------------------------------
// Buttons

void Bot::handleButton(const CallbackQuery& query) {
    // Only buttons in a private chat with the person who pressed them.
    if (query.from.id == 0 || query.from.isBot || query.chatId != query.from.id) return;

    Context ctx(*this);
    ctx.sender_.userId = query.from.id;
    ctx.sender_.chatId = query.chatId;
    ctx.sender_.firstName = query.from.firstName;
    ctx.sender_.lastName = query.from.lastName;
    ctx.sender_.username = query.from.username;
    ctx.sender_.roles = Roles::of(storage_.people, query.from.id);
    ctx.buttonMessageId_ = query.messageId;
    loadConversation(ctx.sender_);

    const Roles roles = ctx.sender_.roles;
    // Strangers are ignored completely, not even an answer to the press,
    // unless they're partway through an invite or the stranger handler knows them.
    if (!roles.isKnown() && !hasCurrent_) {
        if (onStranger_ && onStranger_(ctx) && !client_.answerCallbackQuery(query.id, kFinished)) {
            warnTelegram("Couldn't answer a button press");
        }
        return;
    }

    if (!query.data.empty() && query.data[0] == kConversationButton) {
        handleConversationButton(ctx, query.data);
    } else if (roles.isKnown()) {
        const size_t colon = query.data.find(':');
        const std::string prefix = query.data.substr(0, colon);
        const std::string rest = colon == std::string::npos ? "" : query.data.substr(colon + 1);
        auto it = buttons_.find(prefix);
        if (it == buttons_.end()) {
            ctx.popup(kButtonGone);
        } else if (!roles.any(it->second.roles)) {
            log_.info("Turned down a button press from someone without the right role");
            ctx.popup(it->second.roles == Roles::kAdmin ? "Only admins can do that." : "You can't do that.");
        } else {
            it->second.handler(ctx, rest);
        }
    } else {
        ctx.popup(kButtonGone);
    }

    // Every press needs an answer, or the button shows a spinner on their phone.
    if (!client_.answerCallbackQuery(query.id, ctx.popup_)) warnTelegram("Couldn't answer a button press");
}

void Bot::handleConversationButton(Context& ctx, const std::string& data) {
    const size_t colon = data.find(':');
    uint64_t id = 0;
    if (colon == std::string::npos || !text::parseUint(data.substr(1, colon - 1), id) || !hasCurrent_ ||
        id != current_.id) {
        ctx.popup(kFinished);
        return;
    }
    ConversationType* type = typeNamed(current_.type);
    const uint32_t before = current_.id;
    runStep(ctx, type->onButton(ctx, current_, data.substr(colon + 1)), before);
}

// ---------------------------------------------------------------------------
// Conversations

void Bot::loadConversation(const Sender& sender) {
    hasCurrent_ = false;
    ConversationState state;
    if (!conversations_.find(sender.userId, state)) return;

    const int64_t now = clock_.isTimeSet() ? clock_.unixTime() : 0;
    ConversationType* type = typeNamed(state.type);
    if (now > 0 && state.lastActive > 0 && now - state.lastActive > kConversationTimeoutSeconds) {
        log_.info("An unfinished conversation timed out");
        conversations_.remove(sender.userId);
        timedOut(state, now);
        return;
    } else if (!type) {
        log_.warning("Dropped a conversation of a kind this version doesn't know: " + state.type);
    } else if (!type->allows(sender.roles)) {
        log_.info("Dropped a conversation the person is no longer allowed to be in");
    } else {
        current_ = state;
        hasCurrent_ = true;
        return;
    }
    conversations_.remove(sender.userId);
}

void Bot::timedOut(const ConversationState& state, int64_t now) {
    if (ConversationType* type = typeNamed(state.type)) type->onTimedOut(state, now);
}

bool Bot::startConversation(Context& ctx, const std::string& typeName,
                            const std::map<std::string, std::string>& data) {
    ConversationType* type = typeNamed(typeName);
    if (!type) {
        log_.error("No conversation called " + typeName);
        return false;
    }
    if (!type->allows(ctx.sender().roles)) return false;

    if (hasCurrent_) endConversation();
    current_ = ConversationState();
    current_.id = conversations_.nextId();
    current_.type = typeName;
    current_.userId = ctx.sender().userId;
    current_.data = data;
    hasCurrent_ = true;

    const uint32_t id = current_.id;
    runStep(ctx, type->begin(ctx, current_), id);
    return true;
}

void Bot::runStep(Context& ctx, ConversationType::Outcome outcome, uint32_t idBefore) {
    // If the step started a different conversation, that one has already been
    // saved and this step's outcome belongs to the old one.
    if (!hasCurrent_ || current_.id != idBefore) return;
    if (outcome == ConversationType::Outcome::Finished) {
        endConversation();
        return;
    }
    current_.lastActive = ctx.now();
    saveConversation();
}

void Bot::endConversation() {
    if (!hasCurrent_) return;
    conversations_.remove(current_.userId);
    hasCurrent_ = false;
}

void Bot::saveConversation() {
    if (!conversations_.save(current_)) log_.warning("Couldn't save a conversation; it won't survive a restart");
}

void Bot::warnTelegram(const std::string& what) {
    const Error& e = client_.lastError();
    log_.warning(what + " (" + telegram::toString(e.kind) + ": " + e.description + ")");
}

}  // namespace fvb

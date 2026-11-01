#include "app/screens/Screens.h"

#include <algorithm>

#include "app/screens/ScreenLayout.h"
#include "app/storage/Paths.h"
#include "app/time/TimeChecks.h"

namespace fvb {

using namespace layout;

namespace {

// Which third of the screen's width x is in: 0, 1 or 2, matching the buttons.
int third(int16_t x) {
    if (x < kWidth / 3) return 0;
    if (x < kWidth * 2 / 3) return 1;
    return 2;
}

}  // namespace

// ---------------------------------------------------------------------------
// Start-up and the loop
// ---------------------------------------------------------------------------

void Screens::begin() {
    hw_.audio.setVolume(storage_.settings.voiceVolume());
    show(Screen::Starting);
    draw();
    dirty_ = false;
}

void Screens::tick() {
    const uint64_t now = hw_.clock.millisSinceBoot();

    if (screen_ == Screen::Starting && now - screenSince_ >= kStartingScreenMs) {
        goHome();
    }

    checkPlaybackEnded();

    const bool reviewing =
        screen_ == Screen::Review || screen_ == Screen::ConfirmDelete || screen_ == Screen::ConfirmRerecord;
    if (reviewing && playing_ == Playing::Nothing && now - lastActivity_ >= kReviewTimeoutMs) {
        hw_.log.info("Recording left on review, moved to the bin");
        binRecording();
        goHome();
    }

    if ((screen_ == Screen::Home || screen_ == Screen::NoContacts) && now - lastRefresh_ >= kRefreshMs) {
        reloadContacts();
        if (screen_ == Screen::NoContacts && !contacts_.empty()) show(Screen::Home);
        if (screen_ == Screen::Home && contacts_.empty()) show(Screen::NoContacts);
        dirty_ = true;
    }

    const bool moving = screen_ == Screen::Recording || playing_ != Playing::Nothing;
    if (now - lastFrame_ >= kFrameMs) {
        lastFrame_ = now;
        if (screen_ == Screen::Recording) {
            const uint64_t limitMs = static_cast<uint64_t>(storage_.settings.maxRecordingSeconds()) * 1000;
            if (now - recordingStarted_ >= limitMs) {
                hw_.log.info("Recording reached the time limit");
                stopRecording();
            }
        }
        if (moving) dirty_ = true;
        updateLeds(now);
    }

    if (dirty_) {
        dirty_ = false;
        draw();
    }
}

void Screens::handle(const InputEvent& event) {
    lastActivity_ = hw_.clock.millisSinceBoot();
    switch (screen_) {
        case Screen::Starting:
            break;  // ignore anything pressed while starting up
        case Screen::NoContacts:
            break;
        case Screen::Home:
            handleHome(event);
            break;
        case Screen::Recording:
            handleRecording(event);
            break;
        case Screen::Review:
            handleReview(event);
            break;
        case Screen::ConfirmRerecord:
        case Screen::ConfirmDelete:
            handleConfirm(event);
            break;
        case Screen::History:
            handleHistory(event);
            break;
    }
}

// ---------------------------------------------------------------------------
// Moving between screens
// ---------------------------------------------------------------------------

void Screens::show(Screen screen) {
    screen_ = screen;
    screenSince_ = hw_.clock.millisSinceBoot();
    lastActivity_ = screenSince_;
    dirty_ = true;
}

void Screens::goHome() {
    reloadContacts();
    show(contacts_.empty() ? Screen::NoContacts : Screen::Home);
}

// Reads every contact's index file. Fine on the laptop; on the device, with up
// to 30 contacts on the SD card every two seconds, ticket 2.5 may want the bot
// to say when something changed instead.
void Screens::reloadContacts() {
    lastRefresh_ = hw_.clock.millisSinceBoot();
    contacts_.clear();
    anyUnread_ = false;

    const bool timeKnown = hw_.clock.isTimeSet();
    const int64_t nowUnix = timeKnown ? hw_.clock.unixTime() : 0;
    for (const Contact& contact : storage_.people.contacts()) {
        ContactView view;
        view.contact = contact;
        const auto messages = storage_.messages.messages(contact.number);
        view.hasMessages = !messages.empty();
        view.unread = static_cast<size_t>(std::count_if(messages.begin(), messages.end(), [](const StoredMessage& m) {
            return m.status == StoredMessage::Status::Unread;
        }));
        view.asleep = timeKnown && isContactAsleep(contact, nowUnix);
        anyUnread_ = anyUnread_ || view.unread > 0;
        contacts_.push_back(view);
    }

    if (contacts_.empty()) {
        currentNumber_ = 0;
    } else if (current() == nullptr) {
        currentNumber_ = contacts_.front().contact.number;  // first time, or the contact was removed
    }
}

const Screens::ContactView* Screens::current() const {
    for (const auto& view : contacts_) {
        if (view.contact.number == currentNumber_) return &view;
    }
    return nullptr;
}

uint32_t Screens::currentContact() const { return currentNumber_; }

void Screens::moveContact(int step) {
    if (contacts_.empty()) return;
    stopPlaying();
    const int count = static_cast<int>(contacts_.size());
    int index = 0;
    for (int i = 0; i < count; ++i) {
        if (contacts_[i].contact.number == currentNumber_) index = i;
    }
    index = ((index + step) % count + count) % count;  // wraps round both ways
    currentNumber_ = contacts_[index].contact.number;
    dirty_ = true;
}

int64_t Screens::unixNow() const { return hw_.clock.unixTime(); }

// ---------------------------------------------------------------------------
// Home
// ---------------------------------------------------------------------------

void Screens::handleHome(const InputEvent& event) {
    switch (event.type) {
        case InputEvent::Type::ButtonPress:
            if (event.button == Button::Left) moveContact(-1);
            if (event.button == Button::Right) moveContact(1);
            if (event.button == Button::Middle) startRecording();
            break;
        case InputEvent::Type::Swipe:
            if (event.swipe == SwipeDirection::Left) moveContact(1);
            if (event.swipe == SwipeDirection::Right) moveContact(-1);
            if (event.swipe == SwipeDirection::Up) {
                stopPlaying();
                historySelected_ = 0;
                historyTop_ = 0;
                loadHistory();
                show(Screen::History);
            }
            break;
        case InputEvent::Type::Tap:
            if (event.x < kHistoryIconRight && event.y < kHistoryIconBottom) {
                InputEvent up;
                up.type = InputEvent::Type::Swipe;
                up.swipe = SwipeDirection::Up;
                handleHome(up);
            } else if (insideCircle(event.x, event.y, kPhotoX, kPhotoY, kPhotoRadius)) {
                playOnHome();
            }
            break;
    }
}

void Screens::playOnHome() {
    const ContactView* view = current();
    if (view == nullptr) return;
    if (playing_ != Playing::Nothing) {
        stopPlaying();
        return;
    }
    const auto messages = storage_.messages.messages(view->contact.number);  // oldest first
    if (messages.empty()) return;

    // Oldest unread first, so nothing is skipped. Otherwise the newest.
    auto it = std::find_if(messages.begin(), messages.end(),
                           [](const StoredMessage& m) { return m.status == StoredMessage::Status::Unread; });
    playMessage(view->contact.number, it != messages.end() ? *it : messages.back());
}

// ---------------------------------------------------------------------------
// Recording and review
// ---------------------------------------------------------------------------

void Screens::startRecording() {
    if (current() == nullptr) return;
    stopPlaying();
    hw_.files.makeDir(paths::kWorkDir);
    if (!hw_.audio.startRecording(paths::kRecording)) {
        hw_.log.warning("Couldn't start recording");
        return;
    }
    recordingStarted_ = hw_.clock.millisSinceBoot();
    recordedMs_ = 0;
    show(Screen::Recording);
}

void Screens::stopRecording() {
    recordedMs_ = hw_.audio.stopRecording();
    if (recordedMs_ < kShortestRecordingMs) {
        // An accidental tap, or the recording failed. Not worth keeping or binning.
        hw_.log.info("Recording too short (" + std::to_string(recordedMs_) + " ms), thrown away");
        hw_.files.removeFile(paths::kRecording);
        goHome();
        return;
    }
    show(Screen::Review);
}

void Screens::handleRecording(const InputEvent& event) {
    if (event.type == InputEvent::Type::ButtonPress || event.type == InputEvent::Type::Tap) stopRecording();
}

void Screens::handleReview(const InputEvent& event) {
    enum class Action { None, Listen, Send, Delete, Rerecord } action = Action::None;
    if (event.type == InputEvent::Type::ButtonPress) {
        if (event.button == Button::Left) action = Action::Listen;
        if (event.button == Button::Middle) action = Action::Send;
        if (event.button == Button::Right) action = Action::Delete;
    } else if (event.type == InputEvent::Type::Tap) {
        if (event.x >= kRerecordLeft && event.y < kRerecordBottom) {
            action = Action::Rerecord;
        } else if (event.y >= kReviewActionsTop) {
            const Action byThird[3] = {Action::Listen, Action::Send, Action::Delete};
            action = byThird[third(event.x)];
        }
    }

    switch (action) {
        case Action::None:
            break;
        case Action::Listen:
            if (playing_ == Playing::Recording) {
                stopPlaying();
            } else {
                playRecording();
            }
            break;
        case Action::Send:
            sendRecording();
            break;
        case Action::Delete:
            stopPlaying();
            show(Screen::ConfirmDelete);
            break;
        case Action::Rerecord:
            stopPlaying();
            show(Screen::ConfirmRerecord);
            break;
    }
}

void Screens::sendRecording() {
    stopPlaying();
    if (storage_.queueRecording(currentNumber_, paths::kRecording, recordedMs_, unixNow()) == 0) {
        // Stay on review so the recording isn't lost. Shouldn't happen unless the card is full or broken.
        hw_.log.error("Couldn't save the recording to the outbox");
        return;
    }
    hw_.log.info("Recording saved to the outbox");
    goHome();
}

void Screens::binRecording() {
    if (storage_.binRecording(currentNumber_, paths::kRecording, unixNow()).empty()) {
        hw_.log.error("Couldn't move the recording to the bin");
        hw_.files.removeFile(paths::kRecording);
    }
}

void Screens::handleConfirm(const InputEvent& event) {
    bool yes = false;
    bool no = false;
    if (event.type == InputEvent::Type::ButtonPress) {
        yes = event.button == Button::Right;
        no = !yes;
    } else if (event.type == InputEvent::Type::Tap && event.y >= kConfirmAnswersTop) {
        yes = event.x >= kWidth / 2;
        no = !yes;
    }

    if (no) {
        show(Screen::Review);
    } else if (yes) {
        const bool rerecord = screen_ == Screen::ConfirmRerecord;
        binRecording();
        if (rerecord) {
            startRecording();
            if (screen_ != Screen::Recording) goHome();  // the mic wouldn't start
        } else {
            goHome();
        }
    }
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void Screens::handleHistory(const InputEvent& event) {
    switch (event.type) {
        case InputEvent::Type::ButtonPress:
            if (event.button == Button::Left) moveHistorySelection(-1);
            if (event.button == Button::Right) moveHistorySelection(1);
            if (event.button == Button::Middle) playHistorySelection();
            break;
        case InputEvent::Type::Swipe:
            if (event.swipe == SwipeDirection::Right) {
                stopPlaying();
                goHome();
            }
            if (event.swipe == SwipeDirection::Up) moveHistorySelection(static_cast<int>(kHistoryRowsShown) - 1);
            if (event.swipe == SwipeDirection::Down) moveHistorySelection(-static_cast<int>(kHistoryRowsShown) + 1);
            break;
        case InputEvent::Type::Tap:
            if (event.y < kHistoryHeader) {
                if (event.x < kBackRight) {
                    stopPlaying();
                    goHome();
                }
            } else {
                const size_t row = historyTop_ + static_cast<size_t>((event.y - kHistoryHeader) / kHistoryRow);
                if (row < history_.size()) {
                    historySelected_ = row;
                    playHistorySelection();
                }
            }
            break;
    }
}

void Screens::loadHistory() {
    history_ = storage_.messages.messages(currentNumber_);
    std::reverse(history_.begin(), history_.end());  // newest first
    if (historySelected_ >= history_.size()) historySelected_ = history_.empty() ? 0 : history_.size() - 1;
    dirty_ = true;
}

void Screens::moveHistorySelection(int step) {
    if (history_.empty()) return;
    const int last = static_cast<int>(history_.size()) - 1;
    const int selected = std::max(0, std::min(last, static_cast<int>(historySelected_) + step));
    historySelected_ = static_cast<size_t>(selected);
    if (historySelected_ < historyTop_) historyTop_ = historySelected_;
    if (historySelected_ >= historyTop_ + kHistoryRowsShown) historyTop_ = historySelected_ - kHistoryRowsShown + 1;
    dirty_ = true;
}

void Screens::playHistorySelection() {
    if (historySelected_ >= history_.size()) return;
    const StoredMessage& message = history_[historySelected_];
    if (playing_ == Playing::Message && playingMessage_ == message.id) {
        stopPlaying();
        return;
    }
    stopPlaying();
    playMessage(currentNumber_, message);
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

void Screens::playMessage(uint32_t contact, const StoredMessage& message) {
    hw_.audio.setVolume(storage_.settings.voiceVolume());
    if (!hw_.audio.play(storage_.messages.audioPath(contact, message))) {
        hw_.log.warning("Couldn't play message " + std::to_string(message.id));
        return;
    }
    playing_ = Playing::Message;
    playingContact_ = contact;
    playingMessage_ = message.id;
    dirty_ = true;
}

void Screens::playRecording() {
    hw_.audio.setVolume(storage_.settings.voiceVolume());
    if (!hw_.audio.play(paths::kRecording)) {
        hw_.log.warning("Couldn't play the recording back");
        return;
    }
    playing_ = Playing::Recording;
    dirty_ = true;
}

void Screens::stopPlaying() {
    if (playing_ == Playing::Nothing) return;
    // Cleared first, so checkPlaybackEnded doesn't take this for playing to the end.
    playing_ = Playing::Nothing;
    hw_.audio.stopPlayback();
    dirty_ = true;
}

void Screens::checkPlaybackEnded() {
    if (playing_ == Playing::Nothing || hw_.audio.isPlaying()) return;

    // It finished by itself, so it was heard to the end.
    const Playing was = playing_;
    playing_ = Playing::Nothing;
    lastActivity_ = hw_.clock.millisSinceBoot();
    dirty_ = true;
    if (was != Playing::Message) return;

    StoredMessage message;
    if (storage_.messages.find(playingContact_, playingMessage_, message) &&
        message.direction == StoredMessage::Direction::Received &&
        message.status == StoredMessage::Status::Unread) {
        storage_.messages.markHeard(playingContact_, playingMessage_);
        hw_.log.info("Message " + std::to_string(playingMessage_) + " heard");
        reloadContacts();
        if (screen_ == Screen::History) loadHistory();
    }
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

void Screens::updateLeds(uint64_t now) {
    // Rough: recording shows the voice level as a green bar, unread messages make
    // the LEDs pulse blue, and otherwise they're off. Patterns are decided on the
    // device (ticket 2.5); this just makes the desktop window show something.
    uint8_t mode = 0;
    uint8_t value = 0;
    if (screen_ == Screen::Recording) {
        mode = 1;
        value = static_cast<uint8_t>((hw_.audio.inputLevel() * hw_.leds.count() + 50) / 100);
    } else if (anyUnread_) {
        mode = 2;
        const uint32_t phase = static_cast<uint32_t>(now % 2000);  // two seconds up and down
        const uint32_t up = phase < 1000 ? phase : 2000 - phase;
        value = static_cast<uint8_t>(20 + up * 180 / 1000);
    }
    if (mode == ledMode_ && value == ledValue_) return;
    ledMode_ = mode;
    ledValue_ = value;

    if (mode == 1) {
        for (uint8_t i = 0; i < hw_.leds.count(); ++i) {
            hw_.leds.setPixel(i, i < value ? Colour{0, 200, 60} : Colour{0, 0, 0});
        }
    } else if (mode == 2) {
        hw_.leds.setAll(Colour{0, static_cast<uint8_t>(value / 3), value});
    } else {
        hw_.leds.setAll(Colour{0, 0, 0});
    }
    hw_.leds.show();
}

}  // namespace fvb

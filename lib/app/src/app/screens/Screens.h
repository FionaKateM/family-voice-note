#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app/storage/Storage.h"
#include "hal/Hardware.h"

namespace fvb {

// What the child sees and touches: the home screen (one contact at a time),
// recording, review and history.
//
// ROUGH DRAFTS (ticket 1.11). These exist so the whole flow can be tried on the
// laptop. The real look and controls are decided on the device in ticket 2.5.
// The rules from section 4 of the requirements are the part to keep: recordings
// under 2 seconds are thrown away, the time limit stops recording, re-record and
// delete ask first and move the recording to the bin, Send goes to the outbox
// and straight back home, and playing a received message to the end marks it
// heard.
//
// Controls (Left, Middle and Right are the three buttons under the screen):
//
//   Home       Left/Right or swipe: previous/next contact. Middle: record.
//              Tap the photo: play (the oldest unread message, otherwise the
//              newest message). Tap again to stop. Tap the list icon (top left)
//              or swipe up: history.
//   Recording  Any button or tap: stop.
//   Review     Left: listen. Middle: send. Right: delete. Tap the icon at the
//              top right: re-record.
//   Confirm    Right (tick): yes. Left or Middle (cross): no.
//              Review and confirm left alone for 2 minutes: binned, back home.
//   History    Left/Right: move up/down the list. Middle: play or stop the
//              highlighted message. Tap a message to play it. Tap the back
//              arrow or swipe right: home.
//
// Everything here runs from App::tick, so it shares the box's lock with the bot.
class Screens {
public:
    enum class Screen : uint8_t {
        Starting,         // name and version, briefly
        NoContacts,       // nobody to talk to yet: a note for the parent
        Home,
        Recording,
        Review,
        ConfirmRerecord,
        ConfirmDelete,
        History,
    };

    static constexpr uint32_t kStartingScreenMs = 1500;
    // Recordings shorter than this are taken to be accidental taps.
    // Requirements section 4 says to tune this on the device.
    static constexpr uint32_t kShortestRecordingMs = 2000;
    // How often moving things are redrawn while recording or playing. The
    // requirements cap screen and LED updates at 15 to 20 a second while recording.
    static constexpr uint32_t kFrameMs = 66;
    // How often the home screen looks again at the contacts list, unread counts
    // and who is asleep, in case the bot changed something.
    static constexpr uint32_t kRefreshMs = 2000;
    // A recording left on review (or its yes/no question) this long with nothing
    // pressed is binned and the box goes home. Not while it's playing back.
    static constexpr uint32_t kReviewTimeoutMs = 2 * 60 * 1000;
    static constexpr size_t kHistoryRowsShown = 5;

    Screens(Hardware& hardware, Storage& storage) : hw_(hardware), storage_(storage) {}

    // Shows the starting screen. Call once from App::begin.
    void begin();
    // Call every loop. Moves on from the starting screen, notices when playback
    // or recording ends, and redraws when needed.
    void tick();
    void handle(const InputEvent& event);

    // ---- For tests ----
    Screen screen() const { return screen_; }
    // The contact on screen. 0 if there are none.
    uint32_t currentContact() const;
    // The message being played (0 if nothing is, or if it's the review recording).
    uint32_t playingMessage() const { return playing_ == Playing::Message ? playingMessage_ : 0; }
    bool isPlayingRecording() const { return playing_ == Playing::Recording; }
    size_t historySelection() const { return historySelected_; }
    uint32_t reviewDurationMs() const { return recordedMs_; }

private:
    enum class Playing : uint8_t { Nothing, Message, Recording };

    // Contacts and what's shown about them, reloaded every kRefreshMs.
    struct ContactView {
        Contact contact;
        size_t unread = 0;
        bool asleep = false;
        bool hasMessages = false;
    };

    // ---- Moving between screens ----
    void show(Screen screen);
    void goHome();
    void reloadContacts();
    void moveContact(int step);

    // ---- Per-screen input ----
    void handleHome(const InputEvent& event);
    void handleRecording(const InputEvent& event);
    void handleReview(const InputEvent& event);
    void handleConfirm(const InputEvent& event);
    void handleHistory(const InputEvent& event);

    // ---- Actions ----
    void startRecording();
    void stopRecording();
    void sendRecording();
    void binRecording();
    void playOnHome();
    void playMessage(uint32_t contact, const StoredMessage& message);
    void playRecording();
    void stopPlaying();
    void checkPlaybackEnded();
    void loadHistory();
    void moveHistorySelection(int step);
    void playHistorySelection();

    // ---- LEDs ----
    void updateLeds(uint64_t now);

    // ---- Drawing (ScreenDrawing.cpp) ----
    void draw();
    void drawStarting();
    void drawNoContacts();
    void drawHome();
    void drawRecording();
    void drawReview();
    void drawConfirm();
    void drawHistory();
    void drawPhoto(const Contact& contact, int16_t centreX, int16_t centreY, int16_t radius, bool dimmed);
    std::string messageTimeText(int64_t time) const;

    const ContactView* current() const;
    int64_t unixNow() const;

    Hardware& hw_;
    Storage& storage_;

    Screen screen_ = Screen::Starting;
    bool dirty_ = true;
    uint64_t screenSince_ = 0;  // millisSinceBoot when this screen was shown
    uint64_t lastActivity_ = 0;  // last input, screen change or end of playback
    uint64_t lastRefresh_ = 0;
    uint64_t lastFrame_ = 0;

    std::vector<ContactView> contacts_;
    bool anyUnread_ = false;
    uint32_t currentNumber_ = 0;  // contact number, so a new contact doesn't move the child along

    Playing playing_ = Playing::Nothing;
    uint32_t playingContact_ = 0;
    uint32_t playingMessage_ = 0;

    uint64_t recordingStarted_ = 0;
    uint32_t recordedMs_ = 0;

    std::vector<StoredMessage> history_;  // newest first
    size_t historySelected_ = 0;
    size_t historyTop_ = 0;  // first row shown

    // What the LEDs were last set to, so they're only sent when something changes.
    uint8_t ledMode_ = 0xff;
    uint8_t ledValue_ = 0xff;
};

}  // namespace fvb

// Drawing for the rough screens (see Screens.h). Plain shapes only: no photos
// or proper icons yet. Nothing a child needs depends on the text; the text is
// for parents and for trying things out.

#include <cmath>
#include <cstdio>
#include <string>

#include "app/Version.h"
#include "app/screens/ScreenLayout.h"
#include "app/screens/Screens.h"
#include "app/storage/Bin.h"
#include "app/time/PosixTimeZone.h"
#include "app/time/TimeZones.h"

namespace fvb {

using namespace layout;

namespace {

const Colour kBackground{18, 22, 38};
const Colour kPanel{48, 58, 92};
const Colour kWhite{255, 255, 255};
const Colour kGrey{140, 146, 165};
const Colour kRed{225, 50, 50};
const Colour kDarkRed{70, 8, 12};
const Colour kGreen{40, 175, 80};
const Colour kBlue{50, 120, 230};
const Colour kOrange{240, 150, 30};
const Colour kYellow{245, 210, 60};

// A colour per contact, so they're easy to tell apart before photos exist.
const Colour kContactColours[] = {
    {214, 96, 77}, {66, 146, 198}, {116, 170, 80}, {170, 110, 200},
    {230, 160, 50}, {60, 170, 160}, {200, 90, 150}, {120, 130, 210},
};

Colour dim(Colour c) { return Colour{static_cast<uint8_t>(c.r / 3), static_cast<uint8_t>(c.g / 3), static_cast<uint8_t>(c.b / 3)}; }

// A straight line `thickness` pixels wide, made of two triangles.
void thickLine(Display& d, float x0, float y0, float x1, float y1, float thickness, Colour colour) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length == 0) return;
    const float ox = -dy / length * thickness / 2;
    const float oy = dx / length * thickness / 2;
    auto px = [](float v) { return static_cast<int16_t>(std::lround(v)); };
    d.fillTriangle(px(x0 + ox), px(y0 + oy), px(x1 + ox), px(y1 + oy), px(x1 - ox), px(y1 - oy), colour);
    d.fillTriangle(px(x0 + ox), px(y0 + oy), px(x1 - ox), px(y1 - oy), px(x0 - ox), px(y0 - oy), colour);
}

void centredText(Display& d, int16_t centreX, int16_t y, const std::string& text, Colour colour, uint8_t size) {
    d.drawText(static_cast<int16_t>(centreX - d.textWidth(text, size) / 2), y, text, colour, size);
}

// Pointing right if `right`, otherwise left.
void playTriangle(Display& d, int16_t cx, int16_t cy, int16_t size, bool right, Colour colour) {
    const int16_t s = size;
    if (right) {
        d.fillTriangle(cx - s / 2, cy - s, cx - s / 2, cy + s, cx + s, cy, colour);
    } else {
        d.fillTriangle(cx + s / 2, cy - s, cx + s / 2, cy + s, cx - s, cy, colour);
    }
}

void stopSquare(Display& d, int16_t cx, int16_t cy, int16_t half, Colour colour) {
    d.fillRect(cx - half, cy - half, half * 2, half * 2, colour);
}

void cross(Display& d, int16_t cx, int16_t cy, int16_t half, Colour colour) {
    thickLine(d, cx - half, cy - half, cx + half, cy + half, half / 2.5f, colour);
    thickLine(d, cx - half, cy + half, cx + half, cy - half, half / 2.5f, colour);
}

void tickMark(Display& d, int16_t cx, int16_t cy, int16_t half, Colour colour) {
    thickLine(d, cx - half, cy, cx - half / 3, cy + half * 2 / 3, half / 2.5f, colour);
    thickLine(d, cx - half / 3, cy + half * 2 / 3, cx + half, cy - half * 2 / 3, half / 2.5f, colour);
}

void recordIcon(Display& d, int16_t cx, int16_t cy, int16_t radius) {
    d.fillCircle(cx, cy, radius, kWhite);
    d.fillCircle(cx, cy, radius - 4, kRed);
}

// Re-record: an orange ring round a record dot.
void rerecordIcon(Display& d, int16_t cx, int16_t cy, int16_t radius, Colour background) {
    d.fillCircle(cx, cy, radius, kOrange);
    d.fillCircle(cx, cy, radius - 5, background);
    d.fillCircle(cx, cy, radius / 2, kRed);
    d.fillTriangle(cx + radius - 10, cy - radius - 2, cx + radius + 6, cy - radius + 8, cx + radius - 8, cy - radius + 14,
                   kOrange);
}

void binIcon(Display& d, int16_t cx, int16_t cy, int16_t radius) {
    d.fillCircle(cx, cy, radius, kRed);
    cross(d, cx, cy, radius / 2, kWhite);
}

std::string minutesSeconds(uint32_t ms) {
    const uint32_t seconds = (ms + 500) / 1000;
    char text[16];
    std::snprintf(text, sizeof(text), "%u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
    return text;
}

std::string firstLetter(const std::string& name) {
    if (name.empty()) return "?";
    const unsigned char c = static_cast<unsigned char>(name[0]);
    if (c >= 'a' && c <= 'z') return std::string(1, static_cast<char>(c - 'a' + 'A'));
    if (c < 0x80) return std::string(1, static_cast<char>(c));
    // A letter outside plain ASCII: keep the whole UTF-8 character together.
    size_t length = 1;
    while (length < name.size() && (static_cast<unsigned char>(name[length]) & 0xC0) == 0x80) ++length;
    return name.substr(0, length);
}

}  // namespace

std::string Screens::messageTimeText(int64_t time) const {
    if (time < Bin::kEarliestRealTime) return "--";  // stored before the clock was set
    LocalTime local;
    if (!timezones::toLocal(storage_.settings.timezone(), time, local)) {
        local = civil::split(time);  // timezone not confirmed yet: show UTC
    }
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char text[24];
    std::snprintf(text, sizeof(text), "%d %s %02d:%02d", local.day, months[(local.month + 11) % 12], local.hour,
                  local.minute);
    return text;
}

void Screens::draw() {
    switch (screen_) {
        case Screen::Starting: drawStarting(); break;
        case Screen::NoContacts: drawNoContacts(); break;
        case Screen::Home: drawHome(); break;
        case Screen::Recording: drawRecording(); break;
        case Screen::Review: drawReview(); break;
        case Screen::ConfirmRerecord:
        case Screen::ConfirmDelete: drawConfirm(); break;
        case Screen::History: drawHistory(); break;
    }
    hw_.display.present();
}

void Screens::drawStarting() {
    Display& d = hw_.display;
    d.clear();
    centredText(d, kWidth / 2, 90, "Family Voice Box", kWhite, 2);
    centredText(d, kWidth / 2, 130, "v" + firmwareVersion(), kGrey, 2);
}

void Screens::drawNoContacts() {
    Display& d = hw_.display;
    d.clear();
    d.fillRect(0, 0, kWidth, kHeight, kBackground);
    centredText(d, kWidth / 2, 80, "No contacts yet", kWhite, 2);
    centredText(d, kWidth / 2, 130, "Invite family through", kGrey, 1);
    centredText(d, kWidth / 2, 150, "the box's Telegram bot", kGrey, 1);
}

void Screens::drawPhoto(const Contact& contact, int16_t cx, int16_t cy, int16_t radius, bool dimmed) {
    Display& d = hw_.display;
    const size_t colours = sizeof(kContactColours) / sizeof(kContactColours[0]);
    const Colour colour = kContactColours[(contact.number + colours - 1) % colours];
    d.fillCircle(cx, cy, radius, dimmed ? dim(colour) : colour);
    const uint8_t size = static_cast<uint8_t>(radius >= 50 ? 6 : 3);
    const std::string letter = firstLetter(contact.name);
    d.drawText(static_cast<int16_t>(cx - d.textWidth(letter, size) / 2), static_cast<int16_t>(cy - d.textHeight(size) / 2),
               letter, dimmed ? kGrey : kWhite, size);
}

void Screens::drawHome() {
    Display& d = hw_.display;
    const ContactView* view = current();
    if (view == nullptr) return;
    const uint64_t now = hw_.clock.millisSinceBoot();

    d.clear();
    d.fillRect(0, 0, kWidth, kHeight, kBackground);

    // History icon: a little list, top left.
    for (int i = 0; i < 3; ++i) {
        d.fillRect(14, static_cast<int16_t>(14 + i * 10), 6, 6, kGrey);
        d.fillRect(24, static_cast<int16_t>(14 + i * 10), 22, 6, kGrey);
    }

    drawPhoto(view->contact, kPhotoX, kPhotoY, kPhotoRadius, view->asleep);

    // Asleep: the photo is dimmed and there's a moon by it.
    if (view->asleep) {
        d.fillCircle(kPhotoX - 52, kPhotoY - 48, 16, kYellow);
        d.fillCircle(kPhotoX - 44, kPhotoY - 54, 14, kBackground);
    }

    // Unread badge, top right of the photo.
    if (view->unread > 0) {
        const int16_t bx = kPhotoX + 50;
        const int16_t by = kPhotoY - 50;
        d.fillCircle(bx, by, 18, kRed);
        const std::string count = view->unread > 9 ? "9+" : std::to_string(view->unread);
        d.drawText(static_cast<int16_t>(bx - d.textWidth(count, 2) / 2), static_cast<int16_t>(by - d.textHeight(2) / 2),
                   count, kWhite, 2);
    }

    // Play button, bottom right of the photo. Pulses when there's something unread.
    if (view->hasMessages) {
        const int16_t px = kPhotoX + 50;
        const int16_t py = kPhotoY + 50;
        const bool playingHere = playing_ == Playing::Message && playingContact_ == view->contact.number;
        int16_t radius = 20;
        if (view->unread > 0 && !playingHere) radius = static_cast<int16_t>(20 + (now / 300) % 2 * 3);
        d.fillCircle(px, py, radius, playingHere ? kBlue : kWhite);
        if (playingHere) {
            stopSquare(d, px, py, 7, kWhite);
        } else {
            playTriangle(d, static_cast<int16_t>(px + 2), py, 9, true, kBlue);
        }
    }

    centredText(d, kWidth / 2, 166, view->contact.name, kWhite, 2);

    // What the buttons do.
    if (contacts_.size() > 1) {
        playTriangle(d, kButtonX[0], kButtonHintY, 12, false, kGrey);
        playTriangle(d, kButtonX[2], kButtonHintY, 12, true, kGrey);
    }
    recordIcon(d, kButtonX[1], kButtonHintY, 16);
}

void Screens::drawRecording() {
    Display& d = hw_.display;
    const uint64_t elapsed = hw_.clock.millisSinceBoot() - recordingStarted_;
    const uint64_t limit = static_cast<uint64_t>(storage_.settings.maxRecordingSeconds()) * 1000;

    d.clear();
    d.fillRect(0, 0, kWidth, kHeight, kDarkRed);

    // A circle that grows with the child's voice and gently breathes even when quiet.
    const float breathe = 4.0f * std::sin(static_cast<float>(elapsed) / 300.0f);
    const int16_t radius = static_cast<int16_t>(40 + hw_.audio.inputLevel() * 0.25f + breathe);
    d.fillCircle(kWidth / 2, 86, static_cast<int16_t>(radius + 8), kWhite);
    d.fillCircle(kWidth / 2, 86, radius, kRed);

    if (const ContactView* view = current()) drawPhoto(view->contact, 40, 40, 26, false);

    centredText(d, kWidth / 2, 168, minutesSeconds(static_cast<uint32_t>(elapsed)), kWhite, 2);

    // Time left, as a bar that empties.
    const int16_t barWidth = 240;
    const int16_t left = limit > elapsed ? static_cast<int16_t>(barWidth * (limit - elapsed) / limit) : 0;
    d.fillRect(40, 198, barWidth, 6, dim(kWhite));
    d.fillRect(40, 198, left, 6, kWhite);

    stopSquare(d, kButtonX[1], kButtonHintY + 8, 10, kWhite);
}

void Screens::drawReview() {
    Display& d = hw_.display;
    d.clear();
    d.fillRect(0, 0, kWidth, kHeight, kBackground);

    if (const ContactView* view = current()) drawPhoto(view->contact, 40, 40, 26, false);
    centredText(d, kWidth / 2, 28, minutesSeconds(recordedMs_), kWhite, 2);
    rerecordIcon(d, 280, 44, 24, kBackground);

    // The recording, drawn as bars. Not a real waveform: just something to look at.
    const uint64_t now = hw_.clock.millisSinceBoot();
    for (int i = 0; i < 24; ++i) {
        int height = 10 + static_cast<int>((i * 37 + recordedMs_ / 97) % 40);
        if (playing_ == Playing::Recording) height = 10 + static_cast<int>((i * 37 + now / 80) % 40);
        d.fillRect(static_cast<int16_t>(40 + i * 10), static_cast<int16_t>(118 - height / 2), 6,
                   static_cast<int16_t>(height), kGrey);
    }

    // Listen, send, delete: one above each button.
    const int16_t y = 200;
    d.fillCircle(kButtonX[0], y, 26, kBlue);
    if (playing_ == Playing::Recording) {
        stopSquare(d, kButtonX[0], y, 9, kWhite);
    } else {
        playTriangle(d, static_cast<int16_t>(kButtonX[0] + 3), y, 12, true, kWhite);
    }
    d.fillCircle(kButtonX[1], y, 30, kGreen);
    thickLine(d, kButtonX[1] - 14, y, kButtonX[1] + 10, y, 7, kWhite);  // an arrow: send it off
    d.fillTriangle(kButtonX[1] + 4, y - 13, kButtonX[1] + 4, y + 13, kButtonX[1] + 18, y, kWhite);
    binIcon(d, kButtonX[2], y, 26);
}

void Screens::drawConfirm() {
    Display& d = hw_.display;
    d.clear();
    d.fillRect(0, 0, kWidth, kHeight, kBackground);

    // What's being asked about, with a question mark.
    if (screen_ == Screen::ConfirmDelete) {
        binIcon(d, kWidth / 2 - 30, 70, 40);
    } else {
        rerecordIcon(d, kWidth / 2 - 30, 70, 40, kBackground);
    }
    d.drawText(kWidth / 2 + 30, 46, "?", kWhite, 4);

    // No on the left, yes on the right.
    d.fillCircle(kButtonX[0], 190, 32, kRed);
    cross(d, kButtonX[0], 190, 14, kWhite);
    d.fillCircle(kButtonX[2], 190, 32, kGreen);
    tickMark(d, kButtonX[2], 190, 16, kWhite);
}

void Screens::drawHistory() {
    Display& d = hw_.display;
    d.clear();
    d.fillRect(0, 0, kWidth, kHeight, kBackground);

    // Header: back arrow and who this is with.
    playTriangle(d, 24, kHistoryHeader / 2, 10, false, kWhite);
    d.fillRect(30, kHistoryHeader / 2 - 4, 20, 8, kWhite);
    if (const ContactView* view = current()) {
        d.drawText(kBackRight, static_cast<int16_t>((kHistoryHeader - d.textHeight(2)) / 2), view->contact.name, kWhite, 2);
    }
    d.fillRect(0, kHistoryHeader - 1, kWidth, 1, kPanel);

    if (history_.empty()) {
        centredText(d, kWidth / 2, 120, "No messages yet", kGrey, 2);
        return;
    }

    for (size_t row = 0; row < kHistoryRowsShown && historyTop_ + row < history_.size(); ++row) {
        const size_t index = historyTop_ + row;
        const StoredMessage& m = history_[index];
        const int16_t top = static_cast<int16_t>(kHistoryHeader + row * kHistoryRow);
        const int16_t middle = static_cast<int16_t>(top + kHistoryRow / 2);
        if (index == historySelected_) d.fillRect(0, top, kWidth, kHistoryRow, kPanel);

        // Received messages point in from the left, sent ones point out to the right.
        const bool received = m.direction == StoredMessage::Direction::Received;
        d.fillCircle(24, middle, 14, received ? kBlue : kGreen);
        if (playing_ == Playing::Message && playingMessage_ == m.id) {
            stopSquare(d, 24, middle, 5, kWhite);
        } else {
            playTriangle(d, received ? 22 : 26, middle, 6, !received, kWhite);
        }
        if (m.status == StoredMessage::Status::Unread) d.fillCircle(38, top + 8, 5, kRed);
        if (m.status == StoredMessage::Status::Waiting) d.fillCircle(38, top + 8, 5, kYellow);

        const int16_t textY = static_cast<int16_t>(middle - d.textHeight(2) / 2);
        d.drawText(48, textY, messageTimeText(m.time), kWhite, 2);
        const std::string length = minutesSeconds(m.durationMs);
        d.drawText(static_cast<int16_t>(kWidth - 8 - d.textWidth(length, 2)), textY, length, kGrey, 2);
    }

    // Scroll position, when there's more than fits.
    if (history_.size() > kHistoryRowsShown) {
        const int16_t track = kHeight - kHistoryHeader;
        const int16_t thumb = static_cast<int16_t>(track * kHistoryRowsShown / history_.size());
        const int16_t at = static_cast<int16_t>(kHistoryHeader + track * historyTop_ / history_.size());
        d.fillRect(kWidth - 3, at, 3, thumb, kGrey);
    }
}

}  // namespace fvb

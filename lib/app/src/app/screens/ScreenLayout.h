#pragma once

#include <cstdint>

// Where things are on the rough screens, shared by the drawing code and the code
// that works out what a tap hit. Only used inside app/screens.

namespace fvb {
namespace layout {

constexpr int16_t kWidth = 320;
constexpr int16_t kHeight = 240;

// The three buttons sit under the screen. Icons showing what each one does are
// drawn just above it, centred on these.
constexpr int16_t kButtonX[3] = {53, 160, 267};
constexpr int16_t kButtonHintY = 212;

// Home
constexpr int16_t kPhotoX = 160;
constexpr int16_t kPhotoY = 92;
constexpr int16_t kPhotoRadius = 66;
constexpr int16_t kHistoryIconRight = 56;   // tap area: top left corner
constexpr int16_t kHistoryIconBottom = 52;

// Review: the bottom strip is split into three, one per button.
constexpr int16_t kReviewActionsTop = 170;
constexpr int16_t kRerecordLeft = 230;  // tap area: top right corner
constexpr int16_t kRerecordBottom = 90;

// Confirm: bottom half, left is no, right is yes.
constexpr int16_t kConfirmAnswersTop = 140;

// History
constexpr int16_t kHistoryHeader = 40;
constexpr int16_t kHistoryRow = 40;
constexpr int16_t kBackRight = 70;  // tap area: the header's left end

inline bool insideCircle(int16_t x, int16_t y, int16_t cx, int16_t cy, int16_t r) {
    const int32_t dx = x - cx;
    const int32_t dy = y - cy;
    return dx * dx + dy * dy <= static_cast<int32_t>(r) * r;
}

}  // namespace layout
}  // namespace fvb

#pragma once

#include <cstdint>

namespace fvb {
namespace pixelfont {

// The desktop window's bitmap font (see scripts/make-font.py).
constexpr int kWidth = 7;
constexpr int kHeight = 12;
constexpr char kFirst = ' ';
constexpr char kLast = '~';

extern const uint8_t kGlyphs[kLast - kFirst + 1][kHeight];

}  // namespace pixelfont
}  // namespace fvb

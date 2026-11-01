#pragma once

#include <cstdint>

namespace fvb {

enum class Button : uint8_t { Left, Middle, Right };
enum class SwipeDirection : uint8_t { Left, Right, Up, Down };

struct InputEvent {
    enum class Type : uint8_t { ButtonPress, Tap, Swipe };

    Type type = Type::Tap;
    Button button = Button::Left;      // for ButtonPress
    int16_t x = 0;                     // for Tap
    int16_t y = 0;                     // for Tap
    SwipeDirection swipe = SwipeDirection::Left;  // for Swipe
};

// The touch screen and the three touch buttons under it.
class Input {
public:
    virtual ~Input() = default;

    // Reads the hardware. Call once per loop before pollEvent().
    virtual void update() = 0;
    // Takes the next waiting event. Returns false if there are none.
    virtual bool pollEvent(InputEvent& event) = 0;
};

}  // namespace fvb

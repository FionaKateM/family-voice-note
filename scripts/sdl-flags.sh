#!/bin/sh
# Prints the compiler and linker flags for SDL2, which the desktop build uses for
# its window, keyboard, mouse, mic and speakers. Called from platformio.ini.
if command -v sdl2-config >/dev/null 2>&1; then
    echo $(sdl2-config --cflags --libs)
elif pkg-config --exists sdl2 2>/dev/null; then
    echo $(pkg-config --cflags --libs sdl2)
else
    echo "SDL2 isn't installed. On a Mac: brew install sdl2. On Ubuntu: sudo apt install libsdl2-dev" >&2
    exit 1
fi

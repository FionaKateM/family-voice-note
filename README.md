# Family Voice Box

A voice-only messaging box for young children whose family live in different countries. Built on an M5Stack Core2, using a Telegram bot run by the device itself.

- What it does: [docs/requirements.md](docs/requirements.md)
- How it's being built: [docs/build-plan.md](docs/build-plan.md)
- Not in v1: [docs/later.md](docs/later.md)

## Building and testing

You need [PlatformIO](https://platformio.org/install/cli) (`pip install platformio`). The desktop build also needs:

- libcurl, for HTTPS. It's already on a Mac.
- SDL2, for the window, keyboard, mouse, mic and speakers.
- ffmpeg, for playing voice notes and saving recordings. Without it the box still runs, but silently.

On a Mac:

```bash
brew install sdl2 ffmpeg
```

On Ubuntu:

```bash
sudo apt install libcurl4-openssl-dev libsdl2-dev ffmpeg
```

Run the tests:

```bash
pio test -e desktop
```

Build and run the laptop version of the box (stop it with Ctrl+C):

```bash
pio run -e desktop
```

```bash
.pio/build/desktop/program
```

It keeps its pretend SD card and internal memory in `desktop-data/`, which git ignores.

The box's screen opens in a window, with the 10 LEDs and the three buttons drawn underneath it:

| To do this on the box | Do this on the laptop |
| --- | --- |
| Press the left, middle or right button | Press `1`, `2` or `3` (or the left, down and right arrow keys), or click the button in the window |
| Tap the screen | Click it |
| Swipe | Drag across the screen with the mouse button held down |

Closing the window stops the box, the same as Ctrl+C. Add `--scale 3` to make the window bigger. The first time you record, macOS asks whether Terminal can use the microphone; say yes, or recordings will be silent. The screens are rough drafts for trying the flow out. What the controls do is listed at the top of `lib/app/src/app/screens/Screens.h`.

To give it your setup file, add `--setup`. This copies the file onto the pretend SD card, where the box reads it and deletes the copy. Your original stays put:

```bash
.pio/build/desktop/program --setup secrets/setup.txt
```

Once a bot token has been saved this way, the box answers on Telegram while it runs. To try it from your phone with a test bot, see [docs/telegram.md](docs/telegram.md#trying-it-on-the-laptop).

To pretend the box has moved to a different Wi-Fi network, add `--wifi` with any new name. The box then checks which country it's in, as the device will after moving house (see [docs/time.md](docs/time.md#moving-country)):

```bash
.pio/build/desktop/program --wifi Holiday
```

To try `/update` without publishing a real release, see [docs/updates.md](docs/updates.md#trying-it-on-the-laptop).

Build the real device firmware:

```bash
pio run -e core2
```

GitHub Actions runs all of these, plus a check for secrets, on every push.

Releases are published by hand from the Actions tab. See [docs/updates.md](docs/updates.md#publishing-a-new-version).

### Stopping secrets being committed

Real setup values go in `secrets/setup.txt` (copy `secrets/setup.example.txt`). Git ignores it. To also have git refuse commits containing a bot token or a setup file, run this once in your copy of the repo:

```bash
git config core.hooksPath .githooks
```

### Where things are

| Folder | What's in it |
| --- | --- |
| `lib/hal` | The hardware interfaces: audio, display, SD card, internal memory, network, clock, LEDs, power, buttons and touch, logging |
| `lib/app` | The box's logic. Plain C++ with no Arduino code, so it runs and is tested on a laptop |
| `lib/telegram` | Talking to Telegram's Bot API, on top of the network interface ([why it's our own](docs/telegram.md)) |
| `lib/platform_desktop` | Laptop versions of the hardware |
| `lib/platform_core2` | Core2 versions of the hardware |
| `lib/fakes` | Pretend hardware and pretend Telegram replies used by the tests |
| `src` | One small start-up file for each build |
| `test` | Unit tests |

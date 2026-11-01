# Family Voice Box

A voice-only messaging box for young children whose family live in different countries. Built on an M5Stack Core2, using a Telegram bot run by the device itself.

- What it does: [docs/requirements.md](docs/requirements.md)
- How it's being built: [docs/build-plan.md](docs/build-plan.md)
- Not in v1: [docs/later.md](docs/later.md)

## Building and testing

You need [PlatformIO](https://platformio.org/install/cli) (`pip install platformio`). The desktop build also needs libcurl for HTTPS: it's already on a Mac; on Linux install `libcurl4-openssl-dev`.

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

To try the Telegram connection from your phone with a test bot, see [docs/telegram.md](docs/telegram.md#trying-it-on-the-laptop).

Build the real device firmware:

```bash
pio run -e core2
```

GitHub Actions runs all of these, plus a check for secrets, on every push.

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

# Family Voice Box

A voice-only messaging device for young children (aged 0 to 5) whose family live in different countries. The child taps a family member's photo, records a message, and it arrives on that person's phone as a normal Telegram voice note. Family reply with a voice note and it plays on the device. Nobody outside an approved list can reach the child.

Hardware: M5Stack Core2 (ESP32). Messaging: one Telegram bot per device, run entirely by the device. There is no server.

This is a personal gift project, built by Fiona with Claude Code. Three devices will be made from the same firmware, each with its own setup file.

## Read these first

- `docs/requirements.md` – what v1 must do. This is the spec.
- `docs/build-plan.md` – the tickets, in order. Work on one ticket at a time.
- `docs/later.md` – features deliberately left out of v1. Don't build these unless asked.

The planning copy lives in Fiona's Craft notes. If `docs/` and something Fiona says in the session disagree, ask her which is right and update `docs/` to match.

## Rules

### Secrets never go in this repo

This repo is **public**. Never commit:

- Telegram bot tokens
- Telegram user IDs of real people
- Wi-Fi names or passwords
- Real setup files (`setup.txt`, `*.local.*`, anything under `secrets/`)

Local secrets for the desktop build live in `secrets/setup.txt` on Fiona's computer, which is ignored by git. Use `secrets/setup.example.txt` (fake values only) as the template. If you ever see a real token in a diff, stop and tell Fiona before committing.

Also keep personal details out: no children's names, family members' names or home locations in code, tests, docs or commit messages. Use placeholders like "Child", "Granny", "Parent".

### How to work a ticket

1. Read the ticket in `docs/build-plan.md` and the relevant sections of `docs/requirements.md`.
2. If something is ambiguous or seems wrong, ask Fiona before building on it.
3. Work on a branch named `ticket-<number>-<short-name>` (e.g. `ticket-1.2-storage`).
4. Write tests alongside the code. Logic that doesn't touch hardware must be testable on the desktop build.
5. Finish with all tests passing, then open a pull request.
6. In the pull request, include a short plain-English summary for Fiona: what was built, how to try it, and anything that changed from the requirements or that she needs to decide.
7. If the ticket changes the requirements, update `docs/requirements.md` in the same pull request.

### Talking to Fiona

Fiona is a product manager with an AI background and some Swift experience, but she is not an embedded C++ developer. Explain things in plain language. Avoid metaphors. Use British spelling. Push back if you think something she's asked for is a bad idea.

## Technical approach

- PlatformIO, Arduino framework, C++.
- Two builds from one codebase:
  - `core2`: the real device.
  - `desktop`: runs on a laptop, used for development and tests before the device arrives (23 November 2026).
- All hardware is reached through small interfaces (audio, display, storage, network, clock, LEDs, power, input). Device code implements them with M5Unified and friends. Desktop code implements them with the laptop's file system, network, screen window, mic and speakers.
- The desktop build should behave like a real box: it talks to real Telegram, uses a folder as its SD card, and shows the screen in a 320×240 window.
- Unit tests run on the desktop build and in GitHub Actions on every push.

See section 14 of `docs/requirements.md` for libraries and performance rules.

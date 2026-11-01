# Build plan

Tickets are handed over one at a time, in order. Each one finishes with tests passing, a pull request, and a short plain-English note of what was built and anything that changed from the requirements.

| When | Stage |
| --- | --- |
| Now to 23 Nov 2026 | Stage 1: everything that doesn't need the device, on the desktop build |
| 23 Nov to 14 Dec | Stage 2: get it working on the Core2, try interface ideas, real-world testing |
| Mid Dec to end Jan | Stage 3: three devices, set up by someone non-technical |

## Stage 1: Before the device arrives

Goal: a working box on a laptop that talks to real Telegram. Everything except the actual hardware.

**1.1 Project setup.** Repo structure, two builds (`core2` and `desktop`) from one codebase, small interfaces for each piece of hardware, a test runner, and GitHub Actions running the tests on every push. Add `.gitignore` rules and `secrets/setup.example.txt` so real secrets can't be committed. The `core2` build only needs to compile at this stage.

**1.2 Storage.** Settings, people lists (admins, contacts, monitors), per-contact folders and index files, the outbox, the bin, and `setup.txt` handling (read, store, delete). All with tests.

**1.3 Telegram connection.** Long polling, sending text, buttons and button presses, uploading voice notes with duration, downloading files, reactions, profile photos and invite links. Decide whether to use UniversalTelegramBot or write a small client, and note why.

**1.4 Conversation engine.** Works out who each message is from (admin, contact, monitor, unknown), routes commands, and runs multi-step conversations that survive a restart. Unknown people are ignored.

**1.5 Invite and setup flows.** Contact, monitor and admin invites, the contact setup conversation, admin approval, and `/me` for contacts.

**1.6 Admin settings.** The `/settings` button menu and direct commands, the timezone picker (country then city), the moved-country prompt (see section 8 of the requirements), `/status` and `/trash getlast`.

**1.7 Message handling.** Storing incoming voice notes and marking them unread, the voice-only auto-reply, the outbox with retries, read reactions, and monitor copies.

**1.8 Time.** Internet clock, timezones and daylight saving, quiet hours that cross midnight, the contact asleep check, and the timezone guess from the internet connection at setup. (The moved-country prompt moved to ticket 1.6.)

**1.9 Housekeeping.** Bin clear-out after 30 days, removing old messages, the storage alert, the offline-for-24-hours alert, and the low battery alert logic.

**1.10 Updates.** A GitHub Action that builds the firmware and publishes a release with a version number and checksum. The `/update` conversation and daily check. On the desktop build, installing is simulated.

**1.11 Desktop screen.** A 320×240 window with keyboard keys standing in for the three buttons, and rough versions of the home, recording, review and history screens. Uses the laptop mic and speakers.

**Stage 1 is done when** Fiona can, from her phone: invite a test contact, approve them, have them send a voice note that plays on the laptop box, record a reply that arrives as a normal voice bubble, see the 👀 reaction, change settings with buttons, and receive monitor copies.

## Stage 2: The device arrives

Goal: everything from Stage 1 running on the Core2, the audio proven, and an interface Fiona is happy with.

**2.1 Bring-up.** M5Unified, screen, touch, buttons, LEDs, SD card and Wi-Fi all working. `/status` reports which Core2 hardware revision it is.

**2.2 Audio test (do this before anything else in Stage 2).** Play a real Telegram voice note, then record an MP3 and send it, with the screen running at the same time. Report on sound quality, memory and processor load. This is a go/no-go point.

**2.3 Move the Stage 1 code onto the device.** Task and core setup, and locking for the shared screen and SD card bus.

**2.4 Wi-Fi setup.** Remembering several networks, the setup hotspot with its password on screen, and the setup problem screens.

**2.5 Real screens.** Built from what Fiona decides in testing: home, recording, review, history, unread, asleep, quiet hours, low battery and offline.

**2.6 Audio polish.** Volume levels, levelling of quiet and loud voices, the hearing safety cap and chimes.

**2.7 Updates for real.** Installing and rolling back on the device. Test it by publishing a deliberately broken version and checking the device recovers by itself.

**2.8 Power.** Battery level, charging detection and low battery alerts.

## Stage 3: Three devices

Goal: all three devices installed and working, set up by someone who isn't technical.

**3.1 Browser installer.** A web page (GitHub Pages) where someone plugs the device into a computer and clicks one button in Chrome or Edge to install the firmware. No developer tools needed.

**3.2 Setup guide.** Plain language, step by step, with pictures of what they should see.

**3.3 Self-test screen.** The device checks its speaker, mic, SD card, Wi-Fi and Telegram connection and shows a tick or a cross for each.

# Telegram connection

How the box talks to Telegram, and why it's built this way. Written for ticket 1.3.

## Decision: a small client of our own, not UniversalTelegramBot

The box uses its own Telegram client (`lib/telegram`) rather than the UniversalTelegramBot library.

Why:

1. **It has to run on the laptop.** UniversalTelegramBot is written for Arduino and does its own networking through the ESP32's Wi-Fi classes. That means it can't run in the desktop build, and it can't be tested with fake Telegram replies. Our client sends everything through the box's `Network` interface, so the same code runs on the laptop, on the device, and in tests.
2. **It doesn't cover everything we need.** UniversalTelegramBot handles text, buttons and polling well, but reactions (the 👀), profile photos and sending a voice note with its duration all need raw Bot API calls written around it. Once those are written, the library isn't saving much.
3. **It's small.** The client is a few hundred lines on top of ArduinoJson, which we were using anyway. Every call is covered by tests.

The cost: the device needs its own `Network` implementation (HTTPS over Wi-Fi with the connection kept open). That would have been needed anyway for software updates, and it's part of ticket 2.1/2.4.

## What the client can do

| Need from the requirements | Bot API method | Client call |
| --- | --- | --- |
| Check the bot token on startup | `getMe` | `getMe` |
| Long polling, 25 second wait | `getUpdates` | `getUpdates` |
| Send text | `sendMessage` | `sendMessage` |
| Tap buttons | `sendMessage` with `reply_markup` | `sendMessage(..., &keyboard)` |
| Update a button menu in place | `editMessageText` | `editMessageText` |
| Button presses | `callback_query` updates, `answerCallbackQuery` | `getUpdates`, `answerCallbackQuery` |
| Send a recording as a voice bubble | `sendVoice` (upload, with `duration`) | `sendVoiceFile` |
| Monitor copies without uploading again | `sendVoice` with a `file_id` | `sendVoiceById` |
| 👀 when the child has listened | `setMessageReaction` | `setReaction` |
| Download voice notes and photos to the SD card | `getFile` + file download | `downloadFile` |
| Contact's profile photo, size closest to 320px | `getUserProfilePhotos` | `getProfilePhoto` |
| Invite links | `t.me/<bot>?start=<code>` | `inviteLink`, `parseCommand` |

It also sorts every incoming message into a kind (voice note, text, photo, photo sent as a file, sticker, video, round video, music file, GIF, other), which tickets 1.5 and 1.7 need for the "can only receive voice notes" reply and the photo step of contact setup.

### Things worth knowing

- **Invite links don't need Telegram's invite link feature.** That's for groups. Our links are `t.me/<bot>?start=<code>` deep links, built on the box. Telegram allows up to 64 letters, digits, `_` and `-` in the code. Making codes, using them once and expiring them after 48 hours is ticket 1.5.
- **👀 is on Telegram's list of allowed reactions**, so the choice in section 4 of the requirements is confirmed.
- **Messages are sent as plain text**, with no formatting. A contact called `*Granny*` shows up exactly like that rather than in bold.
- **Errors are sorted** into kinds: offline, token rejected, person blocked the bot, Telegram busy (with how long to wait), and so on. The outbox (ticket 1.7) uses this to decide whether to try again later or give up.
- **The bot token never appears in error messages or logs.** Tests check this.
- **Downloads go to `<name>.part` first** and are only renamed once the whole file has arrived and is the size Telegram said. A half-finished download is never mistaken for a real message.
- **Telegram's download limit for bots is 20 MB.** A 6 minute voice note is well under 1 MB.

### Left for later tickets

- **Uploads read the whole recording into memory.** At the 360 second maximum an MP3 is around 1.5 MB, which fits in the Core2's 8 MB PSRAM. If storage later gains a way to read a file in pieces, uploads can stream instead.
- **Retrying** is up to the caller. The client never retries by itself.

## Checks to do on real Telegram

These can only be confirmed with a real bot and phone:

1. **MP3 voice notes show as a normal voice bubble.** Telegram's documentation says `sendVoice` accepts MP3, but doesn't promise a voice bubble for every MP3. Send `/mp3` in the try-out below: it uploads a 3 second test tone in the same format the box will record (mono, 16 kHz, 32 kbps constant bitrate) with its duration. On your phone, check that:
    - it arrives as a voice bubble with a waveform and a play button, not as a music file or a document
    - it shows as 0:03
    - it plays a steady beep

    If it comes through as a music file instead, the box's recording format will need to change, so tell Claude what you saw. A real recording from the device is still worth checking once ticket 2.2 can make one.
2. **The 👀 reaction appears** on your message on your phone.
3. **Profile photos**: some people's privacy settings hide their photo from bots. The client reports "no photo" in that case, and setup should fall back to asking them to send one.

## Remembering where polling got to

Done in ticket 1.4 by the conversation engine (`lib/app/src/app/bot/Bot.h`), not the client. After each message is handled, the engine saves the next update ID in internal memory (`tg_offset`), along with the bot's own ID (`tg_bot_id`) so a new bot token starts afresh. It saves after every message rather than every batch, so a restart halfway through a batch doesn't repeat the first half.

Before handling a message it also notes which one it's on and how many times it has tried (`tg_busy`). If the box restarts twice while handling the same message, the third time it's skipped and logged. Without this, a message that crashed the box would crash it again on every restart until Telegram dropped it a day later.

## The test MP3

`assets/test-audio/test-tone.mp3` is 3 seconds of a 440 Hz tone, made by `scripts/make-test-tone.sh` with ffmpeg:

- mono, 16 kHz, 32 kbps constant bitrate, matching section 14 of the requirements
- encoded with LAME, not shine. The device is planned to use shine, but ffmpeg's shine support only allows 32, 44.1 and 48 kHz. Both write standard MP3 frames, so Telegram sees the same kind of file.
- no tags or extra header frames, like the device's own recordings will be
- 12 KB, which is 32 kbps × 3.1 seconds

To make it again, run `scripts/make-test-tone.sh`. If you change the length, also change `kTestToneSeconds` in `lib/telegram/src/telegram/TryOut.cpp`.

## Trying it on the laptop

The box itself now answers on Telegram (ticket 1.4): run the desktop build without `--try-telegram` once a token is saved. It knows only `/start`, `/help` and `/cancel` so far; the real commands arrive in tickets 1.5 to 1.7.

The try-out mode below is still useful for the voice note, MP3, reaction and photo checks above, which the box can't do yet.

1. Make a test bot with @BotFather in Telegram and copy its token.
2. Copy `secrets/setup.example.txt` to `secrets/setup.txt` and fill in `BOT_TOKEN`, `CHILD_NAME` and `ADMIN_ID`. (Git ignores this file. The Wi-Fi lines aren't used on the laptop.)
3. Build and run. `--setup` saves the token into the laptop box's pretend internal memory, the same way the device reads its setup file (ticket 1.2). After the first time you can leave it off.

    ```bash
    pio run -e desktop
    ```

    ```bash
    .pio/build/desktop/program --setup secrets/setup.txt --try-telegram
    ```

4. It prints a `https://t.me/...?start=...` link. Open it on your phone and press Start. Only the phone that opens the link gets answers; anyone else is ignored. The link changes every run.
5. Then try:
    - `/buttons`: two buttons appear. Pressing one should change the message to say which you pressed.
    - `/photo`: saves your profile photo to `desktop-data/sdcard/tryout/profile.jpg`.
    - `/mp3`: sends you a test tone as an MP3 voice note (see "Checks to do on real Telegram", point 1). Run the program from the repo folder, because it copies `assets/test-audio/test-tone.mp3` onto the pretend SD card when it starts.
    - Send a voice note: it's saved to `desktop-data/sdcard/tryout/`, gets a 👀 reaction, and comes back to you twice (once uploaded again with its length, once re-sent by Telegram's file ID).
    - Anything else (text, sticker, photo): the bot tells you what kind of message it thinks it was.
6. Stop it with Ctrl+C.

If the box is reading messages and another copy of the bot is also running (or the bot has a webhook set up somewhere), Telegram refuses with a "conflict" error. Stop the other copy.

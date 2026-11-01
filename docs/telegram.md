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

- **Remembering where polling got to (ticket 1.4).** Telegram resends anything the box hasn't confirmed. The client exposes `nextUpdateId()`. The conversation engine should save it after handling each batch and restore it at startup, otherwise the last batch is handled twice after a restart.
- **Uploads read the whole recording into memory.** At the 360 second maximum an MP3 is around 1.5 MB, which fits in the Core2's 8 MB PSRAM. If storage later gains a way to read a file in pieces, uploads can stream instead.
- **Retrying** is up to the caller. The client never retries by itself.

## Checks to do on real Telegram

These can only be confirmed with a real bot and phone:

1. **MP3 voice notes show as a normal voice bubble.** Telegram's documentation says `sendVoice` accepts MP3, but the box's own MP3 recordings can't be tried until the device (or the laptop mic in ticket 1.11) can record. The try-out below sends back Telegram's own OGG files, which proves the upload path but not MP3.
2. **The 👀 reaction appears** on your message on your phone.
3. **Profile photos**: some people's privacy settings hide their photo from bots. The client reports "no photo" in that case, and setup should fall back to asking them to send one.

## Trying it on the laptop

The desktop build has a try-out mode for testing against real Telegram before the conversation engine exists.

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
    - Send a voice note: it's saved to `desktop-data/sdcard/tryout/`, gets a 👀 reaction, and comes back to you twice (once uploaded again with its length, once re-sent by Telegram's file ID).
    - Anything else (text, sticker, photo): the bot tells you what kind of message it thinks it was.
6. Stop it with Ctrl+C.

If the box is reading messages and another copy of the bot is also running (or the bot has a webhook set up somewhere), Telegram refuses with a "conflict" error. Stop the other copy.

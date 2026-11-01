# Requirements (v1)

> The screen layout, buttons and LED patterns aren't decided yet. Anything here about how things look is a starting point to try on the device once it arrives. The workflows and rules are the part that matters.

## 1. Who uses it

- **Child:** aged 0 to 5. Can't read, so nothing on the device can depend on text. For the youngest, a parent does the pressing.
- **Admin:** a parent who manages the device by messaging its Telegram bot. Each device has one or more admins. The maintainer (Fiona) is an admin on every device.
- **Contact:** an approved family member. Sends and receives voice notes using the normal Telegram app.
- **Monitor:** a parent or guardian who gets copies of voice notes (the BCC feature).
- One person can have several roles. A parent could be an admin, a contact and a monitor.

## 2. How it fits together

- Each device has its own Telegram bot. One bot, one device, one child.
- There's no server. The device runs the bot: it keeps asking Telegram for new messages, and every reply, command response and auto-reply comes from the device.
- If the device is offline, nothing happens until it reconnects. Telegram only holds undelivered updates for about 24 hours. Anything older is lost (see the offline alert in section 7).
- Every device runs identical firmware. The only things that differ are in the setup file: Wi-Fi, bot token, child's name and first admin. Contacts, monitors, other admins and settings are all added through the bot and stored on the device.

## 3. Hardware

- M5Stack Core2 (ESP32 with 8 MB PSRAM) with the base that has 10 RGB LEDs. Built-in mic, speaker, 2-inch touch screen, three touch buttons under the screen and a battery.
- There are two hardware revisions of the Core2 with different power chips. Use the M5Unified library so either works.
- microSD card, 16 or 32 GB, formatted FAT32.
- Mostly plugged in. On battery it should manage about an hour of active use.

## 4. The child's experience

### Contacts

- One contact on screen at a time, with their photo. Move between them by swiping or with the buttons.
- **Unread messages:** a badge on that contact's photo, and the LEDs pulse. When on a contact with unread messages, make it obvious how to play them.
- **Asleep:** if it's within a contact's sleep hours in their own timezone, their photo shows they're asleep. The child can still send to them. It's up to the contact to silence their phone.

### Recording

- Tap to start, tap to stop. Recording also stops at the maximum length (default 360 seconds, admins can change it).
- Clear feedback that it's listening: something moving on screen, and the LEDs react to the child's voice level. This must look different from the unread pulse.
- Recordings under 2 seconds are thrown away without going to review (accidental taps). Tune this on the device.

### Review (after recording)

- Listen back, send, re-record, delete.
- Re-record and delete need a confirmation that works without reading. Both move the recording to the bin rather than deleting it.
- Send saves the recording to the outbox and goes straight back to the home screen. Sending happens in the background.
- Once sent, the child can't delete a message.

### Listening

- Play the latest message from the current contact.
- History: a scrollable list of past messages with that contact, sent and received, tap to play.
- When the child plays a received message to the end, the sender gets a reaction on their message in Telegram (👀). Telegram only allows reactions from a fixed list of emoji; 👀 is on it (confirmed in ticket 1.3).

### Sounds and lights

- A chime for every new message, even if the LEDs are already pulsing (except during quiet hours).
- **Low battery** at 20%, 10% and 5%: a sound plus a distinct LED pattern. Admins also get a Telegram message.
- **Volume:** the child can change voice note volume on the device. The alert volume (chimes, low battery) can only be changed by an admin.
- **Hearing safety:** hard cap on maximum loudness, whatever the settings. Target no more than 80 to 85 dBA at 0.5 m. Check with a sound meter app.
- **Levelling:** quiet and loud senders are evened out so they sound roughly the same. The same applies to the mic, so the child's voice comes through clearly whether they're close or far away.

## 5. Quiet hours (the child's own Do Not Disturb)

- Admins set the hours, e.g. 19:00 to 07:00.
- During quiet hours the screen and LEDs are off and there are no sounds. Messages still download in the background.
- Bot commands are still answered straight away. The device keeps checking Telegram at normal speed during quiet hours.
- When quiet hours end, the unread LEDs come on if anything arrived overnight, and any missed low battery sound plays.

## 6. Setup and status screens

- **Setup file:** on first boot, the device reads `setup.txt` from the SD card (Wi-Fi name and password, bot token, child's name, first admin's Telegram ID). It saves these to its internal memory and then **deletes** the file, so the bot token isn't left on the card. To change them later, put a new `setup.txt` on the card.
  - The first time, the bot token, child's name and admin ID are required. Wi-Fi is optional, because the setup Wi-Fi network can add it instead.
  - A later `setup.txt` only needs the lines being changed. A Wi-Fi network is added to the known list, an admin is added to the admin list, and a new bot token or child's name replaces the old one.
  - `ADMIN_ID` can list several people separated by commas (for example the parent and the maintainer).
  - If the file has a problem (missing or badly formed values), nothing is saved and the file stays on the card to be fixed. The setup problem screen will show this once screens exist.
  - The file is overwritten with blanks before it's deleted, so the token can't be recovered from the card.
- **Wi-Fi:** the device remembers several networks (up to 5, forgetting the oldest), so it can be set up at one house and then work at another. On boot it tries known networks for 30 seconds. If none work, it starts its own setup Wi-Fi network (`VoiceBox-Setup` plus a short code) with a password shown on the screen. A parent connects with their phone, picks their home Wi-Fi and enters the password.
- **Bot check:** once online, the device checks the bot token with Telegram (`getMe`). If it's missing or rejected, the device shows a setup problem screen, the LEDs show amber, and it goes no further.
- **Offline after setup:** stored messages can still be played and new ones recorded. Sent messages wait in the outbox. A small indicator shows it's offline.
- All these screens need to make sense to a parent without needing to read much, and should never scare the child.

## 7. Messages and storage

- **Incoming:** voice notes from approved contacts are downloaded to the SD card. Messages from anyone else are ignored with no reply, unless they've arrived through a valid invite link.
- **Not a voice note:** if a contact sends text, a photo, a sticker, a video, a round video message or a music file, the bot replies "[Child] can only receive voice notes."
- **Outgoing:** recordings are MP3 and sent with Telegram's `sendVoice`, including the duration, so they show as normal voice note bubbles.
- **Outbox:** survives power cuts and reboots, sends in order, and retries with longer gaps each time if the network is down.
- **Folders:** everything is stored by contact, with a small index file per contact so history loads without scanning the card. A recording appears in the child's history as soon as they press Send, marked as waiting until Telegram has it.
- **Bin:** deleted and re-recorded recordings go to a hidden bin, named with the date, time and contact. Anything older than 30 days is permanently removed. Recordings binned before the clock was ever set are kept, because their real age is unknown.
- **Limits:** up to 30 contacts, 10 admins and 10 monitors per device.
- **Keeping messages:** messages older than a set number of days (default 60, 0 means keep forever) are removed from the device. They stay in Telegram on family members' phones.
- **Storage alert:** admins get a message when the card is 80% full.
- **Offline alert:** when the device reconnects after more than 24 hours offline, admins get a message such as: "[Child]'s box was offline from Friday 18:00 to Sunday 10:00. Voice notes sent before Saturday 10:00 may not have arrived. Ask family to resend if needed."

## 8. Time

- The clock is set from the internet. Daylight saving is handled automatically.
- **Device timezone:** during setup, the device guesses its location from its internet connection and asks the admin to confirm with buttons ("Looks like you're in [country]. Is that right? [Yes] [Change]"). Change shows a list of countries, then cities if that country has more than one timezone. Nobody ever types a timezone name.
- **Moving country:** whenever the box joins a different Wi-Fi network from last time, it checks its location again. If the country doesn't match its timezone, it asks the admins with buttons ("Looks like the box is now in [country]. Switch to [country] time? [Yes] [No]"). If they say no, it doesn't ask again for that country. Admins can also change the timezone in `/settings` at any time.
- **Contacts:** each contact has their own timezone and sleep hours, used for the asleep indicator.
- **Overnight hours:** quiet hours and sleep hours can cross midnight (19:00 to 07:00). They start on the minute given and end just before the end time. The same start and end time means there are none, so admins can turn quiet hours off by setting them to 00:00 to 00:00.
- **When the time isn't known:** until the clock has been set from the internet and the box's timezone confirmed, the box is never in quiet hours and nobody shows as asleep. Being awake by mistake is better than going dark by mistake.
- **Where the box is:** the setup guess comes from a free location service that looks up the box's internet address (ipinfo.io, with ipapi.co as a backup). Only the country and timezone are kept; the address and town in the reply are thrown away.
- **Timezone list:** the box carries its own list of countries and timezones, made from the IANA time zone database (see `docs/time.md`). Big countries list one city per set of clock rules (the US has eight, not 29). When a country changes its clocks, the list is updated with new firmware.

## 9. Who can do what

| Role | Can do |
| --- | --- |
| Admin | Everything in /settings, approve new people, run updates, get alerts |
| Contact | Send and receive voice notes, change their own name, timezone, sleep hours and photo |
| Monitor | Receive copies of voice notes, nothing else |
| Anyone else | Nothing. Ignored completely unless they use a valid invite link |

- Every admin action is checked against the admin list before it runs.
- When an admin is added or removed, all admins are told.
- The last admin can't be removed.

## 10. Invite and setup flows

### Adding a contact

1. An admin sends `/addcontact`. The bot replies with an invite link (a `t.me/<bot>?start=<code>` deep link) that works once and expires after 48 hours.
2. The admin sends the link to the person however they like.
3. The person taps the link. Telegram opens the bot and presses Start for them.
4. The bot takes them through setup, using tap buttons wherever possible:
   - What [child] calls you (e.g. Granny)
   - Your country and timezone
   - Your sleep hours (default 9pm to 9am, change if you like)
   - Your photo: use your Telegram profile picture or send a different one. Photos sent as files are turned down with a prompt to send them as a normal photo. Download the size closest to 320px wide.
   - A notice: "[Child]'s parents may be able to hear all voice notes to and from you." [I understand]
5. All admins get "Granny has finished setting up. Approve? [Yes] [No]".
6. Once approved, Granny appears on the device and gets "You're all set. Send [Child] a voice note any time."

Contacts can send `/me` at any time to change their name, timezone, sleep hours or photo.

### Adding a monitor or an admin

- `/addmonitor` and `/addadmin` work the same way: an invite link, a short setup, then approval by an existing admin.
- Monitors are told what they'll receive. Admins are told what they can do.

### Removing people

- Through `/settings`, with a confirmation. Removing a contact removes them and their messages from the device. Their Telegram chat history isn't affected.

## 11. Monitoring (BCC)

- One setting for the whole device: Off, Sent only, Received only, or All.
- Every monitor gets a copy of each matching voice note, labelled with who it's from and to, e.g. "[Child] to Granny, 14:32".
- Copies reuse Telegram's stored copy of the audio (`file_id`), so nothing is uploaded twice.
- Contacts are told about this during their setup.

## 12. Admin settings

Admins send `/settings` to get a menu of buttons. The direct commands also work for anyone who prefers typing.

| Setting | What it does | Default |
| --- | --- | --- |
| Contacts | List, add (invite), remove | |
| Monitors | List, add (invite), remove, set mode | Off |
| Admins | List, add (invite), remove | First admin from setup file |
| Quiet hours | Child's do not disturb hours | 19:00 to 07:00 |
| Timezone | Confirm or change the device's timezone | Guessed at setup |
| Voice volume | Resets the child's playback volume | 70% |
| Alert volume | Chimes and alerts. Child can't change it | 50% |
| Max recording | Longest single voice note (10 to 900 seconds) | 360 seconds |
| Keep messages | Days before messages are removed from the device (0 to 3650, 0 means forever) | 60 |
| Child's name | Used in bot messages | From setup file |
| Status (`/status`) | Battery and charging, Wi-Fi strength, storage used (MB and %), firmware version, messages waiting to send | |
| Last deleted (`/trash getlast`) | Sends the admin the most recent recording from the bin | |
| Update (`/update`) | See section 13 | |

## 13. Software updates

- New firmware is published as a GitHub Release on this repo. The device only ever checks that one location, which is built into the firmware. It never installs from a link someone sends it.
- Once a day the device checks for a new version. If there is one, admins get a message: "Version 1.3 is available. Send /update to install."
- `/update`: the bot replies "Version 1.3 is available: [short note on what's new]. Install? [Yes] [No]", or "Already up to date".
- On Yes, it downloads over HTTPS, checks the file is intact (checksum), installs, restarts and reports "Updated to 1.3".
- **Rollback:** if the new version doesn't start properly and reconnect to Telegram within a few minutes, the device goes back to the previous version and tells the admins.
- Each family's admins update their own devices. The maintainer, as an admin on all devices, can also update any of them.

## 14. Technical approach

- PlatformIO, Arduino framework, C++.
- **Two builds from the same code:** the device build, and a desktop build that runs on a laptop. All hardware is reached through small interfaces (audio, screen, SD card, internal memory, network, clock, LEDs, power, buttons and touch, logging), so all the logic can be run and tested on a laptop.
- **The desktop build is a working box:** it talks to real Telegram, uses a folder as its SD card, shows the screen in a 320×240 window, and uses the laptop's mic and speakers.
- **Libraries to confirm during the build:** M5Unified, LVGL, ArduinoJson, shine-esp32 for MP3 recording, and an Opus decoder for incoming voice notes (ESP32-audioI2S or libopus).
- **Telegram client:** a small client of our own (`lib/telegram`) built on the network interface, not UniversalTelegramBot, so it runs and is tested on the desktop build. Decided in ticket 1.3; reasons in `docs/telegram.md`.
- **Performance:** audio runs on one processor core, and screen and network on the other. The screen and SD card share an SPI bus, so access must be locked. Audio is buffered in PSRAM and written to the card in chunks. Screen and LED updates are limited to 15 to 20 per second while recording.
- **Telegram:** long polling (`getUpdates`, 25 second timeout) with the connection kept open.
- **Storage:** settings, secrets and people lists in the ESP32's internal memory (NVS). Messages, photos and indexes on the SD card. The layout is described at the top of `lib/app/src/app/storage/Paths.h`.

## 15. Risks to prove early

1. **Playing Telegram voice notes (Opus) smoothly on the Core2.** This is the biggest technical risk. Test it first when the device arrives. If it can't be done well, the plan needs rethinking.
2. Recording MP3 while the screen updates and files are written to the card, without glitches.
3. Battery life on battery.
4. Measured loudness against the hearing safety cap.

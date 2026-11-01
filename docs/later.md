# Later releases

Features deliberately left out of v1. Don't build these unless Fiona asks.

## Paused features

- **Auto-stop on silence.** Sample background noise for the first 200 ms of a recording, then stop after 4 to 5 seconds of silence. Clips under 3 seconds are thrown away. v1 uses tap to stop and the time limit instead.
- **Full bin commands.** List deleted recordings, get a specific one, empty the bin now. v1 only has `/trash getlast`.
- **Sending a deleted recording on the child's behalf.** For when a child deletes or records over something by accident and then wants it sent. It needs to show up in the child's own history with that contact, not only in Telegram. The flow needs designing. Until then, a parent can get the file with `/trash getlast` and forward it themselves.
- **Timeout and grounding.** Lock the device for a set time, until a set time or indefinitely, with a short message on screen. Includes unlocking early and checking when the lock ends.
- **Parents editing contact names.** In v1, contacts choose their own name.
- **Signed firmware.** An extra check that updates really came from the maintainer. v1 relies on the device only downloading from one fixed place over HTTPS.

## Ideas to think about

- When a contact is asleep, ask "are you sure?" before sending, or hold the message until they wake up.
- More things for contacts to set up when they join.
- Noticing when the box has moved country (by checking its internet location now and then) and asking the admins whether to switch timezone. Dropped from v1 in ticket 1.8: the location is guessed once at setup, and parents change it in `/settings`.

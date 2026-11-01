# Updates

How new firmware gets from this repo onto the boxes (section 13 of the requirements). The box's side is in `lib/app/src/app/updates/`; publishing is `.github/workflows/release.yml`.

## What's in a release

Each release is a GitHub Release on this repo, tagged `v` plus the version (for example `v0.2.0`), with two files attached:

- `firmware.bin`: the Core2 firmware.
- `firmware.json`: what the box reads to decide whether to update. For example:

  ```json
  {"version": "0.2.0", "notes": "Louder voices", "size": 1234567, "sha256": "…"}
  ```

`scripts/make-release.sh` makes both from a built firmware file.

## Publishing a new version

1. In a pull request, change `kFirmwareVersion` in `lib/app/src/app/Version.h` (for example from `"0.1.0"` to `"0.2.0"`), and merge it into main.
2. On GitHub, go to **Actions**, then **Release**, then **Run workflow**.
3. Write a sentence or two on what's new. Admins see this in Telegram, so write it for them, not for developers.
4. Tick **Publish** and run it.

The workflow runs the tests, builds the firmware, makes `firmware.json` and publishes the release. It refuses to publish from any branch other than main, or a version that's already been released.

Leave **Publish** unticked for a practice run. Everything is built and checked, and the two files are kept with the run under **Artifacts**, but nothing is published and no box sees it.

## How a box finds and installs it

- The box only ever looks at `https://github.com/FionaKateM/family-voice-note/releases/latest/download/firmware.json`. That address is built into the firmware, and no message can change it. GitHub's "latest" skips drafts and pre-releases.
- Once a day it fetches that file. If the version is newer than its own, every admin gets "Version 0.2.0 is available. Send /update to install." Each version is only announced once. If GitHub can't be reached, it tries again an hour later.
- `/update` checks straight away. It replies with the version, the note on what's new and Yes and No buttons, or "Already up to date". While there are no releases at all, GitHub answers "not found", which the box treats as up to date.
- On Yes, the box downloads `firmware.bin` from that version's own release, checks its size and SHA-256 checksum against `firmware.json`, installs it and restarts. A download that doesn't match is thrown away and nothing changes.
- Once it's back and has reached Telegram, the admin who said Yes gets "Updated to 0.2.0." and the other admins are told who updated it.
- If the new version doesn't start properly, the device goes back to the old one by itself (ticket 2.7), and every admin is told that the update didn't work.

Versions are compared as numbers, so `0.10.0` is newer than `0.9.0`. An older or equal version is never offered.

## Trying it on the laptop

On the desktop build, installing is pretend: the download really happens and is really checked, but the laptop program can't replace itself. Instead it saves the file in `desktop-data/firmware/`, stops, and the next time you start it, it acts as if it were the new version. That way you can see the whole conversation.

You don't need a real release. Make a pretend one in a folder (any file will do as the "firmware"):

```bash
scripts/make-release.sh .pio/build/desktop/program desktop-data/test-release --version 9.9.9 --notes "A test update"
```

Start the box pointed at that folder instead of GitHub:

```bash
.pio/build/desktop/program --release-folder desktop-data/test-release
```

Send `/update` to the bot from an admin's phone, press Yes, and the program stops. Start it again the same way and you'll get "Updated to 9.9.9."

To see what admins are told when an update doesn't start properly, start it with `--undo-update` straight after a pretend install. It throws away the pretend version, as the device does when it goes back to the old firmware.

To go back to normal, start the program once with `--undo-update` (or delete `desktop-data/firmware/`). Building a version newer than the pretend one also clears it.

## Not done yet

- Installing on the device itself, and going back automatically when a new version doesn't start properly, are ticket 2.7. Until then the Core2 build refuses to install.

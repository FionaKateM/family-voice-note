#!/usr/bin/env bash
# Makes assets/test-audio/test-tone.mp3, the MP3 the laptop try-out sends with /mp3.
#
# It's in the same format the box will record in (requirements section 14):
# mono, 16 kHz, 32 kbps constant bitrate.
#
# It's encoded with LAME rather than shine (the encoder the device is planned
# to use). ffmpeg's shine support only allows 32, 44.1 and 48 kHz, so it can't
# make a 16 kHz file. Both produce standard MP3 frames, so Telegram sees the
# same kind of file either way.
#
# The sound is 3 seconds of a 440 Hz tone at a gentle volume, fading in and
# out so it doesn't click. No tags or extra header frames are written, so the
# file is plain MP3 audio, like the device's recordings.
#
# Needs ffmpeg built with libmp3lame (most ffmpeg packages have it).
# If you change the length, change kTestToneSeconds in lib/telegram/src/telegram/TryOut.cpp.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "sine=frequency=440:sample_rate=16000:duration=3" \
    -af "volume=0.5,afade=t=in:d=0.1,afade=t=out:st=2.9:d=0.1" \
    -ac 1 -ar 16000 -c:a libmp3lame -b:a 32k -abr 0 \
    -map_metadata -1 -id3v2_version 0 -write_id3v1 0 -write_xing 0 \
    assets/test-audio/test-tone.mp3

echo "Made assets/test-audio/test-tone.mp3"

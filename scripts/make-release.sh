#!/usr/bin/env bash
# Gets the files for a release ready: the firmware, and firmware.json describing it.
#
#   scripts/make-release.sh <firmware file> <output folder> [--version <v>] [--notes "<text>"]
#
# The output folder ends up with:
#   firmware.bin    a copy of the firmware
#   firmware.json   {"version":"1.3.0","notes":"...","size":1234567,"sha256":"..."}
#
# The box reads firmware.json from the latest GitHub Release to see if there's a
# new version, then downloads firmware.bin and checks its size and checksum
# against it before installing (lib/app/src/app/updates/Updates.h).
#
# The version comes from lib/app/src/app/Version.h unless --version is given.
# --version is for trying updates on the desktop build:
#
#   scripts/make-release.sh .pio/build/desktop/program desktop-data/test-release --version 9.9.9 --notes "A test"
#   .pio/build/desktop/program --release-folder desktop-data/test-release
#
# then send /update to the bot. (On the desktop build, installing is pretend,
# so any file will do as the "firmware".)
#
# The release workflow (.github/workflows/release.yml) runs this on the real
# Core2 firmware.

set -euo pipefail

usage() {
    echo "Usage: $0 <firmware file> <output folder> [--version <v>] [--notes \"<text>\"]" >&2
    exit 2
}

[[ $# -ge 2 ]] || usage
firmware=$1
out=$2
shift 2
version=""
notes=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) [[ $# -ge 2 ]] || usage; version=$2; shift 2 ;;
        --notes) [[ $# -ge 2 ]] || usage; notes=$2; shift 2 ;;
        *) usage ;;
    esac
done

here=$(cd "$(dirname "$0")" && pwd)
if [[ -z "$version" ]]; then
    version=$("$here/firmware-version.sh")
fi
version=${version#v}
if [[ ! "$version" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]]; then
    echo "\"$version\" isn't a version number like 1.3.0" >&2
    exit 1
fi
if [[ ! -s "$firmware" ]]; then
    echo "No firmware at $firmware" >&2
    exit 1
fi

mkdir -p "$out"
cp "$firmware" "$out/firmware.bin"

# JSON is written by Python so quotes and line breaks in the notes are escaped properly.
python3 - "$out" "$version" "$notes" <<'PY'
import hashlib, json, os, sys

out, version, notes = sys.argv[1], sys.argv[2], sys.argv[3]
path = os.path.join(out, "firmware.bin")
with open(path, "rb") as f:
    data = f.read()
info = {
    "version": version,
    "notes": notes.strip(),
    "size": len(data),
    "sha256": hashlib.sha256(data).hexdigest(),
}
with open(os.path.join(out, "firmware.json"), "w") as f:
    json.dump(info, f, ensure_ascii=False)
    f.write("\n")
print(f"Version {version}: {len(data)} bytes, SHA-256 {info['sha256']}")
PY

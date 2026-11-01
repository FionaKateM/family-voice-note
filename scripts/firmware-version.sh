#!/usr/bin/env bash
# Prints the firmware version from lib/app/src/app/Version.h, e.g. "0.1.0".
# Used by the release workflow, so the version only lives in one place.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

version=$(sed -n 's/^constexpr const char\* kFirmwareVersion = "\([^"]*\)";.*/\1/p' lib/app/src/app/Version.h)
if [[ ! "$version" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]]; then
    echo "Couldn't read the version from lib/app/src/app/Version.h (found \"$version\")" >&2
    exit 1
fi
echo "$version"

#!/usr/bin/env bash
# Stops real secrets reaching this public repo.
#
#   scripts/check-secrets.sh            check every file git is tracking (used by CI)
#   scripts/check-secrets.sh --staged   check only what's about to be committed (used by the pre-commit hook)
#
# Looks for:
#   - setup files and anything in secrets/ (apart from the example)
#   - text shaped like a Telegram bot token: 8-10 digits, a colon, then 35 letters/digits/-/_

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

token_pattern='[0-9]{8,10}:[A-Za-z0-9_-]{35}'
problems=0

if [[ "${1:-}" == "--staged" ]]; then
    files=$(git diff --cached --name-only --diff-filter=ACMR)
else
    files=$(git ls-files)
fi

while IFS= read -r file; do
    [[ -z "$file" ]] && continue
    case "$file" in
        secrets/setup.example.txt) ;;
        secrets/* | setup.txt | */setup.txt | *.local.* | .env | .env.* | */.env | */.env.*)
            echo "Not allowed in this repo: $file"
            problems=1
            ;;
    esac
done <<< "$files"

if [[ "${1:-}" == "--staged" ]]; then
    added=$(git diff --cached -U0 | grep -E '^\+' || true)
    matches=$(grep -E "$token_pattern" <<< "$added" || true)
    if [[ -n "$matches" ]]; then
        # Only show the start of each line, so the token itself isn't printed in full.
        echo "Something that looks like a Telegram bot token is about to be committed:"
        cut -c1-20 <<< "$matches" | sed 's/$/.../'
        problems=1
    fi
else
    matches=$(git grep -I -lE "$token_pattern" || true)
    if [[ -n "$matches" ]]; then
        echo "Something that looks like a Telegram bot token is in these files:"
        echo "$matches"
        problems=1
    fi
fi

if [[ $problems -ne 0 ]]; then
    echo
    echo "Remove these before committing. Real values belong in secrets/setup.txt, which git ignores."
    exit 1
fi
echo "No secrets found."

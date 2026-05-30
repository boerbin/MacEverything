#!/bin/bash
# verify-bundle.sh — assert a .app is self-contained.
#
# Scans every Mach-O binary in Contents/MacOS and every dylib in
# Contents/Frameworks, and fails if any of them still links a library by an
# external absolute path (/opt/homebrew, /usr/local, /opt/local). Those paths
# only exist on developer machines and cause dyld load failures for end users
# (GitHub issue #2).
#
# Exit 0 = self-contained. Exit 1 = external leak found.
#
# Usage: verify-bundle.sh <path-to.app>

set -uo pipefail

APP="${1:?usage: verify-bundle.sh <path-to.app>}"
if [[ ! -d "$APP" ]]; then
    echo "error: app bundle not found: $APP" >&2
    exit 1
fi

MACOS_DIR="$APP/Contents/MacOS"
FRAMEWORKS="$APP/Contents/Frameworks"
EXTERNAL_RE='(/opt/homebrew/|/usr/local/|/opt/local/)'

leaks=0
checked=0

check_macho() {
    local f="$1"
    file "$f" 2>/dev/null | grep -q "Mach-O" || return 0
    checked=$((checked + 1))
    # Skip the install-id line (line 1 of otool -L is the file itself).
    local bad
    bad="$(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -E "$EXTERNAL_RE" || true)"
    if [[ -n "$bad" ]]; then
        echo "LEAK: $f links external libs:"
        echo "$bad" | sed 's/^/    /'
        leaks=$((leaks + 1))
    fi
}

for f in "$MACOS_DIR"/*; do
    [[ -f "$f" ]] && check_macho "$f"
done

if [[ -d "$FRAMEWORKS" ]]; then
    for f in "$FRAMEWORKS"/*; do
        [[ -f "$f" ]] && check_macho "$f"
    done
fi

echo "verify-bundle: checked $checked Mach-O file(s), $leaks leak(s)"
if [[ $leaks -gt 0 ]]; then
    echo "FAIL: app is NOT self-contained"
    exit 1
fi
echo "PASS: app is self-contained"
exit 0

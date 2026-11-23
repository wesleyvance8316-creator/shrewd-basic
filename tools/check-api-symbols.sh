#!/bin/sh
# API surface guard (docs/api-stability.md): the public shrewd_basic_*
# function/macro names in the public headers must match
# tools/api-symbols.txt exactly - a removed symbol is a breaking change
# and a new symbol must be logged.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
manifest="$here/api-symbols.txt"
cur=$(mktemp)
trap 'rm -f "$cur"' EXIT
fail=0

grep -hoE 'shrewd_basic_[A-Za-z0-9_]+\(' "$root"/include/shrewd-basic/*.h \
        "$root"/include/shrewd-basic/*/*.h 2>/dev/null | tr -d '(' | sort -u > "$cur"

echo "== api symbols"
missing=$(comm -23 "$manifest" "$cur")
added=$(comm -13 "$manifest" "$cur")
if [ -n "$missing" ]; then
    echo "REMOVED public symbols (breaking - restore or bump MAJOR):" >&2
    printf '%s\n' "$missing" >&2
    fail=1
fi
if [ -n "$added" ]; then
    echo "new public symbols (add to tools/api-symbols.txt):" >&2
    printf '%s\n' "$added" >&2
    fail=1
fi
if [ "$fail" -eq 0 ]; then
    echo "api symbols: OK ($(wc -l < "$manifest") entries)"
fi
exit "$fail"

#!/bin/sh
# Self-test for tools/reflect_gen.py: generate descriptors from the sample
# header, compile them against the library sources and run the smoke
# program. The library build never needs this; it verifies the generator.
#
# Usage: tools/reflect_gen_check.sh   (CC overrides the compiler)
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 "$here/reflect_gen.py" "$here/reflect_gen_sample.h" -o "$work/generated"

${CC:-cc} -std=c11 \
    -I"$root/include" -I"$root/include/shrewd-basic-internal" \
    -I"$here" -I"$work" \
    "$here/reflect_gen_smoke.c" "$work/generated.c" \
    "$root"/library/reflect/*.c \
    "$root"/library/ds/rbtree.c "$root"/library/ds/ds.c \
    "$root"/library/log/log.c \
    "$root"/library/platform/error.c "$root"/library/platform/memory.c \
    -o "$work/smoke"

"$work/smoke"
echo "reflect_gen_check: OK"

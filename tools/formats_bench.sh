#!/bin/sh
# Build and run the formats micro-benchmark (tools/formats_bench.c).
# Usage: tools/formats_bench.sh   (CC overrides the compiler)
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -std=c11 -O2 \
    -I"$root/include" -I"$root/include/shrewd-basic-internal" \
    "$here/formats_bench.c" \
    "$root"/library/reflect/*.c \
    "$root"/library/ds/*.c \
    "$root"/library/log/log.c \
    "$root"/library/platform/error.c "$root"/library/platform/memory.c \
    "$root"/library/ini/*.c "$root"/library/csv/*.c "$root"/library/toml/*.c \
    "$root"/library/msgpack/*.c "$root"/library/cbor/*.c \
    -o "$work/formats_bench"

"$work/formats_bench"

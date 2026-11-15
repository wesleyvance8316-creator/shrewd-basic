#!/bin/sh
# Build and run the json micro-benchmark (tools/json_bench.c).
# Usage: tools/json_bench.sh   (CC overrides the compiler)
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -std=c11 -O2 \
    -I"$root/include" -I"$root/include/shrewd-basic-internal" \
    "$here/json_bench.c" \
    "$root"/library/json/*.c \
    "$root"/library/reflect/*.c \
    "$root"/library/ds/rbtree.c "$root"/library/ds/ds.c \
    "$root"/library/log/log.c \
    "$root"/library/platform/error.c "$root"/library/platform/memory.c \
    -o "$work/json_bench"

"$work/json_bench"

#!/bin/sh
# Multi-platform verification for shrewd-basic.
#
#   1. clang: full build + every test suite.
#   2. 32-bit (ILP32): full build + tests with -m32. Skipped when the
#      multilib toolchain is not installed (apt install gcc-multilib
#      libc6-dev-i386).
#   3. MSVC: not runnable here (needs a Windows host). The code sticks
#      to C11; the reflect macros only use _Generic and offsetof, which
#      MSVC supports in its C11 mode (/std:c11).
#
# Usage: tools/verify_platforms.sh   (CC overrides the clang compiler)
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "== clang: build + tests"
CC="${CC:-clang}" cmake -S "$root" -B "$work/clang" > /dev/null
cmake --build "$work/clang" -j > /dev/null
(cd "$work/clang" && ctest --output-on-failure)

echo "== 32-bit: build + tests"
echo 'int main(void){return 0;}' > "$work/probe.c"
if "${CC:-cc}" -m32 "$work/probe.c" -o "$work/probe" 2> /dev/null; then
    cmake -S "$root" -B "$work/m32" -DCMAKE_C_FLAGS=-m32 > /dev/null
    cmake --build "$work/m32" -j > /dev/null
    (cd "$work/m32" && ctest --output-on-failure)
else
    echo "skipped: 32-bit toolchain missing (apt install gcc-multilib libc6-dev-i386)"
fi

echo "== MSVC"
echo "skipped: requires a Windows host (build with CMake in the MSVC generator)"

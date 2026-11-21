#!/bin/sh
# Multi-platform verification for shrewd-basic.
#
#   1. clang: full build + every test suite.
#   2. 32-bit (ILP32): full build + tests with -m32. Prefers the system
#      multilib toolchain (apt install gcc-multilib libc6-dev-i386);
#      without root it falls back to downloading the 32-bit runtime
#      packages and extracting them into a local prefix (no install).
#   3. MSVC: needs a Windows host, or msvc-wine on Linux
#      (https://github.com/mstorsjo/msvc-wine: wine64 + msitools +
#      cabextract, then vsdownload.py). The code sticks to C11; the
#      reflect macros only use _Generic and offsetof, which MSVC
#      supports in its C11 mode (/std:c11). clang-cl without a Windows
#      SDK cannot compile the standard headers, so it is not a
#      substitute.
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
m32_flags=""
if "${CC:-cc}" -m32 "$work/probe.c" -o "$work/probe" 2> /dev/null; then
    m32_flags="-m32"
else
    # No root? Extract the 32-bit toolchain pieces into a local prefix.
    # Debian/Ubuntu multilib shares /usr/include with the 64-bit build;
    # the packages contribute 32-bit crt/libgcc plus gnu/stubs-32.h.
    mkdir -p "$work/m32pkgs"
    gcc32_pkg=$(apt-cache search -n --names-only 'gcc-[0-9]+-multilib$' \
        | awk '{print $1}' | sort -V | tail -1)
    libgcc32_pkg=$(apt-cache search -n --names-only 'lib32gcc-[0-9]+-dev$' \
        | awk '{print $1}' | sort -V | tail -1)
    (
        cd "$work/m32pkgs"
        # shellcheck disable=SC2086
        apt-get download libc6-dev-i386 libc6-i386 lib32gcc-s1 \
            ${gcc32_pkg:-} ${libgcc32_pkg:-} 2> /dev/null || true
    )
    for deb in "$work"/m32pkgs/*.deb; do
        [ -e "$deb" ] || continue
        dpkg -x "$deb" "$work/m32pkgs/root"
    done
    R="$work/m32pkgs/root"
    gcc32=$(echo "$R"/usr/lib/gcc/x86_64-linux-gnu/*/32)
    if [ -e "$R/usr/lib32/Scrt1.o" ] && [ -d "$gcc32" ]; then
        m32_flags="-m32 -isystem $R/usr/include/x86_64-linux-gnu \
-isystem /usr/include/x86_64-linux-gnu -B$R/usr/lib32 -B$gcc32"
        m32_link="-static -L$R/usr/lib32"
    fi
fi
if [ -n "$m32_flags" ]; then
    cmake -S "$root" -B "$work/m32" \
        -DCMAKE_C_FLAGS="$m32_flags" \
        -DCMAKE_EXE_LINKER_FLAGS="${m32_link:-}" > /dev/null
    cmake --build "$work/m32" -j > /dev/null
    (cd "$work/m32" && ctest --output-on-failure)
    file "$work/m32"/tests/shrewd-basic-reflect-tests || true
else
    echo "skipped: 32-bit toolchain unavailable (install gcc-multilib" \
         "libc6-dev-i386, or allow apt-get download for the local prefix)"
fi

echo "== MSVC"
if command -v wine > /dev/null 2>&1 && [ -d "${MSVC_WINE:-}" ]; then
    echo "msvc-wine present at ${MSVC_WINE}: run its cl wrapper manually"
else
    echo "skipped: needs a Windows host, or msvc-wine on Linux"
    echo "  (apt install wine64 msitools cabextract, then mstorsjo/msvc-wine)"
fi

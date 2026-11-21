#!/bin/sh
# Self-test for tools/reflect_gen.py: generate descriptors from the
# sample headers, compile them against the library sources and run the
# smoke programs. Covers both backends - lex (header text scan) and
# dwarf (compile a probe, read the real layout, pin it with static
# asserts) - plus the --layout report. The library build never needs
# this; it verifies the generator.
#
# Usage: tools/reflect_gen_check.sh   (CC overrides the compiler)
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

lib_sources="$root/library/reflect/*.c $root/library/ds/rbtree.c $root/library/ds/ds.c $root/library/log/log.c $root/library/platform/error.c $root/library/platform/memory.c"

compile_smoke() {
    # $1 = round dir (holds generated.h/.c), $2 = smoke source
    ${CC:-cc} -std=c11 \
        -I"$root/include" -I"$root/include/shrewd-basic-internal" \
        -I"$here" -I"$1" \
        "$2" "$1/generated.c" \
        $lib_sources \
        -o "$1/smoke"
}

# --- lex backend: sample header -------------------------------------------
mkdir "$work/lex"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample.h" -o "$work/lex/generated"
compile_smoke "$work/lex" "$here/reflect_gen_smoke.c"
"$work/lex/smoke"

# --- dwarf backend: same header, same smoke (macro output parity) ---------
mkdir "$work/dwarf"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample.h" \
    -o "$work/dwarf/generated" --backend dwarf
grep -q "_Static_assert(sizeof(shape_t)" "$work/dwarf/generated.c"
grep -q "Backend: dwarf" "$work/dwarf/generated.c"
compile_smoke "$work/dwarf" "$here/reflect_gen_smoke.c"
"$work/dwarf/smoke"

# --- dwarf backend: C bit-fields ------------------------------------------
mkdir "$work/bits"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_bits.h" \
    -o "$work/bits/generated" --backend dwarf
grep -q "SHREWD_BASIC_REFLECT_FIELD_BITS_UNIT(flags_t, ok" "$work/bits/generated.c"
compile_smoke "$work/bits" "$here/reflect_gen_bits_smoke.c"
"$work/bits/smoke"

# --- --layout report (pahole-lite) ---------------------------------------
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_bits.h" \
    --backend dwarf --layout > "$work/layout.txt"
grep -q "flags_t: 8 bytes" "$work/layout.txt"
grep -q "packed .* bytes" "$work/layout.txt"

echo "reflect_gen_check: OK"

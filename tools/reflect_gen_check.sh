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

# The dwarf rounds need pyelftools; skip loudly (77 = ctest SKIP) rather
# than fail or pretend - never a silent drop.
if ! python3 -c "import elftools" 2> /dev/null; then
    echo "reflect_gen_check: SKIPPED - needs python3-pyelftools" >&2
    echo "  install: sudo apt install -y python3-pyelftools" >&2
    exit 77
fi

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

# --- annotations: owned/wide strings, buffers, count=, tagged unions -----
mkdir "$work/ann"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_ann.h" \
    -o "$work/ann/generated"
grep -q "SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(doc_t, title)" "$work/ann/generated.c"
grep -q "SHREWD_BASIC_REFLECT_FIELD_STRING16_BUFFER(doc_t, tagline)" "$work/ann/generated.c"
grep -q "SHREWD_BASIC_REFLECT_FIELD_WSTRING_OWNED(doc_t, author)" "$work/ann/generated.c"
grep -q "SHREWD_BASIC_REFLECT_FIELD_FLEX(doc_t, body, words" "$work/ann/generated.c"
grep -q "SHREWD_BASIC_REFLECT_UNION_DEFINE(payload_u_t_reflect" "$work/ann/generated.c"
grep -q "SHREWD_BASIC_REFLECT_FIELD_UNION(event_t, payload, payload_u_t_reflect, kind)" \
    "$work/ann/generated.c"
compile_smoke "$work/ann" "$here/reflect_gen_ann_smoke.c"
"$work/ann/smoke"

# --- json policy stubs: one row per directive -----------------------------
mkdir "$work/json"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_json.h" \
    -o "$work/json/generated"
grep -q 'SHREWD_BASIC_JSON_KEY(name, "full_name", "nameID")' "$work/json/generated_json.c"
grep -q 'SHREWD_BASIC_JSON_REQUIRED(name)' "$work/json/generated_json.c"
grep -q 'SHREWD_BASIC_JSON_DEFAULT_STR(name, "anonymous")' "$work/json/generated_json.c"
grep -q 'SHREWD_BASIC_JSON_KEY(level, "lv")' "$work/json/generated_json.c"
grep -q 'SHREWD_BASIC_JSON_RANGE(level, 0, 5)' "$work/json/generated_json.c"
grep -q 'SHREWD_BASIC_JSON_DEFAULT_JSON(box, "{\\"w\\":3,\\"h\\":4}")' \
    "$work/json/generated_json.c"
grep -q 'SHREWD_BASIC_JSON_UNION_STYLE(payload, SHREWD_BASIC_JSON_UNION_FLAT)' \
    "$work/json/generated_json.c"
${CC:-cc} -std=c11 \
    -I"$root/include" -I"$root/include/shrewd-basic-internal" \
    -I"$here" -I"$work/json" \
    "$here/reflect_gen_json_smoke.c" \
    "$work/json/generated.c" "$work/json/generated_json.c" \
    $lib_sources "$root"/library/json/*.c \
    -o "$work/json/smoke"
"$work/json/smoke"

# --- golden: full-text lock on the sample stubs ---------------------------
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample.h" -o "$work/golden" --quiet
mkdir "$work/golden_json"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_json.h" \
    -o "$work/golden_json/generated" --quiet
if [ "${1:-}" = "--update-golden" ]; then
    cp "$work/golden.c" "$here/reflect_gen_golden.c"
    cp "$work/golden.h" "$here/reflect_gen_golden.h"
    cp "$work/golden_json/generated_json.c" "$here/reflect_gen_golden_json.c"
    cp "$work/golden_json/generated_json.h" "$here/reflect_gen_golden_json.h"
    echo "reflect_gen_check: golden files updated"
    exit 0
fi
if [ -f "$here/reflect_gen_golden.c" ]; then
    diff -u "$here/reflect_gen_golden.c" "$work/golden.c"
    diff -u "$here/reflect_gen_golden.h" "$work/golden.h"
    diff -u "$here/reflect_gen_golden_json.c" "$work/golden_json/generated_json.c"
    diff -u "$here/reflect_gen_golden_json.h" "$work/golden_json/generated_json.h"
else
    echo "reflect_gen_check: no golden files (run: tools/reflect_gen_check.sh --update-golden)" >&2
    exit 1
fi

echo "reflect_gen_check: OK"

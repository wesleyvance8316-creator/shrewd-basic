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

# --- schema dump program (runtime exporter, single schema truth) ------
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_json.h" \
    -o "$work/json/generated" --schema-dump --quiet
printf '%s\n' '#include "shrewd-basic/formats/json.h"' \
    'const struct shrewd_basic_json_ops g_sample_ops = {0};' > "$work/json/ops_def.c"
${CC:-cc} -std=c11 \
    -I"$root/include" -I"$root/include/shrewd-basic-internal" \
    -I"$here" -I"$work/json" \
    "$work/json/generated_schema_dump.c" \
    "$work/json/generated.c" "$work/json/generated_json.c" \
    "$work/json/ops_def.c" \
    $lib_sources "$root"/library/json/*.c \
    -o "$work/json/schema_dump"
"$work/json/schema_dump" cfg_t "$work/schema.json"
grep -q '"properties"' "$work/schema.json"
grep -q '"x-aliases"' "$work/schema.json"
if "$work/json/schema_dump" no_such_t > /dev/null 2>&1; then
    echo "reflect_gen_check: schema dump accepted an unknown type" >&2
    exit 1
fi

# --- dbus: mount-table generation ------------------------------------
mkdir -p "$work/dbus/include/shrewd-basic/config"
printf '#define SHREWD_BASIC_FORMAT_JSON 1\n' > "$work/dbus/include/shrewd-basic/config/config.h"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_dbus.h" \
    -o "$work/dbus/generated"
grep -q 'sample_dbus_mounts\[\]' "$work/dbus/generated_dbus.h"
grep -q 'int sample_dbus_mount_all(shrewd_basic_dbus_t \*bus' "$work/dbus/generated_dbus.h"
grep -q '{"/config/user", {.type = &db_user_t_reflect, .store = SHREWD_BASIC_DBUS_STORE_OWNED}}' \
    "$work/dbus/generated_dbus.c"
grep -q '{"/hw/adc0", {.type = &db_state_t_reflect, .ops = &sample_dbus_ops}}' \
    "$work/dbus/generated_dbus.c"
grep -q '{"/config/ro", {.type = &db_ro_t_reflect, .store = SHREWD_BASIC_DBUS_STORE_OWNED, .flags = SHREWD_BASIC_DBUS_MOUNT_READONLY}}' \
    "$work/dbus/generated_dbus.c"
grep -q '{"/event/alarm", {.flags = SHREWD_BASIC_DBUS_MOUNT_EVENT}}' "$work/dbus/generated_dbus.c"
cat > "$work/dbus/smoke.c" <<'EOF'
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/dbus/dbus.h"

#include "generated.h"
#include "generated_dbus.h"
#include "reflect_gen_sample_dbus.h"

const shrewd_basic_dbus_ops_t sample_dbus_ops = {0};

int main(void) {
    shrewd_basic_dbus_t *bus = NULL;
    shrewd_basic_dbus_options_t options;
    shrewd_basic_dbus_handle_t *handles[4];
    int32_t id = 7;
    int32_t back = 0;
    memset(&options, 0, sizeof(options));
    memset(handles, 0, sizeof(handles));
    options.sync_notify = 1;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_dbus_create(&options, &bus)) {
        printf("reflect_gen_dbus_smoke: FAILED create\n");
        return 1;
    }
    if (SHREWD_BASIC_ERR_OK != sample_dbus_mount_all(bus, handles)) {
        printf("reflect_gen_dbus_smoke: FAILED mount_all\n");
        return 1;
    }
    if ((SHREWD_BASIC_ERR_OK != shrewd_basic_dbus_set(bus, "/config/user.id", &id, sizeof(id), NULL))
        || (SHREWD_BASIC_ERR_OK != shrewd_basic_dbus_get(bus, "/config/user.id", &back, sizeof(back), NULL))
        || (back != id)) {
        printf("reflect_gen_dbus_smoke: FAILED round trip\n");
        return 1;
    }
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_set(bus, "/config/ro.magic", &id, sizeof(id), NULL)) {
        printf("reflect_gen_dbus_smoke: FAILED readonly\n");
        return 1;
    }
    for (size_t i = 0; i < sample_dbus_mounts_count; ++i) {
        (void)shrewd_basic_dbus_unmount(bus, handles[i]);
    }
    shrewd_basic_dbus_destroy(bus);
    printf("reflect_gen_dbus_smoke: ok\n");
    return 0;
}
EOF
${CC:-cc} -std=c11 \
    -I"$root/include" -I"$root/include/shrewd-basic-internal" \
    -I"$work/dbus/include" -I"$here" -I"$work/dbus" \
    "$work/dbus/smoke.c" \
    "$work/dbus/generated.c" "$work/dbus/generated_dbus.c" \
    $lib_sources "$root"/library/ds/radix.c \
    "$root"/library/dbus/dbus.c "$root"/library/json/*.c \
    -o "$work/dbus/smoke"
"$work/dbus/smoke"

# table name falls back to the first type when name= is absent
printf '/* reflect: type=t_t;\n   dbus: type=t_t, path=/x; */\ntypedef struct { int a; } t_t;\n' \
    > "$work/dbus/named.h"
python3 "$here/reflect_gen.py" "$work/dbus/named.h" -o "$work/dbus/named_gen" --quiet
grep -q 't_t_dbus_mounts\[\]' "$work/dbus/named_gen_dbus.h"

# refusals: linked mounts are hand-written; event mounts name no type
printf '/* reflect: type=t_t;\n   dbus: type=t_t, path=/x, store=linked; */\ntypedef struct { int a; } t_t;\n' \
    > "$work/dbus/linked.h"
if python3 "$here/reflect_gen.py" "$work/dbus/linked.h" -o "$work/dbus/linked_gen" --quiet 2> /dev/null; then
    echo "reflect_gen_check: store=linked was accepted" >&2
    exit 1
fi
printf '/* reflect: type=t_t;\n   dbus: type=t_t, path=/x, flags=event; */\ntypedef struct { int a; } t_t;\n' \
    > "$work/dbus/evtype.h"
if python3 "$here/reflect_gen.py" "$work/dbus/evtype.h" -o "$work/dbus/evtype_gen" --quiet 2> /dev/null; then
    echo "reflect_gen_check: event mount with type= was accepted" >&2
    exit 1
fi


# --- xml: policy-table generation + csv/ini equivalence ----------------
mkdir -p "$work/xml"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_xml.h" \
    -o "$work/xml/generated"
grep -q 'const shrewd_basic_xml_policy_t cfg_t_xml_policy\[\] = {' "$work/xml/generated_xml.c"
grep -q '{"name", "id", 1, SHREWD_BASIC_XML_REQUIRED},' "$work/xml/generated_xml.c"
grep -q '{"secret", NULL, 0, SHREWD_BASIC_XML_HIDDEN},' "$work/xml/generated_xml.c"
grep -q '{"size", NULL, 1, 0},' "$work/xml/generated_xml.c"
grep -q '{"child", NULL, 0, 0},' "$work/xml/generated_xml.c"
grep -q 'const size_t cfg_t_xml_policy_count = 4;' "$work/xml/generated_xml.c"
grep -q 'const shrewd_basic_xml_policy_t row_map_policy\[\] = {' "$work/xml/generated_xml.c"
grep -q '{"b", "beta", 0, SHREWD_BASIC_XML_REQUIRED | SHREWD_BASIC_XML_HIDDEN},' \
    "$work/xml/generated_xml.c"
grep -q 'extern const shrewd_basic_xml_policy_t cfg_t_xml_policy\[\];' "$work/xml/generated_xml.h"
grep -q 'extern const size_t row_map_policy_count;' "$work/xml/generated_xml.h"
# csv:/ini: equivalence entrances land on the json policy tables
grep -q 'SHREWD_BASIC_JSON_KEY(a, "A")' "$work/xml/generated_json.c"
grep -q 'SHREWD_BASIC_JSON_REQUIRED(a)' "$work/xml/generated_json.c"

# compile smoke: the table is pure data (no module link needed)
cat > "$work/xml/smoke.c" <<'EOF'
#include <stdio.h>

#include "generated_xml.h"
#include "reflect_gen_sample_xml.h"

int main(void) {
    if ((4 != cfg_t_xml_policy_count) || (1 != row_map_policy_count)) {
        printf("reflect_gen_xml_smoke: FAILED counts\n");
        return 1;
    }
    if ((1 != cfg_t_xml_policy[0].as_attr) || (0 != cfg_t_xml_policy[3].as_attr)) {
        printf("reflect_gen_xml_smoke: FAILED rows\n");
        return 1;
    }
    printf("reflect_gen_xml_smoke: ok\n");
    return 0;
}
EOF
${CC:-cc} -std=c11 \
    -I"$root/include" -I"$here" -I"$work/xml" \
    "$work/xml/smoke.c" "$work/xml/generated_xml.c" \
    -o "$work/xml/smoke"
"$work/xml/smoke"

# refusals: member without its type anchor; flags on the type statement;
# duplicate directives; an anchor with no member rows
printf '/* reflect: type=t_t;\n   xml: member=t_t.a, attr; */\ntypedef struct { int a; } t_t;\n' \
    > "$work/xml/nanchor.h"
if python3 "$here/reflect_gen.py" "$work/xml/nanchor.h" -o "$work/xml/nanchor_gen" --quiet 2> /dev/null; then
    echo "reflect_gen_check: xml member without type= was accepted" >&2
    exit 1
fi
printf '/* reflect: type=t_t;\n   xml: type=t_t, attr; */\ntypedef struct { int a; } t_t;\n' \
    > "$work/xml/flagtype.h"
if python3 "$here/reflect_gen.py" "$work/xml/flagtype.h" -o "$work/xml/flagtype_gen" --quiet 2> /dev/null; then
    echo "reflect_gen_check: xml attr on a type statement was accepted" >&2
    exit 1
fi
printf '/* reflect: type=t_t;\n   xml: type=t_t; */\ntypedef struct {\n    /* xml: member=t_t.a, attr, attr; */\n    int a;\n} t_t;\n' \
    > "$work/xml/dup.h"
if python3 "$here/reflect_gen.py" "$work/xml/dup.h" -o "$work/xml/dup_gen" --quiet 2> /dev/null; then
    echo "reflect_gen_check: duplicate xml directive was accepted" >&2
    exit 1
fi
printf '/* reflect: type=t_t;\n   xml: type=t_t; */\ntypedef struct { int a; } t_t;\n' \
    > "$work/xml/empty.h"
if python3 "$here/reflect_gen.py" "$work/xml/empty.h" -o "$work/xml/empty_gen" --quiet 2> /dev/null; then
    echo "reflect_gen_check: empty xml policy table was accepted" >&2
    exit 1
fi

# --- golden: full-text lock on the sample stubs ---------------------------
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample.h" -o "$work/golden" --quiet
mkdir "$work/golden_json"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_json.h" \
    -o "$work/golden_json/generated" --quiet
mkdir "$work/golden_dbus"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_dbus.h" \
    -o "$work/golden_dbus/generated" --quiet
mkdir "$work/golden_xml"
python3 "$here/reflect_gen.py" "$here/reflect_gen_sample_xml.h" \
    -o "$work/golden_xml/generated" --quiet
if [ "${1:-}" = "--update-golden" ]; then
    cp "$work/golden.c" "$here/reflect_gen_golden.c"
    cp "$work/golden.h" "$here/reflect_gen_golden.h"
    cp "$work/golden_json/generated_json.c" "$here/reflect_gen_golden_json.c"
    cp "$work/golden_json/generated_json.h" "$here/reflect_gen_golden_json.h"
    cp "$work/golden_dbus/generated_dbus.c" "$here/reflect_gen_golden_dbus.c"
    cp "$work/golden_dbus/generated_dbus.h" "$here/reflect_gen_golden_dbus.h"
    cp "$work/golden_xml/generated_xml.c" "$here/reflect_gen_golden_xml.c"
    cp "$work/golden_xml/generated_xml.h" "$here/reflect_gen_golden_xml.h"
    echo "reflect_gen_check: golden files updated"
    exit 0
fi
if [ -f "$here/reflect_gen_golden.c" ]; then
    diff -u "$here/reflect_gen_golden.c" "$work/golden.c"
    diff -u "$here/reflect_gen_golden.h" "$work/golden.h"
    diff -u "$here/reflect_gen_golden_json.c" "$work/golden_json/generated_json.c"
    diff -u "$here/reflect_gen_golden_json.h" "$work/golden_json/generated_json.h"
    diff -u "$here/reflect_gen_golden_dbus.c" "$work/golden_dbus/generated_dbus.c"
    diff -u "$here/reflect_gen_golden_dbus.h" "$work/golden_dbus/generated_dbus.h"
    diff -u "$here/reflect_gen_golden_xml.c" "$work/golden_xml/generated_xml.c"
    diff -u "$here/reflect_gen_golden_xml.h" "$work/golden_xml/generated_xml.h"
else
    echo "reflect_gen_check: no golden files (run: tools/reflect_gen_check.sh --update-golden)" >&2
    exit 1
fi

echo "reflect_gen_check: OK"

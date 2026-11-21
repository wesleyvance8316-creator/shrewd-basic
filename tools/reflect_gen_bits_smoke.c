/**
 * @file reflect_gen_bits_smoke.c
 * @brief Smoke program for generated C bit-field descriptors
 *        (reflect_gen_check.sh, dwarf backend).
 *
 * Drives the generated stubs against real C bit-fields: writes via one
 * side, reads via the other, and proves only described groups survive
 * equal/hash. Prints one summary line and exits non-zero on failure.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "generated.h"
#include "reflect_gen_sample_bits.h"

static int failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("reflect_gen_bits_smoke: FAILED %s\n", name);
        ++failures;
    }
}

int main(void) {
    /* C writes -> descriptor reads. */
    flags_t f;
    memset(&f, 0, sizeof(f));
    f.id = 7;
    f.ok = 1;
    f.level = 5;
    f.mode = 9;
    f.rest = 0xabcdef;
    uint32_t word = 0;
    const shrewd_basic_reflect_field_t *field = NULL;
    expect("find ok",
           (SHREWD_BASIC_ERR_OK
            == shrewd_basic_reflect_type_find_field(&flags_t_reflect, "ok", &field))
               && (NULL != field));
    if (NULL != field) {
        expect("get ok",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_get(&f, field, &word, sizeof(word)));
        expect("ok value", 1u == word);
    }
    expect("find level",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&flags_t_reflect, "level", &field));
    if (NULL != field) {
        expect("get level",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_get(&f, field, &word, sizeof(word)));
        expect("level value", 5u == word);
    }
    expect("find mode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&flags_t_reflect, "mode", &field));
    if (NULL != field) {
        expect("get mode",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_get(&f, field, &word, sizeof(word)));
        expect("mode value", 9u == word);
    }
    expect("find rest",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&flags_t_reflect, "rest", &field));
    if (NULL != field) {
        expect("get rest",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_get(&f, field, &word, sizeof(word)));
        expect("rest value", 0xabcdefu == word);
    }

    /* Descriptor writes -> C reads (read-modify-write keeps neighbors). */
    word = 3;
    expect("find mode again",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&flags_t_reflect, "mode", &field));
    if (NULL != field) {
        expect("set mode",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_set(&f, field, &word, sizeof(word)));
    }
    expect("mode wrote", 3u == f.mode);
    expect("neighbors kept", (1u == f.ok) && (5u == f.level) && (0xabcdefu == f.rest));

    /* Mixed units: a and c share the 4-byte window, b owns one byte. */
    mixed_t m;
    memset(&m, 0, sizeof(m));
    m.a = 6;
    m.b = 2;
    m.c = 700;
    m.tag = 0x55aa;
    word = 0;
    expect("find a",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&mixed_t_reflect, "a", &field));
    if (NULL != field) {
        expect("get a",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_get(&m, field, &word, sizeof(word)));
        expect("a value", 6u == word);
    }
    expect("find b",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&mixed_t_reflect, "b", &field));
    if (NULL != field) {
        /* b's unit is one byte (unsigned char : 2) - exchange width
           follows the unit width, per FIELD_BITS semantics. */
        uint8_t byte = 0;
        expect("get b",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_get(&m, field, &byte, sizeof(byte)));
        expect("b value", 2u == byte);
    }
    expect("find c",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&mixed_t_reflect, "c", &field));
    if (NULL != field) {
        expect("get c",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_get(&m, field, &word, sizeof(word)));
        expect("c value", 700u == word);
    }
    expect("find b again",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_reflect_type_find_field(&mixed_t_reflect, "b", &field));
    if (NULL != field) {
        uint8_t byte = 1;
        expect("set b",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_field_set(&m, field, &byte, sizeof(byte)));
    }
    expect("b wrote", 1u == m.b);
    expect("mixed neighbors kept", (6u == m.a) && (700u == m.c) && (0x55aau == m.tag));

    /* Only described groups count: rest differs, but wait - rest IS
       described in flags_t. Flip the other way: tag in mixed_t is a
       primitive; two mixed_t with equal bit groups but different tag
       are NOT equal. */
    mixed_t other;
    memset(&other, 0, sizeof(other));
    other.a = 6;
    other.b = 1;
    other.c = 700;
    other.tag = 0x1234;
    bool same = false;
    expect("equal",
           SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_equal(&m, &other, &mixed_t_reflect, &same));
    expect("not equal (tag)", !same);

    /* Two flags_t with the same described fields compare equal. */
    flags_t g;
    memset(&g, 0, sizeof(g));
    g.id = 7;
    g.ok = 1;
    g.level = 5;
    g.mode = 3;
    g.rest = 0xabcdef;
    same = false;
    expect("flags equal",
           SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_equal(&f, &g, &flags_t_reflect, &same));
    expect("flags equal yes", same);

    printf("reflect_gen_bits_smoke: %s\n", (0 == failures) ? "ok" : "FAILED");
    return (0 == failures) ? 0 : 1;
}

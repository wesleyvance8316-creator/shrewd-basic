/**
 * @file test_cbor.c
 * @brief Tests for the CBOR mapping (RFC 8949 subset over the shared
 *        binary skeleton). One summary line, non-zero exit on failure.
 */

#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>

#include "shrewd-basic/formats/cbor.h"
#include "shrewd-basic/platform/memory.h"

static int g_failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("test_cbor: FAILED %s\n", name);
        ++g_failures;
    }
}

typedef enum { CB_RED = 2, CB_BLUE = 9 } cb_color_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_cb_color_reflect,
                                 cb_color_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(CB_RED),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(CB_BLUE));

typedef struct {
    int32_t x;
    double y;
} cb_point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_cb_point_reflect,
                                 cb_point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_point_t, y));

typedef struct {
    bool flag;
    int8_t s8;
    uint16_t u16;
    float f;
    double d;
    long double ld;
    cb_color_t color;
    char c;
    char name[8];
    const char *owned;
    const char16_t *wide;
    char16_t wbuf[8];
    double pair[2];
    int32_t arr[3];
    cb_point_t pt;
    const cb_point_t *ptr;
} cb_all_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_cb_all_reflect,
    cb_all_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_all_t, flag),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_all_t, s8),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_all_t, u16),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_all_t, f),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_all_t, d),
    SHREWD_BASIC_REFLECT_FIELD_LONG_DOUBLE(cb_all_t, ld),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(cb_all_t, color, g_cb_color_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_all_t, c),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(cb_all_t, name),
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(cb_all_t, owned),
    SHREWD_BASIC_REFLECT_FIELD_STRING16(cb_all_t, wide),
    SHREWD_BASIC_REFLECT_FIELD_STRING16_BUFFER(cb_all_t, wbuf),
    SHREWD_BASIC_REFLECT_FIELD_COMPLEX(cb_all_t, pair),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(cb_all_t, arr, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(cb_all_t, pt, g_cb_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR(cb_all_t, ptr, g_cb_point_reflect));

typedef struct {
    int32_t n;
    bool b;
} cb_two_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_cb_two_reflect,
                                 cb_two_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_two_t, n),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cb_two_t, b));

static const char16_t g_wide[] = {'c', 'a', 'f', 0x00e9, 0}; /* café */

int main(void) {
    cb_all_t src;
    cb_all_t back;
    cb_point_t point;
    void *bytes = NULL;
    size_t size = 0;

    /* Round trip every shape through the wire. */
    memset(&src, 0, sizeof(src));
    memset(&point, 0, sizeof(point));
    src.flag = true;
    src.s8 = -8;
    src.u16 = 65000;
    src.f = 1.25f;
    src.d = -2.5;
    src.ld = 2.5L;
    src.color = CB_BLUE;
    src.c = 'z';
    memcpy(src.name, "hi", 3);
    src.owned = "owned text";
    src.wide = g_wide;
    src.wbuf[0] = 'o';
    src.wbuf[1] = 0x3042;
    src.pair[0] = 1.5;
    src.pair[1] = -2.5;
    src.arr[0] = 7;
    src.arr[1] = 8;
    src.arr[2] = 9;
    src.pt.x = 3;
    src.pt.y = 4.5;
    point.x = 11;
    point.y = 12.5;
    src.ptr = &point;

    expect("encode",
           SHREWD_BASIC_ERR_OK == shrewd_basic_cbor_encode(&src, &g_cb_all_reflect, &bytes, &size));
    expect("encode bytes", (NULL != bytes) && (0 < size));
    memset(&back, 0, sizeof(back));
    expect("decode",
           SHREWD_BASIC_ERR_OK == shrewd_basic_cbor_decode(bytes, size, &back, &g_cb_all_reflect));
    expect("free bytes",
           SHREWD_BASIC_ERR_OK == ((shrewd_basic_cbor_bytes_free(bytes), SHREWD_BASIC_ERR_OK)));
    bytes = NULL;
    {
        bool same = false;
        expect("equal",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_equal(&src, &back, &g_cb_all_reflect, &same));
        expect("equal value", same);
    }
    expect("free", SHREWD_BASIC_ERR_OK == shrewd_basic_cbor_free(&back, &g_cb_all_reflect));

    /* NULL pointer maps to null and back. */
    src.ptr = NULL;
    expect("encode null ptr",
           SHREWD_BASIC_ERR_OK == shrewd_basic_cbor_encode(&src, &g_cb_all_reflect, &bytes, &size));
    memset(&back, 0, sizeof(back));
    expect("decode null ptr",
           SHREWD_BASIC_ERR_OK == shrewd_basic_cbor_decode(bytes, size, &back, &g_cb_all_reflect));
    expect("null ptr stayed", NULL == back.ptr);
    shrewd_basic_cbor_bytes_free(bytes);
    bytes = NULL;
    (void)shrewd_basic_cbor_free(&back, &g_cb_all_reflect);

    /* Wire pinning: {"n":-1,"b":true} -> a2 61 6e 20 61 62 f5. */
    {
        cb_two_t two;
        unsigned char expect_bytes[] = {0xa2, 0x61, 0x6e, 0x20, 0x61, 0x62, 0xf5};
        memset(&two, 0, sizeof(two));
        two.n = -1;
        two.b = true;
        expect("encode pin",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_cbor_encode(&two, &g_cb_two_reflect, &bytes, &size));
        expect("pin bytes",
               (size == sizeof(expect_bytes))
                   && (0 == memcmp(bytes, expect_bytes, sizeof(expect_bytes))));
        shrewd_basic_cbor_bytes_free(bytes);
        bytes = NULL;
    }

    /* f16 on decode (widened): 0xf9 0x3c 0x00 is 1.0. */
    {
        cb_all_t one;
        unsigned char wire[] = {0xa1, 0x61, 0x64, 0xf9, 0x3c, 0x00};
        memset(&one, 0, sizeof(one));
        expect("f16 decode",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_cbor_decode(wire, sizeof(wire), &one, &g_cb_all_reflect));
        expect("f16 value", 1.0 == one.d);
        (void)shrewd_basic_cbor_free(&one, &g_cb_all_reflect);
    }

    /* Strictness: indefinite, tags, trailing, truncation, narrowing. */
    {
        cb_two_t two;
        unsigned char indefinite[] = {0x9f, 0xff};
        unsigned char tag[] = {0xc0, 0x00};
        unsigned char trailing[] = {0xa0, 0x00};
        unsigned char cut[] = {0xa2, 0x61};
        unsigned char big[] = {0xa1, 0x61, 0x6e, 0x1a, 0x80, 0x00, 0x00, 0x00}; /* n = 2^31 */
        memset(&two, 0, sizeof(two));
        expect("indefinite refused",
               SHREWD_BASIC_ERR_BIN_UNSUPPORTED
                   == shrewd_basic_cbor_decode(
                       indefinite, sizeof(indefinite), &two, &g_cb_two_reflect));
        expect("tag refused",
               SHREWD_BASIC_ERR_BIN_UNSUPPORTED
                   == shrewd_basic_cbor_decode(tag, sizeof(tag), &two, &g_cb_two_reflect));
        expect(
            "trailing refused",
            SHREWD_BASIC_ERR_BIN_FORMAT
                == shrewd_basic_cbor_decode(trailing, sizeof(trailing), &two, &g_cb_two_reflect));
        expect("truncated refused",
               SHREWD_BASIC_ERR_BIN_FORMAT
                   == shrewd_basic_cbor_decode(cut, sizeof(cut), &two, &g_cb_two_reflect));
        expect("narrowing refused",
               SHREWD_BASIC_ERR_REFLECT_MISMATCH
                   == shrewd_basic_cbor_decode(big, sizeof(big), &two, &g_cb_two_reflect));
    }

#if LDBL_MANT_DIG > DBL_MANT_DIG
    /* Long double precision guard on encode. */
    {
        void *wire = NULL;
        size_t n = 0;
        memset(&src, 0, sizeof(src));
        src.ld = 1.0L + (long double)1.0e-18;
        expect("ld precision refused",
               SHREWD_BASIC_ERR_BIN_UNSUPPORTED
                   == shrewd_basic_cbor_encode(&src, &g_cb_all_reflect, &wire, &n));
    }
#endif

    if (0 != g_failures) {
        printf("test_cbor: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_cbor: ok\n");
    return 0;
}

/**
 * @file test_bson.c
 * @brief Tests for the BSON mapping (same shape as msgpack/CBOR; wire
 *        format = length-prefixed documents with cstring keys). One
 *        summary line, non-zero exit on failure.
 */

#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>

#include "shrewd-basic/formats/bson.h"
#include "shrewd-basic/platform/memory.h"

static int g_failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("test_bson: FAILED %s\n", name);
        ++g_failures;
    }
}

typedef enum { BS_RED = 2, BS_BLUE = 9 } bs_color_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_bs_color_reflect,
                                 bs_color_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(BS_RED),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(BS_BLUE));

typedef struct {
    int32_t x;
    double y;
} bs_point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_bs_point_reflect,
                                 bs_point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_point_t, y));

typedef struct {
    bool flag;
    int8_t s8;
    uint16_t u16;
    float f;
    double d;
    long double ld;
    bs_color_t color;
    char c;
    char name[8];
    const char *owned;
    const char16_t *wide;
    char16_t wbuf[8];
    double pair[2];
    int32_t arr[3];
    bs_point_t pt;
    const bs_point_t *ptr;
} bs_all_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_bs_all_reflect,
    bs_all_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_all_t, flag),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_all_t, s8),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_all_t, u16),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_all_t, f),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_all_t, d),
    SHREWD_BASIC_REFLECT_FIELD_LONG_DOUBLE(bs_all_t, ld),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(bs_all_t, color, g_bs_color_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_all_t, c),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(bs_all_t, name),
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(bs_all_t, owned),
    SHREWD_BASIC_REFLECT_FIELD_STRING16(bs_all_t, wide),
    SHREWD_BASIC_REFLECT_FIELD_STRING16_BUFFER(bs_all_t, wbuf),
    SHREWD_BASIC_REFLECT_FIELD_COMPLEX(bs_all_t, pair),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(bs_all_t, arr, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(bs_all_t, pt, g_bs_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR(bs_all_t, ptr, g_bs_point_reflect));

typedef struct {
    int32_t n;
    bool b;
} bs_two_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_bs_two_reflect,
                                 bs_two_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_two_t, n),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bs_two_t, b));

/* Multi-dimension arrays are refused (row type behind FIELD_ARRAY). */
SHREWD_BASIC_REFLECT_ARRAY_DEFINE(g_bs_row2, shrewd_basic_reflect_int32, int32_t, 2);

typedef struct {
    int32_t cube[2][2];
} bs_cube_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_bs_cube_reflect,
                                 bs_cube_t,
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(bs_cube_t, cube, g_bs_row2));

static const char16_t g_wide[] = {'c', 'a', 'f', 0x00e9, 0xd83d, 0xde00, 0}; /* caf + U+1F600 */

typedef struct {
    int32_t v[2];
    char tag[8];
    double d;
} pin_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_pin_reflect,
                                 pin_t,
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(pin_t,
                                                                  v,
                                                                  shrewd_basic_reflect_int32),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(pin_t, tag),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(pin_t, d));

typedef struct {
    int64_t big;
} big_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_big_reflect,
                                 big_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(big_t, big));

typedef struct {
    uint8_t small;
} narrow_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_narrow_reflect,
                                 narrow_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(narrow_t, small));

int main(void) {
    bs_all_t src;
    bs_all_t back;
    bs_point_t point;
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
    src.color = BS_BLUE;
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
           SHREWD_BASIC_ERR_OK == shrewd_basic_bson_encode(&src, &g_bs_all_reflect, &bytes, &size));
    expect("encode bytes", (NULL != bytes) && (0 < size));
    memset(&back, 0, sizeof(back));
    expect("decode",
           SHREWD_BASIC_ERR_OK == shrewd_basic_bson_decode(bytes, size, &back, &g_bs_all_reflect));
    shrewd_basic_bson_bytes_free(bytes);
    bytes = NULL;
    {
        bool same = false;
        expect("equal",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_equal(&src, &back, &g_bs_all_reflect, &same));
        expect("equal value", same);
    }
    expect("free", SHREWD_BASIC_ERR_OK == shrewd_basic_bson_free(&back, &g_bs_all_reflect));

    /* NULL pointer maps to null and back. */
    src.ptr = NULL;
    expect("encode null ptr",
           SHREWD_BASIC_ERR_OK == shrewd_basic_bson_encode(&src, &g_bs_all_reflect, &bytes, &size));
    memset(&back, 0, sizeof(back));
    expect("decode null ptr",
           SHREWD_BASIC_ERR_OK == shrewd_basic_bson_decode(bytes, size, &back, &g_bs_all_reflect));
    expect("null ptr stayed", NULL == back.ptr);
    shrewd_basic_bson_bytes_free(bytes);
    bytes = NULL;
    (void)shrewd_basic_bson_free(&back, &g_bs_all_reflect);

    /* Wire pinning: {"n":-1,"b":true} -> length backfill + cstring keys. */
    {
        bs_two_t two;
        unsigned char expect_bytes[] = {0x10,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x10,
                                        0x6e,
                                        0x00,
                                        0xff,
                                        0xff,
                                        0xff,
                                        0xff,
                                        0x08,
                                        0x62,
                                        0x00,
                                        0x01,
                                        0x00};
        memset(&two, 0, sizeof(two));
        two.n = -1;
        two.b = true;
        expect("encode pin",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_bson_encode(&two, &g_bs_two_reflect, &bytes, &size));
        expect("pin bytes",
               (size == sizeof(expect_bytes))
                   && (0 == memcmp(bytes, expect_bytes, sizeof(expect_bytes))));
        shrewd_basic_bson_bytes_free(bytes);
        bytes = NULL;
    }

    /* Wire pinning: array document keyed "0","1"; string length incl NUL;
       double big-endian. */
    {
        pin_t p;
        unsigned char expect_bytes[]
            = {0x32, 0x00, 0x00, 0x00, 0x04, 0x76, 0x00, 0x13, 0x00, 0x00, 0x00, 0x10, 0x30,
               0x00, 0x07, 0x00, 0x00, 0x00, 0x10, 0x31, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
               0x02, 0x74, 0x61, 0x67, 0x00, 0x03, 0x00, 0x00, 0x00, 0x68, 0x69, 0x00, 0x01,
               0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f, 0x00};
        memset(&p, 0, sizeof(p));
        p.v[0] = 7;
        p.v[1] = 8;
        memcpy(p.tag, "hi", 3);
        p.d = 1.0;
        expect("encode pin2",
               SHREWD_BASIC_ERR_OK == shrewd_basic_bson_encode(&p, &g_pin_reflect, &bytes, &size));
        expect("pin2 bytes",
               (size == sizeof(expect_bytes))
                   && (0 == memcmp(bytes, expect_bytes, sizeof(expect_bytes))));
        shrewd_basic_bson_bytes_free(bytes);
        bytes = NULL;
    }

    /* int32/int64 choice and narrowing refusal. */
    {
        big_t b1;
        unsigned char *wire = NULL;
        size_t n = 0;
        memset(&b1, 0, sizeof(b1));
        b1.big = 7;
        expect("small int",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_bson_encode(&b1, &g_big_reflect, (void **)&wire, &n));
        expect("small uses int32", (NULL != wire) && (0x10 == wire[4]));
        shrewd_basic_bson_bytes_free(wire);
        wire = NULL;
        b1.big = INT64_C(5000000000);
        expect("big int",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_bson_encode(&b1, &g_big_reflect, (void **)&wire, &n));
        expect("big uses int64", (NULL != wire) && (0x12 == wire[4]));
        shrewd_basic_bson_bytes_free(wire);
        wire = NULL;
        {
            narrow_t n1;
            unsigned char wide_wire[]
                = {0x14, 0x00, 0x00, 0x00, 0x12, 0x73, 0x6d, 0x61, 0x6c, 0x6c,
                   0x00, 0x2c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            memset(&n1, 0, sizeof(n1));
            expect("narrowing refused",
                   SHREWD_BASIC_ERR_REFLECT_MISMATCH
                       == shrewd_basic_bson_decode(
                           wide_wire, sizeof(wide_wire), &n1, &g_narrow_reflect));
        }
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
                   == shrewd_basic_bson_encode(&src, &g_bs_all_reflect, &wire, &n));
    }
#endif

    /* Refusals: exotic BSON types, malformed streams, multi-dim. */
    {
        bs_two_t two;
        unsigned char exotic[] = {0x14, 0x00, 0x00, 0x00, 0x07, 0x78, 0x00, 0x01, 0x02, 0x03,
                                  0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x00};
        unsigned char trailing[] = {0x08, 0x00, 0x00, 0x00, 0x0a, 0x78, 0x00, 0x00, 0x00};
        unsigned char cut[] = {0x0c, 0x00, 0x00, 0x00, 0x10, 0x61};
        unsigned char badidx[] = {0x14, 0x00, 0x00, 0x00, 0x04, 0x76, 0x00, 0x0c, 0x00, 0x00,
                                  0x00, 0x10, 0x7a, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00};
        bs_cube_t cube;
        void *wire = NULL;
        size_t n = 0;
        memset(&two, 0, sizeof(two));
        memset(&cube, 0, sizeof(cube));
        expect("exotic type refused",
               SHREWD_BASIC_ERR_BIN_UNSUPPORTED
                   == shrewd_basic_bson_decode(exotic, sizeof(exotic), &two, &g_bs_two_reflect));
        expect(
            "trailing refused",
            SHREWD_BASIC_ERR_BIN_FORMAT
                == shrewd_basic_bson_decode(trailing, sizeof(trailing), &two, &g_bs_two_reflect));
        expect("truncated refused",
               SHREWD_BASIC_ERR_BIN_FORMAT
                   == shrewd_basic_bson_decode(cut, sizeof(cut), &two, &g_bs_two_reflect));
        expect("bad index refused",
               SHREWD_BASIC_ERR_BIN_FORMAT
                   == shrewd_basic_bson_decode(badidx, sizeof(badidx), &two, &g_bs_two_reflect));
        expect("multidim refused",
               SHREWD_BASIC_ERR_BIN_UNSUPPORTED
                   == shrewd_basic_bson_encode(&cube, &g_bs_cube_reflect, &wire, &n));
    }

    if (0 != g_failures) {
        printf("test_bson: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_bson: ok\n");
    return 0;
}

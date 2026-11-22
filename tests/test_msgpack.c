/**
 * @file test_msgpack.c
 * @brief Tests for the MessagePack mapping (encode/decode/free).
 *
 * Round-trips every supported field shape, pins wire bytes (minimal
 * integer encodings, big-endian multi-byte numbers, str/map headers),
 * checks the strict failure surface (truncation, trailing bytes, wrong
 * wire types, integer ranges, buffer bounds) and the loud refusal list
 * (bit groups, unions, flexible tails, dynamic arrays, bit-precise
 * integers, opaque). Prints failures and exits non-zero.
 */

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uchar.h>

#include "shrewd-basic/formats/msgpack.h"
#include "shrewd-basic/platform/memory.h"

static int g_failures = 0;

static void expect_eq_int(const char *name, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, actual, expected);
        ++g_failures;
    }
}

static void expect_true(const char *name, int condition) {
    if (0 == condition) {
        fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

/* --- fixtures (file scope: the DEFINE macros emit extern objects) -------- */

typedef enum { MP_RED = 1, MP_BLUE = 7 } mp_color_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_mp_color_reflect,
                                 mp_color_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MP_RED),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MP_BLUE));

typedef struct {
    int32_t x;
    int32_t y;
} mp_point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_mp_point_reflect,
                                 mp_point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_point_t, y));

typedef struct {
    bool flag;
    char ch;
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    float f;
    double d;
    long double ld;
    double pair[2];
    mp_color_t mode;
    char name[8];
    const char *title;
    const char16_t *wtext;
    char16_t wbuf[8];
    mp_point_t pt;
    int32_t arr[3];
    mp_point_t pts[2];
    mp_point_t *link;
} mp_all_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_mp_all_reflect,
    mp_all_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, flag),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, ch),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, i8),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, i16),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, i32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, i64),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, u8),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, u16),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, u32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, u64),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, f),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, d),
    SHREWD_BASIC_REFLECT_FIELD_LONG_DOUBLE(mp_all_t, ld),
    SHREWD_BASIC_REFLECT_FIELD_COMPLEX(mp_all_t, pair),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(mp_all_t, mode, g_mp_color_reflect),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(mp_all_t, name),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_all_t, title),
    SHREWD_BASIC_REFLECT_FIELD_STRING16(mp_all_t, wtext),
    SHREWD_BASIC_REFLECT_FIELD_STRING16_BUFFER(mp_all_t, wbuf),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(mp_all_t, pt, g_mp_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(mp_all_t, arr, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(mp_all_t, pts, g_mp_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR(mp_all_t, link, g_mp_point_reflect));

typedef struct {
    int32_t n;
} mp_one_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_mp_one_reflect,
                                 mp_one_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_one_t, n));

typedef struct {
    uint8_t v;
} mp_small_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_mp_small_reflect,
                                 mp_small_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_small_t, v));

typedef struct {
    char buf[4];
} mp_textbuf_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_mp_textbuf_reflect,
                                 mp_textbuf_t,
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(mp_textbuf_t, buf));

typedef struct {
    int32_t unit;
    int32_t grp;
} mp_bits_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_mp_bits_reflect,
                                 mp_bits_t,
                                 SHREWD_BASIC_REFLECT_FIELD_BITS(mp_bits_t, unit, grp, 0, 4),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_bits_t, grp));

typedef union {
    int32_t a;
    float b;
} mp_union_t;

SHREWD_BASIC_REFLECT_UNION_DEFINE(
    g_mp_union_reflect,
    mp_union_t,
    SHREWD_BASIC_REFLECT_UNION_VARIANT(1, SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_union_t, a)),
    SHREWD_BASIC_REFLECT_UNION_VARIANT(2, SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_union_t, b)));

typedef struct {
    int32_t kind;
    mp_union_t v;
} mp_ev_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_mp_ev_reflect,
    mp_ev_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_ev_t, kind),
    SHREWD_BASIC_REFLECT_FIELD_UNION(mp_ev_t, v, g_mp_union_reflect, kind));

typedef struct {
    int32_t count;
    int32_t data[];
} mp_flex_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_mp_flex_reflect,
    mp_flex_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_flex_t, count),
    SHREWD_BASIC_REFLECT_FIELD_FLEX(mp_flex_t, data, count, shrewd_basic_reflect_int32));

typedef struct {
    size_t count;
    mp_point_t *items;
} mp_dyn_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_mp_dyn_reflect,
    mp_dyn_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(mp_dyn_t, count),
    SHREWD_BASIC_REFLECT_FIELD_DARRAY(mp_dyn_t, items, count, g_mp_point_reflect));

typedef struct {
    int32_t bits;
} mp_bitint_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_mp_bitint_reflect,
                                 mp_bitint_t,
                                 SHREWD_BASIC_REFLECT_FIELD_BITINT(mp_bitint_t, bits));

typedef struct {
    void *handle;
} mp_opq_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_mp_opq_reflect,
                                 mp_opq_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PTR(mp_opq_t,
                                                                handle,
                                                                shrewd_basic_reflect_void));

/* --- wire pins ----------------------------------------------------------- */

static void expect_wire(const char *name,
                        const void *object,
                        const shrewd_basic_reflect_type_t *type,
                        const unsigned char *want,
                        size_t want_size) {
    void *bytes = NULL;
    size_t size = 0;
    int status = shrewd_basic_msgpack_encode(object, type, &bytes, &size);
    if (SHREWD_BASIC_ERR_OK != status) {
        fprintf(stderr, "FAIL %s: encode returned %d\n", name, status);
        ++g_failures;
        return;
    }
    if ((size != want_size) || (0 != memcmp(bytes, want, want_size))) {
        fprintf(stderr, "FAIL %s: %zu bytes", name, size);
        for (size_t i = 0; (i < size) && (i < 24); ++i) {
            fprintf(stderr, " %02x", ((const unsigned char *)bytes)[i]);
        }
        fprintf(stderr, "\n");
        ++g_failures;
    }
    shrewd_basic_msgpack_bytes_free(bytes);
}

static void test_wire_pins(void) {
    int32_t one = 1;
    int32_t minus_one = -1;
    int32_t minus_forty = -40;
    int32_t minus_two_hundred = -200;
    int32_t three_hundred = 300;
    double real = 1.5;
    mp_one_t n = {1};
    static const unsigned char wire_one[] = {0x01};
    static const unsigned char wire_minus_one[] = {0xff};
    static const unsigned char wire_minus_forty[] = {0xd0, 0xd8};
    static const unsigned char wire_minus_two_hundred[] = {0xd1, 0xff, 0x38};
    static const unsigned char wire_three_hundred[] = {0xcd, 0x01, 0x2c};
    static const unsigned char wire_real[] = {0xcb, 0x3f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned char wire_map[] = {0x81, 0xa1, 'n', 0x01};

    /* Minimal integer encodings and big-endian multi-byte numbers. */
    expect_wire("wire pos fixint", &one, &shrewd_basic_reflect_int32, wire_one, sizeof(wire_one));
    expect_wire("wire neg fixint",
                &minus_one,
                &shrewd_basic_reflect_int32,
                wire_minus_one,
                sizeof(wire_minus_one));
    expect_wire("wire int8",
                &minus_forty,
                &shrewd_basic_reflect_int32,
                wire_minus_forty,
                sizeof(wire_minus_forty));
    expect_wire("wire int16",
                &minus_two_hundred,
                &shrewd_basic_reflect_int32,
                wire_minus_two_hundred,
                sizeof(wire_minus_two_hundred));
    expect_wire("wire uint16",
                &three_hundred,
                &shrewd_basic_reflect_int32,
                wire_three_hundred,
                sizeof(wire_three_hundred));
    expect_wire("wire float64", &real, &shrewd_basic_reflect_double, wire_real, sizeof(wire_real));
    /* fixmap + fixstr key + fixint value. */
    expect_wire("wire struct map", &n, &g_mp_one_reflect, wire_map, sizeof(wire_map));

    /* str8 header for payloads over 31 bytes. */
    {
        char long_text[41];
        const char *pointer = long_text;
        void *bytes = NULL;
        size_t size = 0;
        memset(long_text, 'a', sizeof(long_text) - 1u);
        long_text[sizeof(long_text) - 1u] = '\0';
        expect_eq_int(
            "wire str8 encode",
            shrewd_basic_msgpack_encode(&pointer, &shrewd_basic_reflect_string, &bytes, &size),
            SHREWD_BASIC_ERR_OK);
        if ((NULL != bytes) && (42 == size)) {
            expect_true("wire str8 head",
                        (0xd9u == ((const unsigned char *)bytes)[0])
                            && (40u == ((const unsigned char *)bytes)[1]));
        } else {
            expect_true("wire str8 size", 0);
        }
        shrewd_basic_msgpack_bytes_free(bytes);
    }
}

/* --- round trip ---------------------------------------------------------- */

static void test_roundtrip(void) {
    static const char16_t title_units[] = {'c', 'a', 'f', 0x00e9, 0xd83d, 0xde00, 0};
    mp_point_t link_target = {7, -7};
    mp_all_t src;
    mp_all_t dst;
    void *bytes = NULL;
    size_t size = 0;
    bool same = false;
    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    src.flag = true;
    src.ch = 'A';
    src.i8 = -8;
    src.i16 = -1600;
    src.i32 = -123456;
    src.i64 = INT64_C(-1234567890123);
    src.u8 = 250;
    src.u16 = 65000;
    src.u32 = 4000000000u;
    src.u64 = UINT64_C(12345678901234567);
    src.f = 1.25f;
    src.d = -2.5;
    src.ld = 2.75L;
    src.pair[0] = 1.5;
    src.pair[1] = -0.25;
    src.mode = MP_BLUE;
    memcpy(src.name, "hi", 3);
    src.title = "hello";
    src.wtext = title_units;
    src.wbuf[0] = 'o';
    src.wbuf[1] = 'k';
    src.wbuf[2] = 0;
    src.pt.x = 3;
    src.pt.y = -4;
    src.arr[0] = 1;
    src.arr[1] = 2;
    src.arr[2] = 3;
    src.pts[0].x = 5;
    src.pts[0].y = 6;
    src.pts[1].x = -5;
    src.pts[1].y = -6;
    src.link = &link_target;

    expect_eq_int("roundtrip encode",
                  shrewd_basic_msgpack_encode(&src, &g_mp_all_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("roundtrip decode",
                  shrewd_basic_msgpack_decode(bytes, size, &dst, &g_mp_all_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("roundtrip equal",
                  shrewd_basic_reflect_equal(&src, &dst, &g_mp_all_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("roundtrip same", same);
    expect_true("roundtrip link", (NULL != dst.link) && (7 == dst.link->x));
    expect_eq_int(
        "roundtrip free", shrewd_basic_msgpack_free(&dst, &g_mp_all_reflect), SHREWD_BASIC_ERR_OK);
    shrewd_basic_msgpack_bytes_free(bytes);

    /* nil pointer round-trips as nil. */
    src.link = NULL;
    bytes = NULL;
    size = 0;
    memset(&dst, 0, sizeof(dst));
    expect_eq_int("roundtrip nil encode",
                  shrewd_basic_msgpack_encode(&src, &g_mp_all_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("roundtrip nil decode",
                  shrewd_basic_msgpack_decode(bytes, size, &dst, &g_mp_all_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_true("roundtrip nil link", NULL == dst.link);
    expect_eq_int("roundtrip nil free",
                  shrewd_basic_msgpack_free(&dst, &g_mp_all_reflect),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_msgpack_bytes_free(bytes);

#if LDBL_MANT_DIG > DBL_MANT_DIG
    /* long double values that do not survive the float64 narrowing are
       refused instead of silently rounded. */
    src.ld = 1.0L + (long double)1.0e-18;
    bytes = NULL;
    expect_eq_int("roundtrip ld narrowed",
                  shrewd_basic_msgpack_encode(&src, &g_mp_all_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    expect_true("roundtrip ld no bytes", NULL == bytes);
    src.ld = 2.75L;
#endif

    /* Lone surrogate in wide content: refused, never a replacement. */
    {
        mp_all_t bad;
        memset(&bad, 0, sizeof(bad));
        bad.wbuf[0] = (char16_t)0xd800;
        bytes = NULL;
        expect_eq_int("roundtrip lone surrogate",
                      shrewd_basic_msgpack_encode(&bad, &g_mp_all_reflect, &bytes, &size),
                      SHREWD_BASIC_ERR_REFLECT_ENCODING);
        expect_true("roundtrip lone surrogate clean", NULL == bytes);
    }
}

/* --- decode strictness --------------------------------------------------- */

static void test_decode_strict(void) {
    mp_one_t out;
    void *bytes = NULL;
    size_t size = 0;
    mp_one_t n = {1};

    expect_eq_int("strict encode",
                  shrewd_basic_msgpack_encode(&n, &g_mp_one_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    memset(&out, 0, sizeof(out));
    expect_eq_int("strict truncation",
                  shrewd_basic_msgpack_decode(bytes, size - 1u, &out, &g_mp_one_reflect),
                  SHREWD_BASIC_ERR_BIN_FORMAT);
    {
        unsigned char padded[32];
        memcpy(padded, bytes, size);
        padded[size] = 0xc0u; /* trailing nil */
        memset(&out, 0, sizeof(out));
        expect_eq_int("strict trailing",
                      shrewd_basic_msgpack_decode(padded, size + 1u, &out, &g_mp_one_reflect),
                      SHREWD_BASIC_ERR_BIN_FORMAT);
        expect_eq_int("strict wrong root",
                      shrewd_basic_msgpack_decode(padded, 1u, &out, &g_mp_one_reflect),
                      SHREWD_BASIC_ERR_BIN_FORMAT);
    }
    shrewd_basic_msgpack_bytes_free(bytes);

    /* Unknown keys are ignored, first duplicate wins. */
    {
        static const unsigned char wire[]
            = {0x83, 0xa3, 'z', 'z', 'z', 0x01, 0xa1, 'n', 0x05, 0xa1, 'n', 0x06};
        memset(&out, 0, sizeof(out));
        expect_eq_int("strict keys",
                      shrewd_basic_msgpack_decode(wire, sizeof(wire), &out, &g_mp_one_reflect),
                      SHREWD_BASIC_ERR_OK);
        expect_true("strict first wins", 5 == out.n);
    }
    /* Missing keys stay zero. */
    {
        static const unsigned char wire[] = {0x80};
        memset(&out, 0, sizeof(out));
        expect_eq_int("strict empty map",
                      shrewd_basic_msgpack_decode(wire, sizeof(wire), &out, &g_mp_one_reflect),
                      SHREWD_BASIC_ERR_OK);
        expect_true("strict missing zero", 0 == out.n);
    }
    /* Wide wire into a wide field is fine (uint16 300 -> int32). */
    {
        static const unsigned char wire[] = {0x81, 0xa1, 'n', 0xcd, 0x01, 0x2c};
        memset(&out, 0, sizeof(out));
        expect_eq_int("strict wide ok",
                      shrewd_basic_msgpack_decode(wire, sizeof(wire), &out, &g_mp_one_reflect),
                      SHREWD_BASIC_ERR_OK);
        expect_true("strict wide value", 300 == out.n);
    }
    /* Range and shape refusals. */
    {
        static const unsigned char wire[] = {0x81, 0xa1, 'v', 0xcd, 0x01, 0x2c};
        mp_small_t small;
        memset(&small, 0, sizeof(small));
        expect_eq_int("strict uint8 range",
                      shrewd_basic_msgpack_decode(wire, sizeof(wire), &small, &g_mp_small_reflect),
                      SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    }
    {
        static const unsigned char wire[]
            = {0x81, 0xa3, 'b', 'u', 'f', 0xa5, 'a', 'b', 'c', 'd', 'e'};
        mp_textbuf_t buf;
        memset(&buf, 0, sizeof(buf));
        expect_eq_int("strict buffer long",
                      shrewd_basic_msgpack_decode(wire, sizeof(wire), &buf, &g_mp_textbuf_reflect),
                      SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    }
    {
        static const unsigned char wire[] = {0x81, 0xa1, 'n', 0xa3, 'a', 'b', 'c'};
        memset(&out, 0, sizeof(out));
        expect_eq_int("strict wrong type",
                      shrewd_basic_msgpack_decode(wire, sizeof(wire), &out, &g_mp_one_reflect),
                      SHREWD_BASIC_ERR_BIN_FORMAT);
    }
}

/* --- loud refusal list --------------------------------------------------- */

static void test_refusals(void) {
    void *bytes = NULL;
    size_t size = 0;
    mp_bits_t bits;
    mp_ev_t ev;
    mp_flex_t flex;
    mp_dyn_t dyn;
    mp_bitint_t bi;
    mp_opq_t opq;

    memset(&bits, 0, sizeof(bits));
    expect_eq_int("refuse bits",
                  shrewd_basic_msgpack_encode(&bits, &g_mp_bits_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    memset(&ev, 0, sizeof(ev));
    expect_eq_int("refuse union",
                  shrewd_basic_msgpack_encode(&ev, &g_mp_ev_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    expect_eq_int("refuse union decode",
                  shrewd_basic_msgpack_decode("\x80", 1, &ev, &g_mp_ev_reflect),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    memset(&flex, 0, sizeof(flex));
    expect_eq_int("refuse flex",
                  shrewd_basic_msgpack_encode(&flex, &g_mp_flex_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    memset(&dyn, 0, sizeof(dyn));
    expect_eq_int("refuse darray",
                  shrewd_basic_msgpack_encode(&dyn, &g_mp_dyn_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    memset(&bi, 0, sizeof(bi));
    expect_eq_int("refuse bitint",
                  shrewd_basic_msgpack_encode(&bi, &g_mp_bitint_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    memset(&opq, 0, sizeof(opq));
    expect_eq_int("refuse opaque",
                  shrewd_basic_msgpack_encode(&opq, &g_mp_opq_reflect, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    expect_true("refuse left no bytes", NULL == bytes);
}

int main(void) {
    test_wire_pins();
    test_roundtrip();
    test_decode_strict();
    test_refusals();
    if (0 != g_failures) {
        fprintf(stderr, "test_msgpack: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_msgpack: ok\n");
    return 0;
}

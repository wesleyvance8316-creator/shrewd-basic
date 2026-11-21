#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/bin.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

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

/* --- fixtures ---------------------------------------------------------- */

typedef enum { BIN_MODE_OFF = 0, BIN_MODE_ON = 1, BIN_MODE_AUTO = 7 } bin_mode_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_mode_reflect,
                                 bin_mode_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(BIN_MODE_OFF),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(BIN_MODE_ON),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(BIN_MODE_AUTO));

typedef struct {
    bool active;
    char grade;
    int8_t s8;
    uint8_t u8;
    int16_t s16;
    uint16_t u16;
    int32_t s32;
    uint32_t u32;
    int64_t s64;
    uint64_t u64;
    float f;
    double d;
    bin_mode_t mode;
} scalars_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_scalars_reflect,
                                 scalars_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, active),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, grade),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, s8),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, u8),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, s16),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, u16),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, s32),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, u32),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, s64),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, u64),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, f),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(scalars_t, d),
                                 SHREWD_BASIC_REFLECT_FIELD_ENUM(scalars_t, mode, g_mode_reflect));

typedef struct {
    int32_t x;
    int32_t y;
} point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_point_reflect,
                                 point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

typedef struct {
    const char *name;
    char tag[8];
} text_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_text_reflect,
                                 text_t,
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(text_t, name),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(text_t, tag));

SHREWD_BASIC_REFLECT_ARRAY_DEFINE(g_row3_reflect, shrewd_basic_reflect_int32, int32_t, 3);

typedef struct {
    int32_t scores[4];
    int32_t m[2][3];
} arr_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_arr_reflect,
                                 arr_t,
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(arr_t,
                                                                  scores,
                                                                  shrewd_basic_reflect_int32),
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(arr_t, m, g_row3_reflect));

typedef struct {
    int32_t *items;
    size_t count;
    const char **names; /* argv-style NULL-terminated pointer table */
} dyn_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_dyn_reflect,
    dyn_t,
    SHREWD_BASIC_REFLECT_FIELD_DARRAY(dyn_t, items, count, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dyn_t, count),
    SHREWD_BASIC_REFLECT_FIELD_SENTINEL(dyn_t, names, shrewd_basic_reflect_string));

typedef struct {
    int32_t len;
    int32_t data[];
} flex_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_flex_reflect,
    flex_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(flex_t, len),
    SHREWD_BASIC_REFLECT_FIELD_FLEX(flex_t, data, len, shrewd_basic_reflect_int32));

typedef struct {
    point_t *home;
    const char *note;
} ptr_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_ptr_reflect,
                                 ptr_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PTR(ptr_t, home, g_point_reflect),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(ptr_t, note));

typedef union {
    int32_t n;
    point_t point;
} body_t;

typedef struct {
    bin_mode_t kind;
    body_t body;
} msg_t;

SHREWD_BASIC_REFLECT_UNION_DEFINE(
    g_body_reflect,
    body_t,
    SHREWD_BASIC_REFLECT_UNION_VARIANT(BIN_MODE_OFF,
                                       SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(body_t, n)),
    SHREWD_BASIC_REFLECT_UNION_VARIANT(
        BIN_MODE_ON, SHREWD_BASIC_REFLECT_FIELD_STRUCT(body_t, point, g_point_reflect)));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_msg_reflect,
    msg_t,
    SHREWD_BASIC_REFLECT_FIELD_ENUM(msg_t, kind, g_mode_reflect),
    SHREWD_BASIC_REFLECT_FIELD_UNION(msg_t, body, g_body_reflect, kind));

typedef struct {
    uint32_t raw;
} bits_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_bits_reflect,
                                 bits_t,
                                 SHREWD_BASIC_REFLECT_FIELD_BITS(bits_t, raw, ok, 0, 1),
                                 SHREWD_BASIC_REFLECT_FIELD_BITS(bits_t, raw, level, 1, 3));

typedef struct node {
    int32_t data;
    struct node *next;
} node_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_node_reflect,
                                 node_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(node_t, data),
                                 SHREWD_BASIC_REFLECT_FIELD_PTR(node_t, next, g_node_reflect));

/* --- helpers ----------------------------------------------------------- */

static int round_trip(const void *src,
                      const void *type_ref,
                      const shrewd_basic_bin_options_t *options,
                      void *dst,
                      size_t dst_size) {
    void *bytes = NULL;
    size_t size = 0;
    int status = shrewd_basic_bin_encode(src, type_ref, options, &bytes, &size);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memset(dst, 0, dst_size);
    status = shrewd_basic_bin_decode(bytes, size, dst, type_ref, options);
    shrewd_basic_bin_bytes_free(bytes);
    return status;
}

static int round_trip_equal(const void *src,
                            const void *type_ref,
                            const shrewd_basic_bin_options_t *options,
                            void *dst,
                            size_t dst_size,
                            bool *out_equal) {
    int status = round_trip(src, type_ref, options, dst, dst_size);
    *out_equal = false;
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_reflect_equal(src, dst, type_ref, out_equal);
}

/* --- tests ------------------------------------------------------------- */

static void test_scalars(void) {
    scalars_t src;
    scalars_t dst;
    bool same = false;
    memset(&src, 0, sizeof(src));
    src.active = true;
    src.grade = 'A';
    src.s8 = -8;
    src.u8 = 250;
    src.s16 = -1600;
    src.u16 = 65000;
    src.s32 = -123456;
    src.u32 = 4000000000u;
    src.s64 = INT64_C(-1234567890123);
    src.u64 = UINT64_C(12345678901234567);
    src.f = 1.25f;
    src.d = -2.5;
    src.mode = BIN_MODE_AUTO;
    expect_eq_int("scalars round trip",
                  round_trip_equal(&src, &g_scalars_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("scalars equal", same);
    expect_eq_int(
        "scalars free", shrewd_basic_bin_free(&dst, &g_scalars_reflect), SHREWD_BASIC_ERR_OK);
}

static void test_endian_layout(void) {
    point_t src;
    void *bytes = NULL;
    size_t size = 0;
    const unsigned char *data = NULL;
    memset(&src, 0, sizeof(src));
    src.x = (int32_t)0x01020304;
    expect_eq_int("endian le encode",
                  shrewd_basic_bin_encode(&src, &g_point_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    data = (const unsigned char *)bytes;
    expect_true("endian le size", size == (12 + 8));
    expect_true("endian le bytes",
                (0x04 == data[12]) && (0x03 == data[13]) && (0x02 == data[14])
                    && (0x01 == data[15]));
    shrewd_basic_bin_bytes_free(bytes);
    {
        shrewd_basic_bin_options_t options;
        bytes = NULL;
        memset(&options, 0, sizeof(options));
        options.endian = SHREWD_BASIC_BIN_ENDIAN_BIG;
        expect_eq_int("endian be encode",
                      shrewd_basic_bin_encode(&src, &g_point_reflect, &options, &bytes, &size),
                      SHREWD_BASIC_ERR_OK);
        data = (const unsigned char *)bytes;
        expect_true("endian be bytes",
                    (0x01 == data[12]) && (0x02 == data[13]) && (0x03 == data[14])
                        && (0x04 == data[15]));
        shrewd_basic_bin_bytes_free(bytes);
    }
}

static void test_strings(void) {
    text_t src;
    text_t dst;
    bool same = false;
    memset(&src, 0, sizeof(src));
    src.name = "hello bin";
    memcpy(src.tag, "tag", 4);
    expect_eq_int("strings round trip",
                  round_trip_equal(&src, &g_text_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("strings equal", same);
    expect_true("strings copied", src.name != dst.name);
    expect_eq_int(
        "strings free", shrewd_basic_bin_free(&dst, &g_text_reflect), SHREWD_BASIC_ERR_OK);
    expect_true("strings freed", NULL == dst.name);

    /* NULL string round-trips as NULL. */
    memset(&src, 0, sizeof(src));
    expect_eq_int("strings null",
                  round_trip_equal(&src, &g_text_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("strings null equal", same);
    expect_eq_int(
        "strings null free", shrewd_basic_bin_free(&dst, &g_text_reflect), SHREWD_BASIC_ERR_OK);
}

static void test_arrays(void) {
    arr_t src;
    arr_t dst;
    bool same = false;
    memset(&src, 0, sizeof(src));
    for (size_t i = 0; i < 4; ++i) {
        src.scores[i] = (int32_t)(i + 1);
    }
    for (size_t r = 0; r < 2; ++r) {
        for (size_t c = 0; c < 3; ++c) {
            src.m[r][c] = (int32_t)((10 * r) + c);
        }
    }
    expect_eq_int("arrays round trip",
                  round_trip_equal(&src, &g_arr_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("arrays equal", same);
}

static void test_dyn_and_sentinel(void) {
    dyn_t src;
    dyn_t dst;
    bool same = false;
    void *bytes = NULL;
    size_t size = 0;
    int32_t items[3];
    const char *table[3];
    memset(&src, 0, sizeof(src));
    items[0] = 7;
    items[1] = 8;
    items[2] = 9;
    table[0] = "a";
    table[1] = "b";
    table[2] = NULL;
    src.items = items;
    src.count = 3;
    src.names = table;
    expect_eq_int("dyn encode",
                  shrewd_basic_bin_encode(&src, &g_dyn_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    memset(&dst, 0, sizeof(dst));
    expect_eq_int("dyn decode",
                  shrewd_basic_bin_decode(bytes, size, &dst, &g_dyn_reflect, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dyn count derived", 3 == dst.count);
    expect_true("dyn items copied",
                (NULL != dst.items) && (items != dst.items) && (7 == dst.items[0])
                    && (9 == dst.items[2]));
    expect_true("dyn sentinel filled", (NULL != dst.names) && (NULL == dst.names[2]));
    expect_eq_int("dyn free", shrewd_basic_bin_free(&dst, &g_dyn_reflect), SHREWD_BASIC_ERR_OK);
    expect_true("dyn freed", (NULL == dst.items) && (NULL == dst.names));
    shrewd_basic_bin_bytes_free(bytes);

    /* Empty dynamic array round-trips. */
    memset(&src, 0, sizeof(src));
    expect_eq_int("dyn empty",
                  round_trip_equal(&src, &g_dyn_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dyn empty equal", same);
    expect_eq_int(
        "dyn empty free", shrewd_basic_bin_free(&dst, &g_dyn_reflect), SHREWD_BASIC_ERR_OK);
    (void)shrewd_basic_reflect_equal(&src, &src, &g_dyn_reflect, &same);
}

static void test_flex(void) {
    void *block = NULL;
    void *bytes = NULL;
    size_t size = 0;
    size_t need = 0;
    flex_t *src = NULL;
    flex_t *dst = NULL;
    bool same = false;
    expect_eq_int("flex alloc",
                  shrewd_basic_platform_calloc(&block, 1, sizeof(flex_t) + (3 * sizeof(int32_t))),
                  SHREWD_BASIC_ERR_OK);
    src = (flex_t *)block;
    src->len = 3;
    src->data[0] = 4;
    src->data[1] = 5;
    src->data[2] = 6;
    expect_eq_int("flex encode",
                  shrewd_basic_bin_encode(src, &g_flex_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("flex value size",
                  shrewd_basic_bin_value_size(bytes, size, &g_flex_reflect, &need),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex need", need == (sizeof(flex_t) + (3 * sizeof(int32_t))));
    block = NULL;
    expect_eq_int(
        "flex dst alloc", shrewd_basic_platform_calloc(&block, 1, need), SHREWD_BASIC_ERR_OK);
    dst = (flex_t *)block;
    expect_eq_int("flex decode",
                  shrewd_basic_bin_decode(bytes, size, dst, &g_flex_reflect, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex data", (3 == dst->len) && (4 == dst->data[0]) && (6 == dst->data[2]));
    expect_eq_int("flex equal",
                  shrewd_basic_reflect_equal(src, dst, &g_flex_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex equal yes", same);
    expect_eq_int(
        "flex free dst", shrewd_basic_bin_free(dst, &g_flex_reflect), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "flex free src", shrewd_basic_bin_free(src, &g_flex_reflect), SHREWD_BASIC_ERR_OK);
    shrewd_basic_bin_bytes_free(bytes);
    {
        void *old = dst;
        (void)shrewd_basic_platform_free(&old);
    }
    {
        void *old = src;
        (void)shrewd_basic_platform_free(&old);
    }
}

static void test_pointers(void) {
    point_t home;
    ptr_t src;
    ptr_t dst;
    bool same = false;
    memset(&home, 0, sizeof(home));
    memset(&src, 0, sizeof(src));
    home.x = 3;
    home.y = 4;
    src.home = &home;
    src.note = "note";
    expect_eq_int("ptr round trip",
                  round_trip_equal(&src, &g_ptr_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("ptr equal", same);
    expect_true("ptr cloned", (NULL != dst.home) && (dst.home != src.home) && (3 == dst.home->x));
    expect_eq_int("ptr free", shrewd_basic_bin_free(&dst, &g_ptr_reflect), SHREWD_BASIC_ERR_OK);
    expect_true("ptr freed", (NULL == dst.home) && (NULL == dst.note));

    /* NULL pointer round-trips as NULL. */
    memset(&src, 0, sizeof(src));
    expect_eq_int("ptr null",
                  round_trip_equal(&src, &g_ptr_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("ptr null equal", same);
}

static void test_union(void) {
    msg_t src;
    msg_t dst;
    bool same = false;
    void *bytes = NULL;
    size_t size = 0;
    memset(&src, 0, sizeof(src));
    src.kind = BIN_MODE_ON;
    src.body.point.x = 9;
    src.body.point.y = 10;
    expect_eq_int("union point",
                  round_trip_equal(&src, &g_msg_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("union point equal", same);
    expect_eq_int(
        "union point free", shrewd_basic_bin_free(&dst, &g_msg_reflect), SHREWD_BASIC_ERR_OK);

    src.kind = BIN_MODE_OFF;
    src.body.n = -42;
    expect_eq_int("union int",
                  round_trip_equal(&src, &g_msg_reflect, NULL, &dst, sizeof(dst), &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("union int equal", same);

    /* Unknown tag cannot encode; a corrupted tag cannot decode. */
    src.kind = (bin_mode_t)99;
    expect_eq_int("union unknown tag",
                  shrewd_basic_bin_encode(&src, &g_msg_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
    src.kind = BIN_MODE_ON;
    src.body.point.x = 1;
    src.body.point.y = 2;
    expect_eq_int("union encode for corrupt",
                  shrewd_basic_bin_encode(&src, &g_msg_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    ((unsigned char *)bytes)[20] = 0xff;
    ((unsigned char *)bytes)[21] = 0xff;
    memset(&dst, 0, sizeof(dst));
    expect_eq_int("union corrupt tag",
                  shrewd_basic_bin_decode(bytes, size, &dst, &g_msg_reflect, NULL),
                  SHREWD_BASIC_ERR_BIN_FORMAT);
    expect_eq_int(
        "union corrupt free", shrewd_basic_bin_free(&dst, &g_msg_reflect), SHREWD_BASIC_ERR_OK);
    shrewd_basic_bin_bytes_free(bytes);
}

static void test_bits(void) {
    bits_t src;
    bits_t dst;
    memset(&src, 0, sizeof(src));
    src.raw = 0xdeadbeef;
    expect_eq_int("bits round trip",
                  round_trip(&src, &g_bits_reflect, NULL, &dst, sizeof(dst)),
                  SHREWD_BASIC_ERR_OK);
    /* Only the described bit groups have a wire form: they rebuild the
       unit's low nibble, undescribed unit bits do not survive. */
    expect_true("bits fields", (1 == (dst.raw & 1u)) && (7 == ((dst.raw >> 1) & 7u)));
    expect_true("bits groups rebuilt", 0x0fu == dst.raw);
}

static void test_align_and_version(void) {
    scalars_t src;
    scalars_t dst;
    void *compact = NULL;
    void *padded = NULL;
    size_t compact_size = 0;
    size_t padded_size = 0;
    bool same = false;
    shrewd_basic_bin_options_t options;
    memset(&src, 0, sizeof(src));
    memset(&options, 0, sizeof(options));
    src.s64 = INT64_C(-7);
    src.d = 3.5;
    options.natural_align = 1;
    expect_eq_int(
        "align encode",
        shrewd_basic_bin_encode(&src, &g_scalars_reflect, &options, &padded, &padded_size),
        SHREWD_BASIC_ERR_OK);
    expect_eq_int("compact encode",
                  shrewd_basic_bin_encode(&src, &g_scalars_reflect, NULL, &compact, &compact_size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("align grows", padded_size >= compact_size);
    memset(&dst, 0, sizeof(dst));
    expect_eq_int("align decode",
                  shrewd_basic_bin_decode(padded, padded_size, &dst, &g_scalars_reflect, &options),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("align equal",
                  shrewd_basic_reflect_equal(&src, &dst, &g_scalars_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("align equal yes", same);
    shrewd_basic_bin_bytes_free(padded);
    shrewd_basic_bin_bytes_free(compact);

    /* Version stamp: mismatch fails, zero accepts anything. */
    options.natural_align = 0;
    options.version = 5;
    expect_eq_int(
        "version encode",
        shrewd_basic_bin_encode(&src, &g_scalars_reflect, &options, &compact, &compact_size),
        SHREWD_BASIC_ERR_OK);
    options.version = 6;
    memset(&dst, 0, sizeof(dst));
    expect_eq_int(
        "version mismatch",
        shrewd_basic_bin_decode(compact, compact_size, &dst, &g_scalars_reflect, &options),
        SHREWD_BASIC_ERR_BIN_FORMAT);
    options.version = 5;
    expect_eq_int(
        "version match",
        shrewd_basic_bin_decode(compact, compact_size, &dst, &g_scalars_reflect, &options),
        SHREWD_BASIC_ERR_OK);
    options.version = 0;
    expect_eq_int(
        "version any",
        shrewd_basic_bin_decode(compact, compact_size, &dst, &g_scalars_reflect, &options),
        SHREWD_BASIC_ERR_OK);
    shrewd_basic_bin_bytes_free(compact);
}

static void test_errors(void) {
    point_t src;
    point_t dst;
    void *bytes = NULL;
    size_t size = 0;
    unsigned char junk[16];
    node_t self;
    memset(&src, 0, sizeof(src));
    memset(junk, 0, sizeof(junk));
    expect_eq_int("err null args",
                  shrewd_basic_bin_encode(NULL, &g_point_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("err bad magic",
                  shrewd_basic_bin_decode(junk, sizeof(junk), &dst, &g_point_reflect, NULL),
                  SHREWD_BASIC_ERR_BIN_FORMAT);
    expect_eq_int("err short header",
                  shrewd_basic_bin_decode(junk, 4, &dst, &g_point_reflect, NULL),
                  SHREWD_BASIC_ERR_BIN_FORMAT);

    expect_eq_int("err encode for trunc",
                  shrewd_basic_bin_encode(&src, &g_point_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("err truncated",
                  shrewd_basic_bin_decode(bytes, size - 1, &dst, &g_point_reflect, NULL),
                  SHREWD_BASIC_ERR_BIN_FORMAT);
    expect_eq_int(
        "err truncated free", shrewd_basic_bin_free(&dst, &g_point_reflect), SHREWD_BASIC_ERR_OK);
    shrewd_basic_bin_bytes_free(bytes);

    /* Cycles have no wire form. */
    memset(&self, 0, sizeof(self));
    self.data = 1;
    self.next = &self;
    expect_eq_int("err cycle",
                  shrewd_basic_bin_encode(&self, &g_node_reflect, NULL, &bytes, &size),
                  SHREWD_BASIC_ERR_BIN_UNSUPPORTED);
}

int main(void) {
    test_scalars();
    test_endian_layout();
    test_strings();
    test_arrays();
    test_dyn_and_sentinel();
    test_flex();
    test_pointers();
    test_union();
    test_bits();
    test_align_and_version();
    test_errors();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * @file test_csv.c
 * @brief Tests for the RFC 4180 CSV mapping over reflect-described
 *        struct tables.
 *
 * Round-trips leaf shapes through encode/decode (quoting, enums by
 * name, floats, strings), checks header matching (order-independent,
 * missing/unknown/duplicate columns), nested flattening ("a.b"), the
 * loud shape rejections and the ownership contract. Prints one
 * summary line and exits non-zero on failure.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <uchar.h>

#include "shrewd-basic/formats/csv.h"
#include "shrewd-basic/formats/json.h"
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

typedef struct {
    int32_t x;
    int32_t y;
} csv_point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_csv_point_reflect,
                                 csv_point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(csv_point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(csv_point_t, y));

typedef enum { CSV_COLOR_RED = 1, CSV_COLOR_GREEN = 2 } csv_color_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_csv_color_reflect,
                                 csv_color_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(CSV_COLOR_RED),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(CSV_COLOR_GREEN));

typedef struct {
    int32_t num;
    bool flag;
    double real;
    char name[16];
    const char *owned;
    csv_color_t color;
    csv_point_t origin;
} csv_row_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_csv_row_reflect,
    csv_row_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(csv_row_t, num),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(csv_row_t, flag),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(csv_row_t, real),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(csv_row_t, name),
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(csv_row_t, owned),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(csv_row_t, color, g_csv_color_reflect),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(csv_row_t, origin, g_csv_point_reflect));

/* One-column table: empty values must round-trip. */
typedef struct {
    char name[8];
} csv_single_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_csv_single_reflect,
                                 csv_single_t,
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(csv_single_t, name));

/* --- rejected shapes --------------------------------------------------- */

typedef struct {
    int32_t items[2];
} csv_bad_array_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_csv_bad_array_reflect,
                                 csv_bad_array_t,
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(csv_bad_array_t,
                                                                  items,
                                                                  shrewd_basic_reflect_int32));

typedef struct {
    const char *borrowed;
} csv_bad_borrowed_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_csv_bad_borrowed_reflect,
                                 csv_bad_borrowed_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(csv_bad_borrowed_t,
                                                                      borrowed));

typedef struct {
    char16_t *wide;
} csv_bad_wide_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_csv_bad_wide_reflect,
                                 csv_bad_wide_t,
                                 SHREWD_BASIC_REFLECT_FIELD_STRING16(csv_bad_wide_t, wide));

typedef struct {
    uint32_t unit;
} csv_bad_bits_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_csv_bad_bits_reflect,
    csv_bad_bits_t,
    SHREWD_BASIC_REFLECT_FIELD_BITS(csv_bad_bits_t, unit, group, 1, 3));

/* --- helpers ----------------------------------------------------------- */

static void row_fill(csv_row_t *row) {
    memset(row, 0, sizeof(*row));
    row->num = -42;
    row->flag = true;
    row->real = 2.5;
    memcpy(row->name, "plain", 6);
    row->owned = "text";
    row->color = CSV_COLOR_GREEN;
    row->origin.x = 3;
    row->origin.y = -4;
}

static void expect_rows_equal(const char *name, const csv_row_t *a, const csv_row_t *b) {
    bool same = false;
    expect_eq_int(
        name, shrewd_basic_reflect_equal(a, b, &g_csv_row_reflect, &same), SHREWD_BASIC_ERR_OK);
    expect_true(name, same);
}

/* --- tests ------------------------------------------------------------- */

static void test_roundtrip(void) {
    csv_row_t rows[2];
    csv_row_t *back = NULL;
    size_t count = 0;
    char *text = NULL;
    row_fill(&rows[0]);
    row_fill(&rows[1]);
    rows[1].num = 7;
    rows[1].owned = "second";

    expect_eq_int("rt encode",
                  shrewd_basic_csv_encode(rows, 2, &g_csv_row_reflect, NULL, &text, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("rt header",
                (NULL != text)
                    && (NULL != strstr(text, "num,flag,real,name,owned,color,origin.x,origin.y")));
    expect_true("rt enum name", (NULL != text) && (NULL != strstr(text, "CSV_COLOR_GREEN")));
    expect_eq_int("rt decode",
                  shrewd_basic_csv_decode(
                      text, strlen(text), (void **)&back, &count, &g_csv_row_reflect, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_csv_text_free(text);
    expect_true("rt count", (NULL != back) && (2 == count));
    if (NULL != back) {
        expect_rows_equal("rt row 0", &rows[0], &back[0]);
        expect_rows_equal("rt row 1", &rows[1], &back[1]);
        expect_eq_int("rt free",
                      shrewd_basic_csv_rows_free(back, count, &g_csv_row_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_quoting(void) {
    csv_row_t rows[3];
    csv_row_t *back = NULL;
    size_t count = 0;
    char *text = NULL;
    row_fill(&rows[0]);
    row_fill(&rows[1]);
    row_fill(&rows[2]);
    memcpy(rows[0].name, "has,comma", 10);
    rows[0].owned = "quote\"inside";
    rows[1].owned = "line1\nline2";
    memcpy(rows[2].name, "  spaced  ", 11);
    rows[2].owned = "a\rb";

    expect_eq_int("quote encode",
                  shrewd_basic_csv_encode(rows, 3, &g_csv_row_reflect, NULL, &text, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("quote comma form", (NULL != text) && (NULL != strstr(text, "\"has,comma\"")));
    expect_true("quote doubled", (NULL != text) && (NULL != strstr(text, "\"quote\"\"inside\"")));
    expect_true("quote newline form", (NULL != text) && (NULL != strstr(text, "\"line1\nline2\"")));
    expect_eq_int("quote decode",
                  shrewd_basic_csv_decode(
                      text, strlen(text), (void **)&back, &count, &g_csv_row_reflect, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_csv_text_free(text);
    expect_true("quote count", (NULL != back) && (3 == count));
    if (NULL != back) {
        expect_rows_equal("quote row 0", &rows[0], &back[0]);
        expect_rows_equal("quote row 1", &rows[1], &back[1]);
        expect_rows_equal("quote row 2", &rows[2], &back[2]);
        expect_eq_int("quote free",
                      shrewd_basic_csv_rows_free(back, count, &g_csv_row_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_header_matching(void) {
    csv_row_t *back = NULL;
    size_t count = 0;
    /* Shuffled columns; absent columns (flag, name, owned, color,
       origin.*) keep zero values. */
    const char *wire = "num,real\r\n9,1.5\r\n";

    expect_eq_int("hdr decode",
                  shrewd_basic_csv_decode(
                      wire, strlen(wire), (void **)&back, &count, &g_csv_row_reflect, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hdr count", (NULL != back) && (1 == count));
    if (NULL != back) {
        expect_true("hdr values",
                    (9 == back[0].num) && (1.5 == back[0].real) && (0 == back[0].flag)
                        && (0 == back[0].name[0]) && (NULL == back[0].owned)
                        && (0 == back[0].origin.x));
        expect_eq_int("hdr free",
                      shrewd_basic_csv_rows_free(back, count, &g_csv_row_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_header_errors(void) {
    csv_row_t *back = NULL;
    size_t count = 0;
    shrewd_basic_csv_error_t error = {0};

    memset(&error, 0, sizeof(error));
    expect_eq_int(
        "hdr unknown col",
        shrewd_basic_csv_decode(
            "num,zzz\n1,2\n", 12, (void **)&back, &count, &g_csv_row_reflect, NULL, &error),
        SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    expect_true("hdr unknown col msg", (1 == error.line) && (NULL != strstr(error.message, "zzz")));
    expect_true("hdr unknown col out", (NULL == back) && (0 == count));

    memset(&error, 0, sizeof(error));
    expect_eq_int(
        "hdr dup col",
        shrewd_basic_csv_decode(
            "num,num\n1,2\n", 12, (void **)&back, &count, &g_csv_row_reflect, NULL, &error),
        SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    expect_true("hdr dup col msg", NULL != strstr(error.message, "duplicate"));

    memset(&error, 0, sizeof(error));
    expect_eq_int(
        "hdr ragged",
        shrewd_basic_csv_decode(
            "num,real\n1\n", 11, (void **)&back, &count, &g_csv_row_reflect, NULL, &error),
        SHREWD_BASIC_ERR_JSON_SYNTAX);
    expect_true("hdr ragged line", 2 == error.line);

    memset(&error, 0, sizeof(error));
    expect_eq_int("hdr bad value",
                  shrewd_basic_csv_decode(
                      "num\nzz\n", 7, (void **)&back, &count, &g_csv_row_reflect, NULL, &error),
                  SHREWD_BASIC_ERR_REFLECT_TEXT);
    expect_true("hdr bad value msg", NULL != strstr(error.message, "num"));

    memset(&error, 0, sizeof(error));
    expect_eq_int(
        "hdr missing",
        shrewd_basic_csv_decode("", 0, (void **)&back, &count, &g_csv_row_reflect, NULL, &error),
        SHREWD_BASIC_ERR_JSON_SYNTAX);
}

static void test_single_column(void) {
    csv_single_t rows[2];
    csv_single_t *back = NULL;
    size_t count = 0;
    char *text = NULL;
    memset(&rows, 0, sizeof(rows));
    rows[0].name[0] = '\0'; /* empty cell must round-trip */
    memcpy(rows[1].name, "ok", 3);
    expect_eq_int("single encode",
                  shrewd_basic_csv_encode(rows, 2, &g_csv_single_reflect, NULL, &text, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("single quoted empty", (NULL != text) && (NULL != strstr(text, "\"\"")));
    expect_eq_int(
        "single decode",
        shrewd_basic_csv_decode(
            text, strlen(text), (void **)&back, &count, &g_csv_single_reflect, NULL, NULL),
        SHREWD_BASIC_ERR_OK);
    shrewd_basic_csv_text_free(text);
    expect_true("single roundtrip",
                (NULL != back) && (2 == count) && ('\0' == back[0].name[0])
                    && (0 == strcmp(back[1].name, "ok")));
    if (NULL != back) {
        expect_eq_int("single free",
                      shrewd_basic_csv_rows_free(back, count, &g_csv_single_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_rejected_shapes(void) {
    char *text = NULL;
    csv_bad_array_t array_row;
    csv_bad_borrowed_t borrowed_row;
    csv_bad_wide_t wide_row;
    csv_bad_bits_t bits_row;
    csv_row_t *back = NULL;
    size_t count = 0;
    memset(&array_row, 0, sizeof(array_row));
    memset(&borrowed_row, 0, sizeof(borrowed_row));
    memset(&wide_row, 0, sizeof(wide_row));
    memset(&bits_row, 0, sizeof(bits_row));

    expect_eq_int(
        "reject array",
        shrewd_basic_csv_encode(&array_row, 1, &g_csv_bad_array_reflect, NULL, &text, NULL),
        SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
    expect_true("reject array out", NULL == text);
    expect_eq_int(
        "reject borrowed",
        shrewd_basic_csv_encode(&borrowed_row, 1, &g_csv_bad_borrowed_reflect, NULL, &text, NULL),
        SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
    expect_eq_int("reject wide",
                  shrewd_basic_csv_encode(&wide_row, 1, &g_csv_bad_wide_reflect, NULL, &text, NULL),
                  SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
    expect_eq_int("reject bits",
                  shrewd_basic_csv_encode(&bits_row, 1, &g_csv_bad_bits_reflect, NULL, &text, NULL),
                  SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
    expect_eq_int("reject decode side",
                  shrewd_basic_csv_decode(
                      "a\n1\n", 4, (void **)&back, &count, &g_csv_bad_array_reflect, NULL, NULL),
                  SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
}

/* Policy rows: KEY rename + aliases, REQUIRED, HIDDEN (merged). */
static const shrewd_basic_json_field_policy_t g_csv_policy_rows[] = {
    SHREWD_BASIC_JSON_KEY(num, "n", "number"),
    SHREWD_BASIC_JSON_REQUIRED(num),
    SHREWD_BASIC_JSON_KEY(name, "label"),
    SHREWD_BASIC_JSON_HIDDEN(flag),
};
static const shrewd_basic_json_info_t g_csv_policy
    = {.type = &g_csv_row_reflect, .fields = g_csv_policy_rows, .field_count = 4};

static void test_policy(void) {
    csv_row_t rows[2];
    csv_row_t *back = NULL;
    size_t count = 0;
    char *text = NULL;
    shrewd_basic_csv_error_t error;
    memset(&error, 0, sizeof(error));
    memset(rows, 0, sizeof(rows));
    rows[0].num = 1;
    rows[0].flag = true;
    rows[0].real = 1.5;
    memcpy(rows[0].name, "a", 2);
    rows[0].color = CSV_COLOR_GREEN;
    rows[1].num = 2;
    memcpy(rows[1].name, "b", 2);
    rows[1].color = CSV_COLOR_RED;

    /* Encode: canonical names (n/label), HIDDEN column absent. */
    expect_eq_int(
        "policy encode",
        shrewd_basic_csv_encode(rows, 2, &g_csv_row_reflect, &g_csv_policy, &text, &error),
        SHREWD_BASIC_ERR_OK);
    expect_true("policy canonical",
                (NULL != strstr(text, "n,")) && (NULL != strstr(text, "label")));
    expect_true("policy hidden out", NULL == strstr(text, "flag"));

    /* Decode: aliases land. */
    expect_eq_int(
        "policy decode alias",
        shrewd_basic_csv_decode(
            text, strlen(text), (void **)&back, &count, &g_csv_row_reflect, &g_csv_policy, &error),
        SHREWD_BASIC_ERR_OK);
    expect_true("policy alias value", (2 == count) && (1 == back[0].num) && (0 == back[0].flag));
    (void)shrewd_basic_csv_rows_free(back, count, &g_csv_row_reflect);
    back = NULL;

    /* Member name in the header is accepted too. */
    {
        const char *doc = "num,label,real,owned,color,origin.x,origin.y\n9,x,0,,1,0,0\n";
        expect_eq_int("policy decode member",
                      shrewd_basic_csv_decode(doc,
                                              strlen(doc),
                                              (void **)&back,
                                              &count,
                                              &g_csv_row_reflect,
                                              &g_csv_policy,
                                              &error),
                      SHREWD_BASIC_ERR_OK);
    }
    expect_true("policy member value", (1 == count) && (9 == back[0].num));
    (void)shrewd_basic_csv_rows_free(back, count, &g_csv_row_reflect);
    back = NULL;

    /* REQUIRED column missing -> TYPE_MISMATCH. */
    {
        const char *doc = "label,real,owned,color,origin.x,origin.y\nx,0,,1,0,0\n";
        expect_eq_int("policy required",
                      shrewd_basic_csv_decode(doc,
                                              strlen(doc),
                                              (void **)&back,
                                              &count,
                                              &g_csv_row_reflect,
                                              &g_csv_policy,
                                              &error),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    }

    /* HIDDEN column in the header is skipped, never decoded. */
    {
        const char *doc = "n,flag,label,real,owned,color,origin.x,origin.y\n5,1,x,0,,1,0,0\n";
        expect_eq_int("policy hidden in",
                      shrewd_basic_csv_decode(doc,
                                              strlen(doc),
                                              (void **)&back,
                                              &count,
                                              &g_csv_row_reflect,
                                              &g_csv_policy,
                                              &error),
                      SHREWD_BASIC_ERR_OK);
    }
    expect_true("policy hidden value", (1 == count) && (0 == back[0].flag));
    (void)shrewd_basic_csv_rows_free(back, count, &g_csv_row_reflect);
    shrewd_basic_csv_text_free(text);
}

int main(void) {
    test_roundtrip();
    test_quoting();
    test_header_matching();
    test_header_errors();
    test_single_column();
    test_rejected_shapes();
    test_policy();
    if (0 != g_failures) {
        fprintf(stderr, "test_csv: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_csv: ok\n");
    return 0;
}

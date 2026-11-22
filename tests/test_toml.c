/**
 * @file test_toml.c
 * @brief Tests for the TOML subset mapping (encode/decode/free).
 *
 * Round-trips a nested config (scalars, strings, arrays, tables),
 * pins the encode text, asserts line-numbered refusals for every
 * rejected construct and checks the ownership contract. Prints
 * failures and exits non-zero.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/toml.h"
#include "shrewd-basic/reflect/reflect.h"

static int g_failures = 0;

static void expect(const char *name, int ok) {
    if (0 == ok) {
        fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

static void expect_eq_int(const char *name, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", name, actual, expected);
        ++g_failures;
    }
}

static void expect_streq(const char *name, const char *actual, const char *expected) {
    if ((NULL == actual) || (0 != strcmp(actual, expected))) {
        fprintf(stderr,
                "FAIL %s: got \"%s\" want \"%s\"\n",
                name,
                (NULL != actual) ? actual : "(null)",
                expected);
        ++g_failures;
    }
}

/* --- fixtures ----------------------------------------------------------- */

typedef enum { MODE_OFF = 0, MODE_ON = 7 } mode_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_mode_reflect,
                                 mode_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_OFF),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_ON));

typedef struct {
    int32_t x;
    double y;
} point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_point_reflect,
                                 point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

typedef struct {
    int32_t count;
    double ratio;
    float weight;
    bool enabled;
    mode_t mode;
    char name[16];
    const char *title; /* owned */
    int32_t scores[3];
    point_t origin;
} config_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_config_reflect,
    config_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(config_t, count),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(config_t, ratio),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(config_t, weight),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(config_t, enabled),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(config_t, mode, g_mode_reflect),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(config_t, name),
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(config_t, title),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(config_t, scores, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(config_t, origin, g_point_reflect));

/* Pointer-shaped fixture: refused loudly by both directions. */
typedef struct {
    point_t *link;
    int32_t tail;
} refuser_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_refuser_reflect,
                                 refuser_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PTR(refuser_t, link, g_point_reflect),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(refuser_t, tail));

/* --- helpers ------------------------------------------------------------- */

static void fill(config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->count = 7;
    cfg->ratio = 0.5;
    cfg->weight = 2.0f;
    cfg->enabled = true;
    cfg->mode = MODE_ON;
    memcpy(cfg->name, "demo", 5);
    cfg->title = "hi \"x\"";
    cfg->scores[0] = 1;
    cfg->scores[1] = 2;
    cfg->scores[2] = 3;
    cfg->origin.x = 3;
    cfg->origin.y = -4.5;
}

/* Decode @p src expecting failure at @p want_line (0 = don't check). */
static void decode_fails(const char *name, const char *src, size_t want_line) {
    config_t obj;
    shrewd_basic_toml_error_t error;
    int status = 0;
    memset(&obj, 0, sizeof(obj));
    memset(&error, 0, sizeof(error));
    status = shrewd_basic_toml_decode(src, strlen(src), &obj, &g_config_reflect, &error);
    if (SHREWD_BASIC_ERR_OK == status) {
        fprintf(stderr, "FAIL %s: unexpectedly accepted\n", name);
        ++g_failures;
        (void)shrewd_basic_toml_free(&obj, &g_config_reflect);
        return;
    }
    if ((0 != want_line) && (error.line != want_line)) {
        fprintf(stderr,
                "FAIL %s: line %zu want %zu (%s)\n",
                name,
                error.line,
                want_line,
                error.message);
        ++g_failures;
    }
    (void)shrewd_basic_toml_free(&obj, &g_config_reflect);
}

static void test_roundtrip(void) {
    config_t src;
    config_t dst;
    bool same = false;
    char *text = NULL;
    shrewd_basic_toml_error_t error;
    memset(&error, 0, sizeof(error));
    fill(&src);

    expect_eq_int("roundtrip encode",
                  shrewd_basic_toml_encode(&src, &g_config_reflect, &text, &error),
                  SHREWD_BASIC_ERR_OK);
    if (NULL == text) {
        return;
    }
    expect_streq("roundtrip text",
                 text,
                 "count = 7\n"
                 "ratio = 0.5\n"
                 "weight = 2.0\n"
                 "enabled = true\n"
                 "mode = \"MODE_ON\"\n"
                 "name = \"demo\"\n"
                 "title = \"hi \\\"x\\\"\"\n"
                 "scores = [1, 2, 3]\n"
                 "\n"
                 "[origin]\n"
                 "x = 3\n"
                 "y = -4.5\n");
    memset(&dst, 0, sizeof(dst));
    expect_eq_int("roundtrip decode",
                  shrewd_basic_toml_decode(text, strlen(text), &dst, &g_config_reflect, &error),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("roundtrip equal",
                  shrewd_basic_reflect_equal(&src, &dst, &g_config_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect("roundtrip same", (0 != same) ? 1 : 0);
    shrewd_basic_toml_text_free(text);
    expect_eq_int(
        "roundtrip free", shrewd_basic_toml_free(&dst, &g_config_reflect), SHREWD_BASIC_ERR_OK);
}

static void test_decode_forms(void) {
    config_t dst;
    shrewd_basic_toml_error_t error;
    static const char *src = "# comment\n"
                             "count = 0x2A\n"
                             "ratio = 1\n"
                             "weight = 1.5e1\n"
                             "enabled = false\n"
                             "mode = \"MODE_OFF\"\n"
                             "name = 'lit\\n'\n"
                             "title = \"a\\tb\\u00e9\"\n"
                             "scores = [1_0, +2,\n"
                             "          3,]\n"
                             "\n"
                             "[origin]\n"
                             "x = -5 # trailing\n"
                             "y = 2.5\n";
    memset(&dst, 0, sizeof(dst));
    memset(&error, 0, sizeof(error));
    expect_eq_int("forms decode",
                  shrewd_basic_toml_decode(src, strlen(src), &dst, &g_config_reflect, &error),
                  SHREWD_BASIC_ERR_OK);
    expect("forms count hex", 42 == dst.count);
    expect("forms ratio int-to-double", 1.0 == dst.ratio);
    expect("forms weight exponent", 15.0f == dst.weight);
    expect("forms disabled", (0 == dst.enabled) ? 1 : 0);
    expect("forms enum name", MODE_OFF == dst.mode);
    expect_streq("forms literal string", dst.name, "lit\\n");
    expect_streq("forms escapes", dst.title, "a\tb\xC3\xA9");
    expect("forms array", (10 == dst.scores[0]) && (2 == dst.scores[1]) && (3 == dst.scores[2]));
    expect("forms table", (-5 == dst.origin.x) && (2.5 == dst.origin.y));
    expect_eq_int(
        "forms free", shrewd_basic_toml_free(&dst, &g_config_reflect), SHREWD_BASIC_ERR_OK);

    /* Numeric enum fallback and empty array. */
    memset(&dst, 0, sizeof(dst));
    expect_eq_int("forms2 decode",
                  shrewd_basic_toml_decode("mode = 7\nscores = []\n",
                                           strlen("mode = 7\nscores = []\n"),
                                           &dst,
                                           &g_config_reflect,
                                           &error),
                  SHREWD_BASIC_ERR_OK);
    expect("forms2 enum number", MODE_ON == dst.mode);
    expect("forms2 empty array", (0 == dst.scores[0]) && (0 == dst.scores[2]));
    expect_eq_int(
        "forms2 free", shrewd_basic_toml_free(&dst, &g_config_reflect), SHREWD_BASIC_ERR_OK);
}

static void test_refusals(void) {
    /* Every rejected construct is loud and line-numbered. */
    decode_fails("array table", "[[origin]]\n", 1);
    decode_fails("inline table", "origin = {x = 1}\n", 1);
    decode_fails("dotted key", "origin.x = 1\n", 1);
    decode_fails("multiline string", "name = \"\"\"x\"\"\"\n", 1);
    decode_fails("datetime", "count = 1979-05-27\n", 1);
    decode_fails("datetime time", "count = 07:32:00\n", 1);
    decode_fails("duplicate key", "count = 1\ncount = 2\n", 2);
    decode_fails("duplicate table", "[origin]\nx = 1\n[origin]\n", 3);
    decode_fails("duplicate dotted table", "[origin]\n[origin.x]\n", 2);
    decode_fails("mixed array", "scores = [1, \"x\"]\n", 1);
    decode_fails("oversize array", "scores = [1, 2, 3, 4]\n", 1);
    decode_fails("unknown key", "bogus = 1\n", 1);
    decode_fails("unknown table", "[bogus]\nx = 1\n", 1);
    decode_fails("bad escape", "name = \"a\\qb\"\n", 1);
    decode_fails("unpaired surrogate", "name = \"\\ud800\"\n", 1);
    decode_fails("quoted key", "\"name\" = \"x\"\n", 1);
    decode_fails("string into number", "count = \"7\"\n", 1);
    decode_fails("number into buffer", "name = 7\n", 1);
    decode_fails("bool into number", "count = true\n", 1);
    decode_fails("array for scalar", "count = [1]\n", 1);
    decode_fails("struct for scalar", "origin = 5\n", 1);
    decode_fails("mixed int float", "scores = [1, 2.5]\n", 1);
    decode_fails("bad underscore", "count = 1__0\n", 1);

    /* Pointer shapes refuse in both directions. */
    {
        refuser_t obj;
        shrewd_basic_toml_error_t error;
        char *text = NULL;
        int status = 0;
        memset(&obj, 0, sizeof(obj));
        memset(&error, 0, sizeof(error));
        status = shrewd_basic_toml_encode(&obj, &g_refuser_reflect, &text, &error);
        expect("refuse encode ptr", SHREWD_BASIC_ERR_OK != status);
        expect("refuse encode ptr text", NULL == text);
        status = shrewd_basic_toml_decode(
            "tail = 1\n", strlen("tail = 1\n"), &obj, &g_refuser_reflect, &error);
        expect_eq_int("refuse decode ok tail", status, SHREWD_BASIC_ERR_OK);
        status = shrewd_basic_toml_decode(
            "link = 1\n", strlen("link = 1\n"), &obj, &g_refuser_reflect, &error);
        expect("refuse decode ptr", SHREWD_BASIC_ERR_OK != status);
    }

    /* NULL owned string has no TOML form (TOML has no null). */
    {
        config_t obj;
        shrewd_basic_toml_error_t error;
        char *text = NULL;
        int status = 0;
        fill(&obj);
        obj.title = NULL;
        memset(&error, 0, sizeof(error));
        status = shrewd_basic_toml_encode(&obj, &g_config_reflect, &text, &error);
        expect("refuse null string", SHREWD_BASIC_ERR_OK != status);
        expect("refuse null string text", NULL == text);
    }
}

int main(void) {
    test_roundtrip();
    test_decode_forms();
    test_refusals();
    if (0 != g_failures) {
        fprintf(stderr, "test_toml: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_toml: ok\n");
    return 0;
}

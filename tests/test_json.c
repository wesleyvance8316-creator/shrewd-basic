/**
 * @file test_json.c
 * @brief Tests for the json module: value model, parser and serializer.
 *
 * Covers the strict RFC 8259 grammar (and its relaxed options), number
 * token fidelity, string escapes and surrogates, the DOM accessors and
 * mutation, and compact/pretty output with text-identical round-trips.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/json/json.h"

static int g_failures = 0;

static void expect_eq_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, actual, expected);
        ++g_failures;
    }
}

static void expect_true(const char *name, int condition)
{
    if (0 == condition) {
        fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

static void expect_streq(const char *name, const char *actual, const char *expected)
{
    if ((NULL == actual) || (NULL == expected) || (0 != strcmp(actual, expected))) {
        fprintf(stderr, "FAIL %s: got \"%s\", want \"%s\"\n", name,
                (NULL != actual) ? actual : "(null)",
                (NULL != expected) ? expected : "(null)");
        ++g_failures;
    }
}

/* Parse a string literal; the result is returned (NULL on failure). */
static shrewd_basic_json_value_t *parse_ok(const char *name, const char *text)
{
    shrewd_basic_json_value_t *value = NULL;
    int status = shrewd_basic_json_parse(text, strlen(text), &value, NULL, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        fprintf(stderr, "FAIL %s: parse returned %d\n", name, status);
        ++g_failures;
        return NULL;
    }
    return value;
}

/* Assert that @p text fails to parse with @p status. */
static void parse_fails(const char *name, const char *text, int status)
{
    shrewd_basic_json_value_t *value = NULL;
    shrewd_basic_json_error_t error;
    int actual = shrewd_basic_json_parse(text, strlen(text), &value, NULL, &error);
    if ((actual != status) || (NULL != value)) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, actual, status);
        ++g_failures;
    }
    shrewd_basic_json_value_free(value);
}

static void test_parse_scalars(void)
{
    shrewd_basic_json_value_t *v = parse_ok("scalar null", "null");
    expect_true("scalar null kind",
                SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(v));
    shrewd_basic_json_value_free(v);

    v = parse_ok("scalar true", " true ");
    bool flag = false;
    expect_eq_int("scalar bool",
                  shrewd_basic_json_to_bool(v, &flag), SHREWD_BASIC_ERR_OK);
    expect_true("scalar bool value", flag);
    shrewd_basic_json_value_free(v);

    v = parse_ok("scalar int", "-42");
    int64_t i64 = 0;
    expect_eq_int("scalar int read",
                  shrewd_basic_json_to_i64(v, &i64), SHREWD_BASIC_ERR_OK);
    expect_true("scalar int value", -42 == i64);
    expect_streq("scalar int raw", shrewd_basic_json_number_text(v), "-42");
    shrewd_basic_json_value_free(v);

    v = parse_ok("scalar uint", "18446744073709551615");
    uint64_t u64 = 0;
    expect_eq_int("scalar uint read",
                  shrewd_basic_json_to_u64(v, &u64), SHREWD_BASIC_ERR_OK);
    expect_true("scalar uint value", UINT64_MAX == u64);
    shrewd_basic_json_value_free(v);

    /* Number tokens keep their exact text. */
    v = parse_ok("scalar double", "1.25e2");
    double d = 0.0;
    expect_eq_int("scalar double read",
                  shrewd_basic_json_to_f64(v, &d), SHREWD_BASIC_ERR_OK);
    expect_true("scalar double value", 125.0 == d);
    expect_streq("scalar double raw", shrewd_basic_json_number_text(v), "1.25e2");
    shrewd_basic_json_value_free(v);

    /* 1.0 reads as the integer 1. */
    v = parse_ok("scalar integral double", "1.0");
    i64 = 0;
    expect_eq_int("scalar 1.0 as i64",
                  shrewd_basic_json_to_i64(v, &i64), SHREWD_BASIC_ERR_OK);
    expect_true("scalar 1.0 value", 1 == i64);
    shrewd_basic_json_value_free(v);

    /* Out-of-range and wrong-type reads fail cleanly. */
    v = parse_ok("scalar range", "1e300");
    expect_eq_int("scalar u64 overflow",
                  shrewd_basic_json_to_u64(v, &u64),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    expect_eq_int("scalar i64 overflow",
                  shrewd_basic_json_to_i64(v, &i64),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    shrewd_basic_json_value_free(v);

    v = parse_ok("scalar negative to u64", "-1");
    expect_eq_int("scalar negative u64",
                  shrewd_basic_json_to_u64(v, &u64),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    shrewd_basic_json_value_free(v);

    v = parse_ok("scalar string", "\"hi\"");
    expect_eq_int("scalar string as bool",
                  shrewd_basic_json_to_bool(v, &flag),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    expect_streq("scalar string read", shrewd_basic_json_to_string(v), "hi");
    shrewd_basic_json_value_free(v);
}

static void test_parse_strings(void)
{
    shrewd_basic_json_value_t *v = parse_ok("string escapes",
                                            "\"a\\\"b\\\\c\\n\\t\\u0041\"");
    expect_streq("string escapes content",
                 shrewd_basic_json_to_string(v), "a\"b\\c\n\tA");
    shrewd_basic_json_value_free(v);

    /* Surrogate pair: U+1F600. */
    v = parse_ok("string surrogate", "\"\\ud83d\\ude00\"");
    expect_streq("string surrogate content",
                 shrewd_basic_json_to_string(v), "\xf0\x9f\x98\x80");
    shrewd_basic_json_value_free(v);

    /* Raw UTF-8 bytes pass through. */
    v = parse_ok("string utf8", "\"\xe4\xb8\xad\xe6\x96\x87\"");
    expect_streq("string utf8 content", shrewd_basic_json_to_string(v),
                 "\xe4\xb8\xad\xe6\x96\x87");
    shrewd_basic_json_value_free(v);

    parse_fails("string lone high surrogate", "\"\\ud800\"",
                SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("string lone low surrogate", "\"\\udc00\"",
                SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("string control char", "\"a\nb\"", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("string bad escape", "\"\\x41\"", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("string bad hex", "\"\\u12g4\"", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("string unterminated", "\"abc", SHREWD_BASIC_ERR_JSON_SYNTAX);
}

static void test_parse_containers(void)
{
    const char *text = "{\"b\": [1, 2], \"a\": {\"x\": true}, \"b\": null}";
    shrewd_basic_json_value_t *v = parse_ok("container", text);
    expect_true("container kind",
                SHREWD_BASIC_JSON_OBJECT == shrewd_basic_json_kind(v));
    /* Duplicates preserved in order; get() resolves to the first. */
    expect_true("container count", (size_t)3 == shrewd_basic_json_count(v));
    expect_streq("container key order", shrewd_basic_json_key_at(v, 0), "b");
    expect_streq("container key order 2", shrewd_basic_json_key_at(v, 1), "a");
    expect_streq("container key order 3", shrewd_basic_json_key_at(v, 2), "b");
    expect_true("container first wins",
                SHREWD_BASIC_JSON_ARRAY
                        == shrewd_basic_json_kind(shrewd_basic_json_get(v, "b")));
    expect_true("container nested",
                SHREWD_BASIC_JSON_ARRAY
                        == shrewd_basic_json_kind(shrewd_basic_json_at(v, 0)));
    {
        shrewd_basic_json_value_t *array = shrewd_basic_json_at(v, 0);
        int64_t item = 0;
        expect_true("container array count",
                    (size_t)2 == shrewd_basic_json_count(array));
        expect_eq_int("container array item",
                      shrewd_basic_json_to_i64(shrewd_basic_json_at(array, 1),
                                               &item),
                      SHREWD_BASIC_ERR_OK);
        expect_true("container array item value", 2 == item);
    }
    expect_true("container at range", NULL == shrewd_basic_json_at(v, 3));
    expect_true("container get missing", NULL == shrewd_basic_json_get(v, "z"));
    shrewd_basic_json_value_free(v);

    v = parse_ok("container empty", "{\"o\": {}, \"a\": []}");
    expect_true("container empty counts",
                (0 == shrewd_basic_json_count(shrewd_basic_json_get(v, "o")))
                && (0 == shrewd_basic_json_count(shrewd_basic_json_get(v, "a"))));
    shrewd_basic_json_value_free(v);
}

static void test_parse_strict(void)
{
    parse_fails("strict trailing comma array", "[1,]", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict trailing comma object", "{\"a\":1,}",
                SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict comment", "/* x */ 1", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict leading zero", "01", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict leading plus", "+1", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict bare dot", ".5", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict trailing dot", "1.", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict empty exponent", "1e", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict bare word", "tru", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict trailing garbage", "1 2", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict bare member", "{a:1}", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict missing colon", "{\"a\" 1}", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict unterminated object", "{\"a\":1",
                SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict empty", "", SHREWD_BASIC_ERR_JSON_SYNTAX);
    parse_fails("strict nan", "NaN", SHREWD_BASIC_ERR_JSON_SYNTAX);
}

static void test_parse_options(void)
{
    shrewd_basic_json_options_t options;
    shrewd_basic_json_value_t *v = NULL;
    memset(&options, 0, sizeof(options));
    options.allow_comments = 1;
    options.allow_trailing_commas = 1;
    expect_eq_int("options relaxed",
                  shrewd_basic_json_parse("/* hi */ [1, // two\n 2,]",
                                          sizeof("/* hi */ [1, // two\n 2,]") - 1,
                                          &v, &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("options relaxed count",
                (NULL != v) && ((size_t)2 == shrewd_basic_json_count(v)));
    shrewd_basic_json_value_free(v);

    /* Depth limit counts value nesting: [[1]] needs depth 4 at the leaf. */
    memset(&options, 0, sizeof(options));
    options.max_depth = 4;
    v = NULL;
    expect_eq_int("options depth ok",
                  shrewd_basic_json_parse("[[[1]]]", 7, &v, &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_json_value_free(v);
    {
        shrewd_basic_json_error_t error;
        v = NULL;
        expect_eq_int("options depth over",
                      shrewd_basic_json_parse("[[[[1]]]]", 9, &v, &options,
                                              &error),
                      SHREWD_BASIC_ERR_JSON_DEPTH);
        expect_true("options depth message", '\0' != error.message[0]);
    }

    /* Default depth (32) is enforced too. */
    {
        char deep[82]; /* 40 x '[', one value, 40 x ']' */
        shrewd_basic_json_error_t error;
        size_t n = 0;
        memset(deep, 0, sizeof(deep));
        for (size_t i = 0; i < 40; ++i) {
            deep[n++] = '[';
        }
        deep[n++] = '1';
        for (size_t i = 0; i < 40; ++i) {
            deep[n++] = ']';
        }
        v = NULL;
        expect_eq_int("default depth over",
                      shrewd_basic_json_parse(deep, n, &v, NULL, &error),
                      SHREWD_BASIC_ERR_JSON_DEPTH);
    }

    /* Error detail points at the fault. */
    {
        shrewd_basic_json_error_t error;
        v = NULL;
        expect_eq_int("error detail status",
                      shrewd_basic_json_parse("[1, 02]", 7, &v, NULL, &error),
                      SHREWD_BASIC_ERR_JSON_SYNTAX);
        expect_true("error detail offset", 5 == error.offset);
    }
}

static void test_value_model(void)
{
    shrewd_basic_json_value_t *object = NULL;
    shrewd_basic_json_value_t *array = NULL;
    shrewd_basic_json_value_t *item = NULL;
    int64_t i64 = 0;

    expect_eq_int("model object",
                  shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &object),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model array",
                  shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &array),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model i64", shrewd_basic_json_new_i64(-7, &item),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model set", shrewd_basic_json_set(object, "n", item),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model set again", shrewd_basic_json_new_i64(8, &item),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model set replace",
                  shrewd_basic_json_set(object, "n", item), SHREWD_BASIC_ERR_OK);
    expect_true("model replace count", (size_t)1 == shrewd_basic_json_count(object));
    expect_eq_int("model get read",
                  shrewd_basic_json_to_i64(shrewd_basic_json_get(object, "n"),
                                           &i64),
                  SHREWD_BASIC_ERR_OK);
    expect_true("model get value", 8 == i64);

    expect_eq_int("model string",
                  shrewd_basic_json_new_string("hi", &item), SHREWD_BASIC_ERR_OK);
    expect_eq_int("model append", shrewd_basic_json_append(array, item),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model append null",
                  shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, &item),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model append 2", shrewd_basic_json_append(array, item),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("model set obj",
                  shrewd_basic_json_set(object, "a", array), SHREWD_BASIC_ERR_OK);

    expect_eq_int("model remove missing",
                  shrewd_basic_json_remove(object, "zz"),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("model remove",
                  shrewd_basic_json_remove(object, "n"), SHREWD_BASIC_ERR_OK);
    expect_true("model remove count", (size_t)1 == shrewd_basic_json_count(object));

    /* Constructors validate their arguments. */
    expect_eq_int("model bad kind",
                  shrewd_basic_json_new(SHREWD_BASIC_JSON_NUMBER, &item),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("model nonfinite",
                  shrewd_basic_json_new_f64(1.0 / 0.0, &item),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    shrewd_basic_json_value_free(object); /* frees the nested array too */
}

static void test_serialize(void)
{
    shrewd_basic_json_value_t *v = NULL;
    char *text = NULL;

    /* Compact golden output, key order preserved. */
    v = parse_ok("serialize src",
                 "{\"b\":[1,2.5,\"x\\ty\"],\"a\":null,\"c\":true}");
    expect_eq_int("serialize compact",
                  shrewd_basic_json_serialize(v, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("serialize compact text", text,
                 "{\"b\":[1,2.5,\"x\\ty\"],\"a\":null,\"c\":true}");
    shrewd_basic_json_text_free(text);

    /* Pretty output. */
    {
        shrewd_basic_json_options_t options;
        memset(&options, 0, sizeof(options));
        options.pretty = 1;
        expect_eq_int("serialize pretty",
                      shrewd_basic_json_serialize(v, &text, &options, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_streq("serialize pretty text", text,
                     "{\n  \"b\": [\n    1,\n    2.5,\n    \"x\\ty\"\n  ],\n"
                     "  \"a\": null,\n  \"c\": true\n}");
        shrewd_basic_json_text_free(text);
    }
    shrewd_basic_json_value_free(v);

    /* Number text is preserved exactly through the DOM. */
    v = parse_ok("serialize number src", "[0.1, -0.0, 1e400, 2]");
    expect_eq_int("serialize number",
                  shrewd_basic_json_serialize(v, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("serialize number text", text, "[0.1,-0.0,1e400,2]");
    shrewd_basic_json_text_free(text);
    shrewd_basic_json_value_free(v);

    /* Created doubles render canonically (shortest round-trip form). */
    expect_eq_int("serialize created f64",
                  shrewd_basic_json_new_f64(0.1, &v), SHREWD_BASIC_ERR_OK);
    expect_eq_int("serialize created f64 text",
                  shrewd_basic_json_serialize(v, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("serialize created f64 value", text, "0.1");
    shrewd_basic_json_text_free(text);
    shrewd_basic_json_value_free(v);
    expect_eq_int("serialize created neg zero",
                  shrewd_basic_json_new_f64(-0.0, &v), SHREWD_BASIC_ERR_OK);
    expect_eq_int("serialize created neg zero text",
                  shrewd_basic_json_serialize(v, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("serialize created neg zero value", text, "-0");
    shrewd_basic_json_text_free(text);
    shrewd_basic_json_value_free(v);

    /* Created u64 keeps full digits. */
    expect_eq_int("serialize created u64",
                  shrewd_basic_json_new_u64(UINT64_MAX, &v), SHREWD_BASIC_ERR_OK);
    expect_eq_int("serialize created u64 text",
                  shrewd_basic_json_serialize(v, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("serialize created u64 value", text, "18446744073709551615");
    shrewd_basic_json_text_free(text);
    shrewd_basic_json_value_free(v);

    /* Strings escape on output; control bytes as \u00XX. */
    expect_eq_int("serialize string build",
                  shrewd_basic_json_new_string_n("a\"b\n\x01\xc3\xa9", 7, &v),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("serialize string text",
                  shrewd_basic_json_serialize(v, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("serialize string escaped", text,
                 "\"a\\\"b\\n\\u0001\xc3\xa9\"");
    shrewd_basic_json_text_free(text);
    shrewd_basic_json_value_free(v);
}

static void test_roundtrip(void)
{
    const char *source =
            "{\"id\":18446744073709551615,\"name\":\"caf\xc3\xa9\",\"pi\":3.14,"
            "\"flags\":[true,false,null],\"nested\":{\"x\":-0.0,\"y\":1e3}}";
    shrewd_basic_json_value_t *v = parse_ok("roundtrip first", source);
    char *text = NULL;
    expect_eq_int("roundtrip serialize",
                  shrewd_basic_json_serialize(v, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("roundtrip text identical", text, source);
    {
        shrewd_basic_json_value_t *again = NULL;
        expect_eq_int("roundtrip reparse",
                      shrewd_basic_json_parse(text, strlen(text), &again, NULL,
                                              NULL),
                      SHREWD_BASIC_ERR_OK);
        char *second = NULL;
        expect_eq_int("roundtrip reserialize",
                      shrewd_basic_json_serialize(again, &second, NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_streq("roundtrip stable", second, text);
        shrewd_basic_json_text_free(second);
        shrewd_basic_json_value_free(again);
    }
    shrewd_basic_json_text_free(text);
    shrewd_basic_json_value_free(v);
}

int main(void)
{
    test_parse_scalars();
    test_parse_strings();
    test_parse_containers();
    test_parse_strict();
    test_parse_options();
    test_value_model();
    test_serialize();
    test_roundtrip();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

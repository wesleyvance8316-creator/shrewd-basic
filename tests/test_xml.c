/**
 * @file test_xml.c
 * @brief Tests for the XML mapping: round trips through the text layer,
 *        policy rows (name/as_attr/REQUIRED/HIDDEN), the strict parser
 *        and the rejection list with line numbers.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>

#include "shrewd-basic/formats/xml.h"

static int g_failures = 0;

static void expect_true(const char *name, int ok) {
    if (0 == ok) {
        fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

static void expect_eq_int(const char *name, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, actual, expected);
        ++g_failures;
    }
}

static void expect_streq(const char *name, const char *actual, const char *expected) {
    if ((NULL == actual) || (NULL == expected) || (0 != strcmp(actual, expected))) {
        fprintf(stderr,
                "FAIL %s: got \"%s\", want \"%s\"\n",
                name,
                (NULL != actual) ? actual : "(null)",
                (NULL != expected) ? expected : "(null)");
        ++g_failures;
    }
}

/* --- fixtures ------------------------------------------------------------- */

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
    int32_t num;
    double real;
    mode_t mode;
    char tag[8];
    const char *name;
    const char16_t *wide;
    char16_t wbuf[8];
    point_t pt;
    int32_t arr[2];
    const point_t *ptr;
} doc_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_doc_reflect,
                                 doc_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(doc_t, num),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(doc_t, real),
                                 SHREWD_BASIC_REFLECT_FIELD_ENUM(doc_t, mode, g_mode_reflect),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(doc_t, tag),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(doc_t, name),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING16(doc_t, wide),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING16_BUFFER(doc_t, wbuf),
                                 SHREWD_BASIC_REFLECT_FIELD_STRUCT(doc_t, pt, g_point_reflect),
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(doc_t,
                                                                  arr,
                                                                  shrewd_basic_reflect_int32),
                                 SHREWD_BASIC_REFLECT_FIELD_PTR(doc_t, ptr, g_point_reflect));

typedef struct {
    int32_t id;
    const char *title;
    int32_t secret;
} pol_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_pol_reflect,
                                 pol_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(pol_t, id),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(pol_t, title),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(pol_t, secret));

static const shrewd_basic_xml_policy_t g_pol_map[] = {
    {"id", "n", 1, 0},
    {"title", NULL, 0, SHREWD_BASIC_XML_REQUIRED},
    {"secret", NULL, 0, SHREWD_BASIC_XML_HIDDEN},
};

static const char16_t g_wide[] = {'c', 'a', 'f', 0x00e9, 0}; /* cafe + e-acute */
static const char16_t g_wbuf[] = {'o', 0x3042, 0};           /* o + hiragana A */

/* --- tests ---------------------------------------------------------------- */

static void test_roundtrip(void) {
    doc_t src;
    doc_t back;
    point_t point = {11, 12.5};
    char *text = NULL;
    bool same = false;
    memset(&src, 0, sizeof(src));
    src.num = -42;
    src.real = 2.5;
    src.mode = MODE_ON;
    memcpy(src.tag, "hi", 3);
    src.name = "alice";
    src.wide = g_wide;
    memcpy(src.wbuf, g_wbuf, sizeof(g_wbuf));
    src.pt.x = 3;
    src.pt.y = 4.5;
    src.arr[0] = 7;
    src.arr[1] = 8;
    src.ptr = &point;
    expect_eq_int("encode",
                  shrewd_basic_xml_encode(&src, &g_doc_reflect, NULL, 0, &text, NULL),
                  SHREWD_BASIC_ERR_OK);
    memset(&back, 0, sizeof(back));
    expect_eq_int("decode",
                  shrewd_basic_xml_decode(text, strlen(text), &back, &g_doc_reflect, NULL, 0, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("equal",
                  shrewd_basic_reflect_equal(&src, &back, &g_doc_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal value", same);
    shrewd_basic_xml_text_free(text);
    text = NULL;
    expect_eq_int("free", shrewd_basic_xml_free(&back, &g_doc_reflect), SHREWD_BASIC_ERR_OK);

    /* nil pointer is omitted and stays NULL */
    src.ptr = NULL;
    expect_eq_int("encode nil",
                  shrewd_basic_xml_encode(&src, &g_doc_reflect, NULL, 0, &text, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("nil omitted", NULL == strstr(text, "<ptr>"));
    memset(&back, 0, sizeof(back));
    expect_eq_int("decode nil",
                  shrewd_basic_xml_decode(text, strlen(text), &back, &g_doc_reflect, NULL, 0, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("nil pointer", NULL == back.ptr);
    shrewd_basic_xml_text_free(text);
    (void)shrewd_basic_xml_free(&back, &g_doc_reflect);
}

static void test_policy(void) {
    pol_t src;
    pol_t back;
    char *text = NULL;
    shrewd_basic_xml_error_t error;
    memset(&src, 0, sizeof(src));
    memset(&error, 0, sizeof(error));
    src.id = 7;
    src.title = "hi";
    src.secret = 0x5ec;
    expect_eq_int("policy encode",
                  shrewd_basic_xml_encode(&src, &g_pol_reflect, g_pol_map, 3, &text, &error),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("policy golden", text, "<pol_t n=\"7\"><title>hi</title></pol_t>");
    memset(&back, 0, sizeof(back));
    expect_eq_int(
        "policy decode",
        shrewd_basic_xml_decode(text, strlen(text), &back, &g_pol_reflect, g_pol_map, 3, &error),
        SHREWD_BASIC_ERR_OK);
    expect_true("policy roundtrip",
                (7 == back.id) && (NULL != back.title) && (0 == strcmp(back.title, "hi")));
    /* HIDDEN keeps zero on decode (never decoded) */
    expect_true("hidden zero", 0 == back.secret);
    shrewd_basic_xml_text_free(text);
    (void)shrewd_basic_xml_free(&back, &g_pol_reflect);

    /* REQUIRED: missing title fails with a shape error */
    expect_true("required missing",
                SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH
                    == shrewd_basic_xml_decode("<pol_t n=\"7\"/>",
                                               strlen("<pol_t n=\"7\"/>"),
                                               &back,
                                               &g_pol_reflect,
                                               g_pol_map,
                                               3,
                                               &error));

    /* HIDDEN is never decoded even when present in the document */
    memset(&back, 0, sizeof(back));
    expect_eq_int(
        "hidden decode",
        shrewd_basic_xml_decode("<pol_t n=\"1\"><title>t</title><secret>5</secret></pol_t>",
                                strlen("<pol_t n=\"1\"><title>t</title><secret>5</secret></pol_t>"),
                                &back,
                                &g_pol_reflect,
                                g_pol_map,
                                3,
                                &error),
        SHREWD_BASIC_ERR_OK);
    expect_true("hidden stays zero", 0 == back.secret);
    (void)shrewd_basic_xml_free(&back, &g_pol_reflect);
}

static void test_parser(void) {
    pol_t back;
    shrewd_basic_xml_error_t error;
    static const char *prolog = "<?xml version=\"1.0\"?>\n"
                                "<!-- lead comment -->\n"
                                "<pol_t n=\"&#x37;\">\n"
                                "  <title><![CDATA[a<b&c>]]>&amp;&lt;&gt;&quot;&apos;</title>\n"
                                "</pol_t>";
    memset(&back, 0, sizeof(back));
    memset(&error, 0, sizeof(error));
    expect_eq_int("prolog parse",
                  shrewd_basic_xml_decode(
                      prolog, strlen(prolog), &back, &g_pol_reflect, g_pol_map, 3, &error),
                  SHREWD_BASIC_ERR_OK);
    expect_true("prolog values",
                (7 == back.id) && (NULL != back.title)
                    && (0 == strcmp(back.title, "a<b&c>&<>\"'")));
    (void)shrewd_basic_xml_free(&back, &g_pol_reflect);
}

static void expect_reject(const char *name, const char *doc, size_t want_line) {
    pol_t back;
    shrewd_basic_xml_error_t error;
    int status = 0;
    memset(&back, 0, sizeof(back));
    memset(&error, 0, sizeof(error));
    status = shrewd_basic_xml_decode(doc, strlen(doc), &back, &g_pol_reflect, g_pol_map, 3, &error);
    expect_true(name, SHREWD_BASIC_ERR_OK != status);
    if ((0 != want_line) && (want_line != error.line)) {
        fprintf(stderr, "FAIL %s line: got %zu, want %zu\n", name, error.line, want_line);
        ++g_failures;
    }
    (void)shrewd_basic_xml_free(&back, &g_pol_reflect);
}

static void test_rejects(void) {
    expect_reject("doctype", "<!DOCTYPE x>\n<pol_t/>", 1);
    expect_reject("entity decl", "<!ENTITY x \"y\">\n<pol_t/>", 1);
    expect_reject("foreign entity", "<pol_t n=\"1\"><title>&foo;</title></pol_t>", 1);
    expect_reject("trailing", "<pol_t/>junk", 1);
    expect_reject("bad close", "<pol_t></pol_x>", 1);
    expect_reject("duplicate attr", "<pol_t n=\"1\" n=\"2\"/>", 1);
    expect_reject("processing instruction", "<?pi x?><pol_t/>", 1);
    expect_reject("unterminated", "<pol_t><title>a</title>", 1);
    expect_reject("bad ref", "<pol_t n=\"&#x110000;\"/>", 1);
}

int main(void) {
    test_roundtrip();
    test_policy();
    test_parser();
    test_rejects();
    if (0 != g_failures) {
        fprintf(stderr, "test_xml: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_xml: ok\n");
    return 0;
}

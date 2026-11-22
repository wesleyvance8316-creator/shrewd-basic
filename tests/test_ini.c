/**
 * @file test_ini.c
 * @brief Tests for the INI mapping (sections/keys through the shared
 *        text layer). One summary line, non-zero exit on failure.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/ini.h"
#include "shrewd-basic/formats/json.h"
#include "shrewd-basic/platform/memory.h"

static int g_failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("test_ini: FAILED %s\n", name);
        ++g_failures;
    }
}

typedef struct {
    int32_t x;
    double y;
} ini_point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_ini_point_reflect,
                                 ini_point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(ini_point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(ini_point_t, y));

typedef enum { INI_LOW = 0, INI_HIGH = 3 } ini_level_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_ini_level_reflect,
                                 ini_level_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(INI_LOW),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(INI_HIGH));

typedef struct {
    const char *name; /* owned */
    ini_level_t level;
    bool verbose;
    ini_point_t origin;
    char tag[8];
} ini_cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_ini_cfg_reflect,
    ini_cfg_t,
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(ini_cfg_t, name),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(ini_cfg_t, level, g_ini_level_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(ini_cfg_t, verbose),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(ini_cfg_t, origin, g_ini_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(ini_cfg_t, tag));

static const char *g_doc = "; comment\n"
                           "name = demo\n"
                           "level = INI_HIGH\n"
                           "verbose = true\n"
                           "tag = hello\n"
                           "\n"
                           "# section comment\n"
                           "[origin]\n"
                           "x = -7\n"
                           "y = 2.5\n";

/* Array fixture: fixed array + dynamic array (count-linked). */
typedef struct {
    int32_t vals[3];
    int32_t *dyn;
    size_t dyn_count;
} ini_arr_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    g_ini_arr_reflect,
    ini_arr_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(ini_arr_t, vals, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_DARRAY(ini_arr_t, dyn, dyn_count, shrewd_basic_reflect_int32));

/* Policy rows: KEY rename + alias, REQUIRED, HIDDEN (merged). */
static const shrewd_basic_json_field_policy_t g_ini_policy_rows[] = {
    SHREWD_BASIC_JSON_KEY(name, "title", "alias"),
    SHREWD_BASIC_JSON_REQUIRED(verbose),
    SHREWD_BASIC_JSON_HIDDEN(tag),
};
static const shrewd_basic_json_info_t g_ini_policy
    = {.type = &g_ini_cfg_reflect, .fields = g_ini_policy_rows, .field_count = 3};

int main(void) {
    ini_cfg_t cfg;
    ini_cfg_t twin;
    ini_cfg_t *heap = NULL;
    shrewd_basic_ini_error_t error;
    shrewd_basic_ini_options_t options;
    char *text = NULL;
    memset(&cfg, 0, sizeof(cfg));
    memset(&error, 0, sizeof(error));
    memset(&options, 0, sizeof(options));

    /* Decode: root leaves, dotted section, enum by name, owned string. */
    expect("decode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_ini_decode(
                   g_doc, strlen(g_doc), &cfg, &g_ini_cfg_reflect, NULL, NULL, &error));
    expect("decode name", (NULL != cfg.name) && (0 == strcmp(cfg.name, "demo")));
    expect("decode level", INI_HIGH == cfg.level);
    expect("decode verbose", cfg.verbose);
    expect("decode buffer", 0 == strcmp(cfg.tag, "hello"));
    expect("decode section", (-7 == cfg.origin.x) && (2.5 == cfg.origin.y));

    /* Encode back: section header + keys; round trip equal. */
    expect("encode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_ini_encode(&cfg, &g_ini_cfg_reflect, NULL, &text, NULL, &error));
    expect("encode shape",
           (NULL != text) && (NULL != strstr(text, "[origin]"))
               && (NULL != strstr(text, "name = demo")));
    memset(&twin, 0, sizeof(twin));
    expect("redecode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_ini_decode(
                   text, strlen(text), &twin, &g_ini_cfg_reflect, NULL, NULL, &error));
    expect("roundtrip",
           (NULL != twin.name) && (0 == strcmp(twin.name, cfg.name)) && (twin.level == cfg.level)
               && (twin.origin.x == cfg.origin.x) && (twin.origin.y == cfg.origin.y));
    shrewd_basic_ini_text_free(text);
    text = NULL;

    /* Unknown keys: skipped by default, error under strict. */
    {
        ini_cfg_t extra;
        memset(&extra, 0, sizeof(extra));
        expect("unknown skip",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_decode(
                       "zz = 1\nname = a\n", 13, &extra, &g_ini_cfg_reflect, NULL, NULL, &error));
        shrewd_basic_ini_free(&extra, &g_ini_cfg_reflect);
        options.strict = 1;
        memset(&extra, 0, sizeof(extra));
        expect("unknown strict",
               SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH
                   == shrewd_basic_ini_decode(
                       "zz = 1\n", 7, &extra, &g_ini_cfg_reflect, NULL, &options, &error));
        expect("strict line", 1 == error.line);
    }

    /* Syntax and shape refusals carry lines. */
    memset(&twin, 0, sizeof(twin));
    expect("bad line",
           SHREWD_BASIC_ERR_JSON_SYNTAX
               == shrewd_basic_ini_decode(
                   "no equals here\n", 15, &twin, &g_ini_cfg_reflect, NULL, NULL, &error));
    expect("bad line number", 1 == error.line);
    expect("bad section",
           SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH
               == shrewd_basic_ini_decode(
                   "[nope]\nx = 1\n", 13, &twin, &g_ini_cfg_reflect, NULL, NULL, &error));
    expect("bad value",
           SHREWD_BASIC_ERR_REFLECT_TEXT
               == shrewd_basic_ini_decode(
                   "level = nope\n", 13, &twin, &g_ini_cfg_reflect, NULL, NULL, &error));

    /* Ownership: heap root + env-style free of contents. */
    expect("heap alloc",
           SHREWD_BASIC_ERR_OK == shrewd_basic_platform_calloc((void **)&heap, 1, sizeof(*heap)));
    expect("heap decode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_ini_decode(
                   "name = x\n", 9, heap, &g_ini_cfg_reflect, NULL, NULL, &error));
    expect("heap owned", (NULL != heap->name) && (0 == strcmp(heap->name, "x")));
    expect("free", SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(heap, &g_ini_cfg_reflect));
    (void)shrewd_basic_platform_free((void **)&heap);

    expect("free root 1", SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&cfg, &g_ini_cfg_reflect));
    expect("free root 2", SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&twin, &g_ini_cfg_reflect));

    /* Policy: canonical key + aliases, REQUIRED, HIDDEN. */
    {
        ini_cfg_t pol;
        shrewd_basic_ini_error_t perr;
        char *ptext = NULL;
        memset(&pol, 0, sizeof(pol));
        memset(&perr, 0, sizeof(perr));
        pol.name = "n";
        pol.level = INI_LOW;
        pol.verbose = true;
        memcpy(pol.tag, "v1", 3);
        expect("policy encode",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_encode(
                       &pol, &g_ini_cfg_reflect, &g_ini_policy, &ptext, NULL, &perr));
        expect("policy canonical key", NULL != strstr(ptext, "title = n"));
        expect("policy hidden out", NULL == strstr(ptext, "tag"));
        shrewd_basic_ini_text_free(ptext);
        memset(&pol, 0, sizeof(pol));
        expect("policy decode alias",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_decode("title = a\nverbose = true\n",
                                              strlen("title = a\nverbose = true\n"),
                                              &pol,
                                              &g_ini_cfg_reflect,
                                              &g_ini_policy,
                                              NULL,
                                              &perr));
        expect("policy alias value", (NULL != pol.name) && (0 == strcmp(pol.name, "a")));
        expect("policy free",
               SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&pol, &g_ini_cfg_reflect));
        memset(&pol, 0, sizeof(pol));
        expect("policy decode member",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_decode("name = b\nverbose = true\n",
                                              strlen("name = b\nverbose = true\n"),
                                              &pol,
                                              &g_ini_cfg_reflect,
                                              &g_ini_policy,
                                              NULL,
                                              &perr));
        expect("policy free 2",
               SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&pol, &g_ini_cfg_reflect));
        memset(&pol, 0, sizeof(pol));
        expect("policy required",
               SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH
                   == shrewd_basic_ini_decode("name = c\n",
                                              strlen("name = c\n"),
                                              &pol,
                                              &g_ini_cfg_reflect,
                                              &g_ini_policy,
                                              NULL,
                                              &perr));
        expect("policy hidden in",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_decode("name = d\nverbose = true\ntag = z\n",
                                              strlen("name = d\nverbose = true\ntag = z\n"),
                                              &pol,
                                              &g_ini_cfg_reflect,
                                              &g_ini_policy,
                                              NULL,
                                              &perr));
        expect("policy hidden value", 0 == pol.tag[0]);
        expect("policy free 3",
               SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&pol, &g_ini_cfg_reflect));
    }

    /* Arrays: repeated keys append (fixed fill, overflow, dynamic). */
    {
        ini_arr_t arr;
        ini_arr_t back;
        shrewd_basic_ini_error_t aerr;
        char *atext = NULL;
        memset(&arr, 0, sizeof(arr));
        memset(&back, 0, sizeof(back));
        memset(&aerr, 0, sizeof(aerr));
        expect("array decode",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_decode("vals = 1\nvals = 2\nvals = 3\n",
                                              strlen("vals = 1\nvals = 2\nvals = 3\n"),
                                              &arr,
                                              &g_ini_arr_reflect,
                                              NULL,
                                              NULL,
                                              &aerr));
        expect("array fill", (1 == arr.vals[0]) && (2 == arr.vals[1]) && (3 == arr.vals[2]));
        expect("array encode",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_encode(&arr, &g_ini_arr_reflect, NULL, &atext, NULL, &aerr));
        {
            int hits = 0;
            const char *scan = atext;
            while (NULL != (scan = strstr(scan, "vals = "))) {
                hits += 1;
                scan += 7;
            }
            expect("array repeated keys", 3 == hits);
        }
        shrewd_basic_ini_text_free(atext);
        memset(&arr, 0, sizeof(arr));
        expect("array overflow",
               SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH
                   == shrewd_basic_ini_decode("vals = 1\nvals = 2\nvals = 3\nvals = 4\n",
                                              strlen("vals = 1\nvals = 2\nvals = 3\nvals = 4\n"),
                                              &arr,
                                              &g_ini_arr_reflect,
                                              NULL,
                                              NULL,
                                              &aerr));
        memset(&arr, 0, sizeof(arr));
        expect("darray decode",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_ini_decode("dyn = 7\ndyn = 8\ndyn = 9\n",
                                              strlen("dyn = 7\ndyn = 8\ndyn = 9\n"),
                                              &arr,
                                              &g_ini_arr_reflect,
                                              NULL,
                                              NULL,
                                              &aerr));
        expect("darray grow",
               (3 == arr.dyn_count) && (7 == arr.dyn[0]) && (8 == arr.dyn[1]) && (9 == arr.dyn[2]));
        expect("darray free",
               SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&arr, &g_ini_arr_reflect));
    }

    if (0 != g_failures) {
        printf("test_ini: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_ini: ok\n");
    return 0;
}

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
                   g_doc, strlen(g_doc), &cfg, &g_ini_cfg_reflect, NULL, &error));
    expect("decode name", (NULL != cfg.name) && (0 == strcmp(cfg.name, "demo")));
    expect("decode level", INI_HIGH == cfg.level);
    expect("decode verbose", cfg.verbose);
    expect("decode buffer", 0 == strcmp(cfg.tag, "hello"));
    expect("decode section", (-7 == cfg.origin.x) && (2.5 == cfg.origin.y));

    /* Encode back: section header + keys; round trip equal. */
    expect("encode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_ini_encode(&cfg, &g_ini_cfg_reflect, &text, NULL, &error));
    expect("encode shape",
           (NULL != text) && (NULL != strstr(text, "[origin]"))
               && (NULL != strstr(text, "name = demo")));
    memset(&twin, 0, sizeof(twin));
    expect("redecode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_ini_decode(
                   text, strlen(text), &twin, &g_ini_cfg_reflect, NULL, &error));
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
                       "zz = 1\nname = a\n", 13, &extra, &g_ini_cfg_reflect, NULL, &error));
        shrewd_basic_ini_free(&extra, &g_ini_cfg_reflect);
        options.strict = 1;
        memset(&extra, 0, sizeof(extra));
        expect("unknown strict",
               SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH
                   == shrewd_basic_ini_decode(
                       "zz = 1\n", 7, &extra, &g_ini_cfg_reflect, &options, &error));
        expect("strict line", 1 == error.line);
    }

    /* Syntax and shape refusals carry lines. */
    memset(&twin, 0, sizeof(twin));
    expect("bad line",
           SHREWD_BASIC_ERR_JSON_SYNTAX
               == shrewd_basic_ini_decode(
                   "no equals here\n", 15, &twin, &g_ini_cfg_reflect, NULL, &error));
    expect("bad line number", 1 == error.line);
    expect("bad section",
           SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH
               == shrewd_basic_ini_decode(
                   "[nope]\nx = 1\n", 13, &twin, &g_ini_cfg_reflect, NULL, &error));
    expect("bad value",
           SHREWD_BASIC_ERR_REFLECT_TEXT
               == shrewd_basic_ini_decode(
                   "level = nope\n", 13, &twin, &g_ini_cfg_reflect, NULL, &error));

    /* Ownership: heap root + env-style free of contents. */
    expect("heap alloc",
           SHREWD_BASIC_ERR_OK == shrewd_basic_platform_calloc((void **)&heap, 1, sizeof(*heap)));
    expect("heap decode",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_ini_decode("name = x\n", 9, heap, &g_ini_cfg_reflect, NULL, &error));
    expect("heap owned", (NULL != heap->name) && (0 == strcmp(heap->name, "x")));
    expect("free", SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(heap, &g_ini_cfg_reflect));
    (void)shrewd_basic_platform_free((void **)&heap);

    expect("free root 1", SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&cfg, &g_ini_cfg_reflect));
    expect("free root 2", SHREWD_BASIC_ERR_OK == shrewd_basic_ini_free(&twin, &g_ini_cfg_reflect));

    if (0 != g_failures) {
        printf("test_ini: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_ini: ok\n");
    return 0;
}

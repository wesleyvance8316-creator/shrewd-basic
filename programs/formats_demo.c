#define _POSIX_C_SOURCE 200809L
/**
 * @file formats_demo.c
 * @brief Every interface of the shared text layer and the six format
 *        modules (INI, env/args, CSV, TOML, msgpack, CBOR): leaf
 *        conversion, encode/decode round trips and the free contracts.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/bson.h"
#include "shrewd-basic/formats/cbor.h"
#include "shrewd-basic/formats/csv.h"
#include "shrewd-basic/formats/env.h"
#include "shrewd-basic/formats/ini.h"
#include "shrewd-basic/formats/msgpack.h"
#include "shrewd-basic/formats/toml.h"
#include "shrewd-basic/formats/xml.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

typedef enum { DEMO_LOW = 0, DEMO_HIGH = 3 } demo_level_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(demo_level_reflect,
                                 demo_level_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(DEMO_LOW),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(DEMO_HIGH));

typedef struct {
    int32_t x;
    double y;
} demo_point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(demo_point_reflect,
                                 demo_point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(demo_point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(demo_point_t, y));

typedef struct {
    const char *name; /* owned */
    demo_level_t level;
    bool verbose;
    demo_point_t origin;
    char tag[8];
} demo_cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    demo_cfg_reflect,
    demo_cfg_t,
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(demo_cfg_t, name),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(demo_cfg_t, level, demo_level_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(demo_cfg_t, verbose),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(demo_cfg_t, origin, demo_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(demo_cfg_t, tag));

/* --- text layer ----------------------------------------------------------- */

static void demo_text_layer(void) {
    const shrewd_basic_reflect_field_t *field = demo_cfg_reflect.sb_refl_fields + 1; /* level */
    demo_level_t value = DEMO_HIGH;
    char buf[32];
    size_t len = 0;
    demo_level_t back = DEMO_LOW;
    shrewd_basic_reflect_render_text(field, &value, buf, sizeof(buf), &len);
    printf("text: render -> \"%s\" (%zu bytes)\n", buf, len);
    shrewd_basic_reflect_render_text(field, &value, NULL, 0, &len); /* measure */
    shrewd_basic_reflect_scan_text(field, &back, buf, strlen(buf));
    printf("text: scan -> %s\n", (back == value) ? "round trip ok" : "FAILED");
}

/* --- INI ------------------------------------------------------------------ */

static void demo_ini(void) {
    static const char *doc = "; demo\nname = alice\nlevel = DEMO_HIGH\n"
                             "verbose = true\ntag = v1\n[origin]\nx = -7\ny = 2.5\n";
    demo_cfg_t cfg;
    demo_cfg_t twin;
    shrewd_basic_ini_error_t error;
    char *text = NULL;
    memset(&cfg, 0, sizeof(cfg));
    memset(&twin, 0, sizeof(twin));
    memset(&error, 0, sizeof(error));
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_ini_decode(doc, strlen(doc), &cfg, &demo_cfg_reflect, NULL, NULL, &error)) {
        printf("ini: decode FAILED (line %zu: %s)\n", error.line, error.message);
        return;
    }
    shrewd_basic_ini_encode(&cfg, &demo_cfg_reflect, NULL, &text, NULL, &error);
    shrewd_basic_ini_decode(text, strlen(text), &twin, &demo_cfg_reflect, NULL, NULL, &error);
    printf("ini: decode/encode round trip -> %s\n",
           ((NULL != twin.name) && (0 == strcmp(twin.name, "alice"))) ? "ok" : "FAILED");
    shrewd_basic_ini_text_free(text);
    shrewd_basic_ini_free(&cfg, &demo_cfg_reflect);
    shrewd_basic_ini_free(&twin, &demo_cfg_reflect);
}

/* --- env / args ----------------------------------------------------------- */

static void demo_env_args(int argc, char **argv) {
    demo_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    (void)setenv("APP_NAME", "from-env", 1);
    (void)setenv("APP_LEVEL", "DEMO_LOW", 1);
    (void)setenv("APP_VERBOSE", "true", 1);
    shrewd_basic_env_scan(&cfg, &demo_cfg_reflect, "APP_");
    printf("env: scan -> name=%s level=%d\n",
           (NULL != cfg.name) ? cfg.name : "(null)",
           (int)cfg.level);
    shrewd_basic_args_scan(&cfg, &demo_cfg_reflect, "APP_", argc, argv);
    printf("args: scan (NAME=VALUE / --NAME=VALUE, first match wins)\n");
    shrewd_basic_env_free(&cfg, &demo_cfg_reflect);
}

/* --- CSV ------------------------------------------------------------------ */

static void demo_csv(void) {
    demo_point_t rows[2];
    demo_point_t *back = NULL;
    size_t count = 0;
    char *text = NULL;
    memset(rows, 0, sizeof(rows));
    rows[0].x = 1;
    rows[0].y = 2.5;
    rows[1].x = -3;
    rows[1].y = 4.25;
    shrewd_basic_csv_encode(rows, 2, &demo_point_reflect, NULL, &text, NULL);
    printf("csv: encode ->\n%s", text);
    shrewd_basic_csv_decode(
        text, strlen(text), (void **)&back, &count, &demo_point_reflect, NULL, NULL);
    printf("csv: decode -> %zu rows, first x=%d\n", count, (int)((NULL != back) ? back[0].x : 0));
    shrewd_basic_csv_text_free(text);
    shrewd_basic_csv_rows_free(back, count, &demo_point_reflect);
}

/* --- TOML ----------------------------------------------------------------- */

static void demo_toml(void) {
    static const char *doc = "# demo\nname = \"alice\"\nlevel = \"DEMO_HIGH\"\n"
                             "[origin]\nx = -7\ny = 2.5\n";
    demo_cfg_t cfg;
    shrewd_basic_toml_error_t error;
    char *text = NULL;
    memset(&cfg, 0, sizeof(cfg));
    memset(&error, 0, sizeof(error));
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_toml_decode(doc, strlen(doc), &cfg, &demo_cfg_reflect, &error)) {
        printf("toml: decode FAILED (line %zu: %s)\n", error.line, error.message);
        return;
    }
    shrewd_basic_toml_encode(&cfg, &demo_cfg_reflect, &text, &error);
    printf("toml: round trip -> %s\n",
           (NULL != strstr(text, "name = \"alice\"")) ? "ok" : "FAILED");
    shrewd_basic_toml_text_free(text);
    shrewd_basic_toml_free(&cfg, &demo_cfg_reflect);
}

/* --- XML ------------------------------------------------------------------ */

static void demo_xml(void) {
    static const shrewd_basic_xml_policy_t map[] = {
        {"name", "id", 1, 0}, /* as attribute */
    };
    demo_cfg_t cfg;
    demo_cfg_t twin;
    shrewd_basic_xml_error_t error;
    char *text = NULL;
    bool same = false;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "alice"; /* borrowed literal - never freed */
    cfg.level = DEMO_HIGH;
    cfg.origin.x = -7;
    cfg.origin.y = 2.5;
    memcpy(cfg.tag, "v1", 3);
    shrewd_basic_xml_encode(&cfg, &demo_cfg_reflect, map, 1, &text, &error);
    memset(&twin, 0, sizeof(twin));
    shrewd_basic_xml_decode(text, strlen(text), &twin, &demo_cfg_reflect, map, 1, &error);
    shrewd_basic_reflect_equal(&cfg, &twin, &demo_cfg_reflect, &same);
    printf("xml: %s\n", same ? "round trip ok" : "FAILED");
    shrewd_basic_xml_text_free(text);
    shrewd_basic_xml_free(&twin, &demo_cfg_reflect);
}

/* --- msgpack / CBOR ------------------------------------------------------- */

static void demo_binary(
    const char *label,
    int (*encode)(const void *, const shrewd_basic_reflect_type_t *, void **, size_t *),
    int (*decode)(const void *, size_t, void *, const shrewd_basic_reflect_type_t *),
    void (*bytes_free)(void *),
    int (*free_fn)(void *, const shrewd_basic_reflect_type_t *)) {
    demo_cfg_t src;
    demo_cfg_t back;
    void *bytes = NULL;
    size_t size = 0;
    bool same = false;
    memset(&src, 0, sizeof(src));
    src.name = "alice";
    src.level = DEMO_HIGH;
    src.verbose = true;
    src.origin.x = -7;
    src.origin.y = 2.5;
    memcpy(src.tag, "v1", 3);
    encode(&src, &demo_cfg_reflect, &bytes, &size);
    memset(&back, 0, sizeof(back));
    decode(bytes, size, &back, &demo_cfg_reflect);
    shrewd_basic_reflect_equal(&src, &back, &demo_cfg_reflect, &same);
    printf("%s: %zu-byte round trip -> %s\n", label, size, same ? "equal" : "FAILED");
    bytes_free(bytes);
    free_fn(&back, &demo_cfg_reflect);
}

int main(int argc, char **argv) {
    printf("== shrewd-basic formats demo ==\n");
    demo_text_layer();
    demo_ini();
    demo_env_args(argc, argv);
    demo_csv();
    demo_toml();
    demo_xml();
    demo_binary("msgpack",
                shrewd_basic_msgpack_encode,
                shrewd_basic_msgpack_decode,
                shrewd_basic_msgpack_bytes_free,
                shrewd_basic_msgpack_free);
    demo_binary("cbor",
                shrewd_basic_cbor_encode,
                shrewd_basic_cbor_decode,
                shrewd_basic_cbor_bytes_free,
                shrewd_basic_cbor_free);
    demo_binary("bson",
                shrewd_basic_bson_encode,
                shrewd_basic_bson_decode,
                shrewd_basic_bson_bytes_free,
                shrewd_basic_bson_free);
    printf("formats demo: ok\n");
    return 0;
}

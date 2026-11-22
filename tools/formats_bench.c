/**
 * @file formats_bench.c
 * @brief Micro-benchmark for the shared text layer and the six format
 *        modules (INI, CSV, TOML, msgpack, CBOR - plus env-free leaf
 *        conversion through render_text/scan_text).
 *
 * Measures per-op latency and text/binary throughput on representative
 * payloads. The numbers are a baseline for optimization work.
 *
 * Usage: tools/formats_bench.sh   (built by tools/formats_bench.sh)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shrewd-basic/formats/bson.h"
#include "shrewd-basic/formats/cbor.h"
#include "shrewd-basic/formats/csv.h"
#include "shrewd-basic/formats/ini.h"
#include "shrewd-basic/formats/msgpack.h"
#include "shrewd-basic/formats/toml.h"
#include "shrewd-basic/formats/xml.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/reflect/reflect.h"

typedef enum { LEVEL_LOW = 0, LEVEL_MID = 2, LEVEL_HIGH = 5 } level_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(level_reflect,
                                 level_t,
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(LEVEL_LOW),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(LEVEL_MID),
                                 SHREWD_BASIC_REFLECT_ENUM_VALUE(LEVEL_HIGH));

/* A representative ~20-field record for the mapping paths. */
typedef struct {
    int64_t big;
    double ratio;
    level_t level;
    int32_t i1;
    int32_t i2;
    int32_t i3;
    int32_t i4;
    double d1;
    double d2;
    double d3;
    const char *s1;
    const char *s2;
    const char *s3;
    bool b1;
    bool b2;
    char tag[12];
    uint32_t u1;
    uint32_t u2;
    int32_t i5;
    double d4;
} cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(cfg_reflect,
                                 cfg_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, big),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, ratio),
                                 SHREWD_BASIC_REFLECT_FIELD_ENUM(cfg_t, level, level_reflect),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, i1),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, i2),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, i3),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, i4),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, d1),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, d2),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, d3),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(cfg_t, s1),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(cfg_t, s2),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(cfg_t, s3),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, b1),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, b2),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(cfg_t, tag),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, u1),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, u2),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, i5),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, d4));

/* A row record for the table path. */
typedef struct {
    int32_t id;
    double ratio;
    const char *name;
} row_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(row_reflect,
                                 row_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(row_t, id),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(row_t, ratio),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(row_t, name));

static double now_ns(void) {
    struct timespec ts = {0};
    timespec_get(&ts, TIME_UTC);
    return ((double)ts.tv_sec * 1e9) + (double)ts.tv_nsec;
}

typedef void (*batch_fn)(void *ctx, long n);

static double bench_ns(batch_fn fn, void *ctx, long *out_iters) {
    long iters = 16;
    for (;;) {
        double start = now_ns();
        double elapsed = 0.0;
        fn(ctx, iters);
        elapsed = now_ns() - start;
        if ((elapsed >= 1e8) || (iters >= (1L << 24))) {
            *out_iters = iters;
            return elapsed / (double)iters;
        }
        iters *= 2;
    }
}

static void report(const char *what, long iters, double ns_per_op, double bytes) {
    double mbps
        = ((0.0 < ns_per_op) && (0.0 < bytes)) ? ((bytes * 1e9) / (ns_per_op * 1048576.0)) : 0.0;
    printf("  %-30s %10.1f ns/op  %12.0f ops/s  %8.2f MB/s  (%ld iters)\n",
           what,
           ns_per_op,
           (0.0 < ns_per_op) ? (1e9 / ns_per_op) : 0.0,
           mbps,
           iters);
}

static void run_case(const char *what, batch_fn fn, void *ctx, double bytes) {
    long iters = 0;
    double ns = bench_ns(fn, ctx, &iters);
    report(what, iters, ns, bytes);
}

static void quiet_logs(void) {
    static const char *const names[]
        = {"reflect", "json", "ds", "log", "ini", "csv", "toml", "msgpack", "cbor", "xml", "bson"};
    for (size_t i = 0; i < (sizeof(names) / sizeof(names[0])); ++i) {
        shrewd_basic_log_module_t *quiet = NULL;
        if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find(names[i], &quiet)) {
            (void)shrewd_basic_log_module_set_level(quiet, SHREWD_BASIC_LOG_LEVEL_NONE);
        }
    }
}

static void fill_cfg(cfg_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->big = INT64_C(1234567890123);
    cfg->ratio = 2.5;
    cfg->level = LEVEL_HIGH;
    cfg->i1 = 1;
    cfg->i2 = 2;
    cfg->i3 = 3;
    cfg->i4 = 4;
    cfg->d1 = 1.25;
    cfg->d2 = 2.5;
    cfg->d3 = -3.75;
    cfg->s1 = "alpha";
    cfg->s2 = "beta";
    cfg->s3 = "gamma";
    cfg->b1 = true;
    cfg->b2 = false;
    memcpy(cfg->tag, "tag-0", 6);
    cfg->u1 = 100u;
    cfg->u2 = 200u;
    cfg->i5 = 5;
    cfg->d4 = 4.5;
}

/* --- text layer scenarios -------------------------------------------------- */

typedef struct {
    const shrewd_basic_reflect_field_t *field;
    void *value;
    char buf[64];
    size_t len;
} text_ctx_t;

static void text_render(void *arg, long n) {
    text_ctx_t *c = (text_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_reflect_render_text(c->field, c->value, c->buf, sizeof(c->buf), &c->len);
    }
}

static void text_scan(void *arg, long n) {
    text_ctx_t *c = (text_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_reflect_scan_text(c->field, c->value, c->buf, c->len);
    }
}

/* --- encode/decode scenarios ---------------------------------------------- */

typedef struct {
    const void *src; /* encode source */
    void *target;    /* decode destination */
    const char *doc;
    size_t doc_len;
    void *bytes;
    size_t bytes_len;
} map_ctx_t;

static void ini_decode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_ini_free(c->target, &cfg_reflect);
        (void)shrewd_basic_ini_decode(c->doc, c->doc_len, c->target, &cfg_reflect, NULL, NULL);
    }
}

static void ini_encode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        char *text = NULL;
        (void)shrewd_basic_ini_encode(c->src, &cfg_reflect, &text, NULL, NULL);
        shrewd_basic_ini_text_free(text);
    }
}

static void toml_decode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_toml_free(c->target, &cfg_reflect);
        (void)shrewd_basic_toml_decode(c->doc, c->doc_len, c->target, &cfg_reflect, NULL);
    }
}

static void toml_encode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        char *text = NULL;
        (void)shrewd_basic_toml_encode(c->src, &cfg_reflect, &text, NULL);
        shrewd_basic_toml_text_free(text);
    }
}

static void csv_decode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        void *rows = NULL;
        size_t count = 0;
        (void)shrewd_basic_csv_decode(c->doc, c->doc_len, &rows, &count, &row_reflect, NULL);
        (void)shrewd_basic_csv_rows_free(rows, count, &row_reflect);
    }
}

static void csv_encode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        char *text = NULL;
        (void)shrewd_basic_csv_encode(c->src, (size_t)100, &row_reflect, &text, NULL);
        shrewd_basic_csv_text_free(text);
    }
}

static void msgpack_encode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        void *bytes = NULL;
        size_t size = 0;
        (void)shrewd_basic_msgpack_encode(c->src, &cfg_reflect, &bytes, &size);
        shrewd_basic_msgpack_bytes_free(bytes);
    }
}

static void msgpack_decode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_msgpack_free(c->target, &cfg_reflect);
        (void)shrewd_basic_msgpack_decode(c->bytes, c->bytes_len, c->target, &cfg_reflect);
    }
}

static void cbor_encode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        void *bytes = NULL;
        size_t size = 0;
        (void)shrewd_basic_cbor_encode(c->src, &cfg_reflect, &bytes, &size);
        shrewd_basic_cbor_bytes_free(bytes);
    }
}

static void cbor_decode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_cbor_free(c->target, &cfg_reflect);
        (void)shrewd_basic_cbor_decode(c->bytes, c->bytes_len, c->target, &cfg_reflect);
    }
}

static void xml_encode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        char *text = NULL;
        (void)shrewd_basic_xml_encode(c->src, &cfg_reflect, NULL, 0, &text, NULL);
        shrewd_basic_xml_text_free(text);
    }
}

static void xml_decode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_xml_free(c->target, &cfg_reflect);
        (void)shrewd_basic_xml_decode(c->doc, c->doc_len, c->target, &cfg_reflect, NULL, 0, NULL);
    }
}

static void bson_encode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        void *bytes = NULL;
        size_t size = 0;
        (void)shrewd_basic_bson_encode(c->src, &cfg_reflect, &bytes, &size);
        shrewd_basic_bson_bytes_free(bytes);
    }
}

static void bson_decode_op(void *arg, long n) {
    map_ctx_t *c = (map_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_bson_free(c->target, &cfg_reflect);
        (void)shrewd_basic_bson_decode(c->bytes, c->bytes_len, c->target, &cfg_reflect);
    }
}

int main(void) {
    static cfg_t cfg;
    static cfg_t target;
    static row_t rows[100];
    static char ini_doc[4096];
    static char toml_doc[4096];
    size_t ini_len = 0;
    size_t toml_len = 0;
    text_ctx_t tctx;
    map_ctx_t mctx;

    fill_cfg(&cfg);
    quiet_logs();

    /* --- text layer --- */
    memset(&tctx, 0, sizeof(tctx));
    tctx.value = &cfg.big;
    (void)shrewd_basic_reflect_type_find_field(&cfg_reflect, "big", &tctx.field);
    (void)shrewd_basic_reflect_render_text(
        tctx.field, tctx.value, tctx.buf, sizeof(tctx.buf), &tctx.len);
    printf("formats_bench: text layer (leaf conversion)\n");
    run_case("render int64", text_render, &tctx, (double)tctx.len);
    run_case("scan int64", text_scan, &tctx, (double)tctx.len);
    tctx.value = &cfg.d4;
    (void)shrewd_basic_reflect_type_find_field(&cfg_reflect, "d4", &tctx.field);
    (void)shrewd_basic_reflect_render_text(
        tctx.field, tctx.value, tctx.buf, sizeof(tctx.buf), &tctx.len);
    run_case("render double", text_render, &tctx, (double)tctx.len);
    run_case("scan double", text_scan, &tctx, (double)tctx.len);
    tctx.value = &cfg.level;
    (void)shrewd_basic_reflect_type_find_field(&cfg_reflect, "level", &tctx.field);
    (void)shrewd_basic_reflect_render_text(
        tctx.field, tctx.value, tctx.buf, sizeof(tctx.buf), &tctx.len);
    run_case("render enum", text_render, &tctx, (double)tctx.len);
    run_case("scan enum", text_scan, &tctx, (double)tctx.len);

    /* --- INI --- */
    ini_len = (size_t)snprintf(ini_doc,
                               sizeof(ini_doc),
                               "big = 1234567890123\nratio = 2.5\nlevel = LEVEL_HIGH\n"
                               "i1 = 1\ni2 = 2\ni3 = 3\ni4 = 4\n"
                               "d1 = 1.25\nd2 = 2.5\nd3 = -3.75\n"
                               "s1 = alpha\ns2 = beta\ns3 = gamma\n"
                               "b1 = true\nb2 = false\ntag = tag-0\n"
                               "u1 = 100\nu2 = 200\ni5 = 5\nd4 = 4.5\n");
    memset(&mctx, 0, sizeof(mctx));
    mctx.src = &target;
    mctx.target = &target;
    mctx.doc = ini_doc;
    mctx.doc_len = ini_len;
    printf("formats_bench: INI (%zu bytes, 20 keys)\n", ini_len);
    run_case("ini decode", ini_decode_op, &mctx, (double)ini_len);
    run_case("ini encode", ini_encode_op, &mctx, (double)ini_len);
    (void)shrewd_basic_ini_free(&target, &cfg_reflect);
    memset(&target, 0, sizeof(target)); /* slots must not dangle across sections */

    /* --- TOML --- */
    toml_len = (size_t)snprintf(toml_doc,
                                sizeof(toml_doc),
                                "big = 1234567890123\nratio = 2.5\nlevel = \"LEVEL_HIGH\"\n"
                                "i1 = 1\ni2 = 2\ni3 = 3\ni4 = 4\n"
                                "d1 = 1.25\nd2 = 2.5\nd3 = -3.75\n"
                                "s1 = \"alpha\"\ns2 = \"beta\"\ns3 = \"gamma\"\n"
                                "b1 = true\nb2 = false\ntag = \"tag-0\"\n"
                                "u1 = 100\nu2 = 200\ni5 = 5\nd4 = 4.5\n");
    mctx.doc = toml_doc;
    mctx.doc_len = toml_len;
    printf("formats_bench: TOML (%zu bytes, 20 keys)\n", toml_len);
    run_case("toml decode", toml_decode_op, &mctx, (double)toml_len);
    run_case("toml encode", toml_encode_op, &mctx, (double)toml_len);
    (void)shrewd_basic_toml_free(&target, &cfg_reflect);
    memset(&target, 0, sizeof(target));

    /* --- CSV (100 rows) --- */
    for (size_t i = 0; i < 100; ++i) {
        rows[i].id = (int32_t)i;
        rows[i].ratio = (double)i * 0.5;
        rows[i].name = "row";
    }
    {
        char *seed = NULL;
        (void)shrewd_basic_csv_encode(rows, 100, &row_reflect, &seed, NULL);
        mctx.doc = seed;
        mctx.doc_len = (NULL != seed) ? strlen(seed) : 0;
        mctx.src = rows;
        mctx.target = rows;
        printf("formats_bench: CSV (100 rows, %zu bytes)\n", mctx.doc_len);
        run_case("csv decode", csv_decode_op, &mctx, (double)mctx.doc_len);
        run_case("csv encode", csv_encode_op, &mctx, (double)mctx.doc_len);
        shrewd_basic_csv_text_free(seed);
    }

    /* --- msgpack / CBOR --- */
    {
        void *bytes = NULL;
        size_t size = 0;
        (void)shrewd_basic_msgpack_encode(&cfg, &cfg_reflect, &bytes, &size);
        mctx.bytes = bytes;
        mctx.bytes_len = size;
        mctx.src = &cfg;
        mctx.target = &target;
        printf("formats_bench: msgpack (%zu bytes)\n", size);
        run_case("msgpack encode", msgpack_encode_op, &mctx, (double)size);
        run_case("msgpack decode", msgpack_decode_op, &mctx, (double)size);
        (void)shrewd_basic_msgpack_free(&target, &cfg_reflect);
        memset(&target, 0, sizeof(target));
        shrewd_basic_msgpack_bytes_free(bytes);
    }
    {
        void *bytes = NULL;
        size_t size = 0;
        (void)shrewd_basic_cbor_encode(&cfg, &cfg_reflect, &bytes, &size);
        mctx.bytes = bytes;
        mctx.bytes_len = size;
        mctx.src = &cfg;
        printf("formats_bench: cbor (%zu bytes)\n", size);
        run_case("cbor encode", cbor_encode_op, &mctx, (double)size);
        run_case("cbor decode", cbor_decode_op, &mctx, (double)size);
        (void)shrewd_basic_cbor_free(&target, &cfg_reflect);
        memset(&target, 0, sizeof(target));
        shrewd_basic_cbor_bytes_free(bytes);
    }

    /* --- XML / BSON --- */
    {
        char *seed = NULL;
        (void)shrewd_basic_xml_encode(&cfg, &cfg_reflect, NULL, 0, &seed, NULL);
        mctx.doc = seed;
        mctx.doc_len = (NULL != seed) ? strlen(seed) : 0;
        mctx.src = &cfg;
        mctx.target = &target;
        printf("formats_bench: xml (%zu bytes)\n", mctx.doc_len);
        run_case("xml encode", xml_encode_op, &mctx, (double)mctx.doc_len);
        run_case("xml decode", xml_decode_op, &mctx, (double)mctx.doc_len);
        (void)shrewd_basic_xml_free(&target, &cfg_reflect);
        memset(&target, 0, sizeof(target));
        shrewd_basic_xml_text_free(seed);
    }
    {
        void *bytes = NULL;
        size_t size = 0;
        (void)shrewd_basic_bson_encode(&cfg, &cfg_reflect, &bytes, &size);
        mctx.bytes = bytes;
        mctx.bytes_len = size;
        mctx.src = &cfg;
        printf("formats_bench: bson (%zu bytes)\n", size);
        run_case("bson encode", bson_encode_op, &mctx, (double)size);
        run_case("bson decode", bson_decode_op, &mctx, (double)size);
        (void)shrewd_basic_bson_free(&target, &cfg_reflect);
        memset(&target, 0, sizeof(target));
        shrewd_basic_bson_bytes_free(bytes);
    }

    printf("formats_bench: done\n");
    return 0;
}

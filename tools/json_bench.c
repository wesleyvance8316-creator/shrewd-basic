/**
 * @file json_bench.c
 * @brief Micro-benchmark for the json hot paths.
 *
 * Parse/serialize a synthetic document (objects with numbers and
 * strings), map it through a described struct, and query the DOM.
 * The numbers are a baseline for optimization work.
 *
 * Usage: tools/json_bench.sh   (built by tools/json_bench.sh)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shrewd-basic/json/json.h"
#include "shrewd-basic/log/log.h"

/* A representative record for the mapping paths. */
typedef struct {
    int32_t id;
    double ratio;
    const char *name;
    int32_t tags[4];
} record_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(record_reflect, record_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(record_t, id),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(record_t, ratio),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(record_t, name),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(record_t, tags, shrewd_basic_reflect_int32));

static double now_ns(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ((double)ts.tv_sec * 1e9) + (double)ts.tv_nsec;
}

static void report(const char *what, long iters, double ns)
{
    printf("  %-28s %8ld iters  %10.1f ns/op\n", what, iters,
           (0 < iters) ? (ns / (double)iters) : 0.0);
}

int main(void)
{
    /* Document: N objects, each with two numbers and a short string. */
    enum { N = 2000, DOC_MAX = 262144 };
    static char doc[DOC_MAX];
    size_t len = 0;
    shrewd_basic_json_value_t *tree = NULL;
    shrewd_basic_log_module_t *quiet = NULL;

    len = (size_t)snprintf(doc, sizeof(doc), "[");
    for (int i = 0; i < N; ++i) {
        len += (size_t)snprintf(doc + len, sizeof(doc) - len,
                                "%s{\"id\":%d,\"ratio\":%g,"
                                "\"name\":\"item-%d\",\"tags\":[1,2,3,4]}",
                                (0 != i) ? "," : "", i, (double)i * 0.5, i);
    }
    (void)snprintf(doc + len, sizeof(doc) - len, "]");
    len = strlen(doc);

    /* Warm-up registers the json log module before it is looked up. */
    {
        shrewd_basic_json_value_t *seed = NULL;
        (void)shrewd_basic_json_parse("[0]", 3, &seed, NULL, NULL);
        shrewd_basic_json_value_free(seed);
    }
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("json", &quiet)) {
        (void)shrewd_basic_log_module_set_level(quiet, SHREWD_BASIC_LOG_LEVEL_NONE);
    }

    printf("json_bench: document %d objects, %zu bytes\n", N, len);

    /* parse */
    {
        long iters = 200;
        double start = now_ns();
        for (long i = 0; i < iters; ++i) {
            shrewd_basic_json_value_free(tree);
            tree = NULL;
            (void)shrewd_basic_json_parse(doc, len, &tree, NULL, NULL);
        }
        report("parse", iters, now_ns() - start);
    }

    /* Small-container document: 1-member objects (the inline case). */
    {
        enum { M = 4000, SMALL_MAX = 131072 };
        static char small[SMALL_MAX];
        size_t small_len = 0;
        shrewd_basic_json_value_t *tiny = NULL;
        small_len = (size_t)snprintf(small, sizeof(small), "[");
        for (int i = 0; i < M; ++i) {
            small_len += (size_t)snprintf(small + small_len,
                                          sizeof(small) - small_len,
                                          "%s{\"k%d\":%d}", (0 != i) ? "," : "",
                                          i, i);
        }
        (void)snprintf(small + small_len, sizeof(small) - small_len, "]");
        small_len = strlen(small);
        printf("small-container document %d objects, %zu bytes\n", M, small_len);
        {
            long iters = 100;
            double start = now_ns();
            for (long i = 0; i < iters; ++i) {
                shrewd_basic_json_value_free(tiny);
                tiny = NULL;
                (void)shrewd_basic_json_parse(small, small_len, &tiny, NULL, NULL);
            }
            report("parse (tiny containers)", iters, now_ns() - start);
            shrewd_basic_json_value_free(tiny);
        }
    }

    /* serialize (compact) */
    {
        long iters = 200;
        double start = now_ns();
        for (long i = 0; i < iters; ++i) {
            char *text = NULL;
            (void)shrewd_basic_json_serialize(tree, &text, NULL, NULL);
            shrewd_basic_json_text_free(text);
        }
        report("serialize", iters, now_ns() - start);
    }

    /* DOM query */
    {
        long iters = 2000;
        const shrewd_basic_json_value_t *found = NULL;
        double start = now_ns();
        for (long i = 0; i < iters; ++i) {
            const shrewd_basic_json_value_t *obj =
                    shrewd_basic_json_at(tree, (size_t)(i % N));
            (void)shrewd_basic_json_pointer(obj, "/name", &found);
        }
        report("pointer query", iters, now_ns() - start);
    }

    /* struct mapping round trip */
    {
        record_t record;
        long iters = 50000;
        double start = 0.0;
        memset(&record, 0, sizeof(record));
        record.id = 42;
        record.ratio = 1.5;
        record.name = "sample";
        record.tags[0] = 7;
        start = now_ns();
        for (long i = 0; i < iters; ++i) {
            char *text = NULL;
            (void)shrewd_basic_json_encode(&record, &record_reflect, NULL, &text,
                                           NULL, NULL);
            shrewd_basic_json_text_free(text);
        }
        report("encode struct", iters, now_ns() - start);

        {
            const char *wire =
                    "{\"id\":42,\"ratio\":1.5,\"name\":\"sample\","
                    "\"tags\":[7,2,3,4]}";
            size_t wire_len = strlen(wire);
            start = now_ns();
            for (long i = 0; i < iters; ++i) {
                record_t target;
                memset(&target, 0, sizeof(target));
                (void)shrewd_basic_json_decode(wire, wire_len, &target,
                                               &record_reflect, NULL, NULL,
                                               NULL);
                (void)shrewd_basic_json_free(&target, &record_reflect, NULL);
            }
        }
        report("decode struct", iters, now_ns() - start);
    }

    shrewd_basic_json_value_free(tree);
    return 0;
}

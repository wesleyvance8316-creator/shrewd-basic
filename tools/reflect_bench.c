/**
 * @file reflect_bench.c
 * @brief Micro-benchmark baseline for the reflect hot paths.
 *
 * Measures per-operation cost of the generic walkers on synthetic
 * shapes: a wide struct (find/resolve paths), a hub object with dynamic
 * arrays (copy/destroy/equal/dump). The numbers are a baseline for
 * future optimization work - nothing here is tuned yet.
 *
 * Usage: tools/reflect_bench   (built by tools/reflect_bench.sh)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

/* A 32-field struct: stresses table scans in find_field/resolve. */
typedef struct {
    int32_t f0;  int32_t f1;  int32_t f2;  int32_t f3;
    int32_t f4;  int32_t f5;  int32_t f6;  int32_t f7;
    int32_t f8;  int32_t f9;  int32_t f10; int32_t f11;
    int32_t f12; int32_t f13; int32_t f14; int32_t f15;
    int32_t f16; int32_t f17; int32_t f18; int32_t f19;
    int32_t f20; int32_t f21; int32_t f22; int32_t f23;
    int32_t f24; int32_t f25; int32_t f26; int32_t f27;
    int32_t f28; int32_t f29; int32_t f30; int32_t f31;
} wide_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(wide_type, wide_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f0),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f1),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f2),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f3),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f4),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f5),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f6),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f7),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f8),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f9),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f10),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f11),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f12),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f13),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f14),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f15),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f16),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f17),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f18),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f19),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f20),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f21),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f22),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f23),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f24),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f25),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f26),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f27),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f28),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f29),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f30),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(wide_t, f31));

/* A hub with dynamic arrays: exercises the allocation-heavy walkers. */
typedef struct {
    int32_t **points;
    size_t point_count;
    int32_t *values;
    size_t value_count;
} hub_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(hub_type, hub_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR_DARRAY(hub_t, points, point_count,
                                          shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(hub_t, point_count),
    SHREWD_BASIC_REFLECT_FIELD_DARRAY(hub_t, values, value_count,
                                      shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(hub_t, value_count));

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

static void *xalloc(size_t size)
{
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, size)) {
        return NULL;
    }
    return block;
}

static hub_t *hub_build(size_t points, size_t values)
{
    hub_t *hub = xalloc(sizeof(hub_t));
    if (NULL == hub) {
        return NULL;
    }
    hub->points = xalloc(points * sizeof(int32_t *));
    hub->point_count = points;
    for (size_t i = 0; i < points; ++i) {
        hub->points[i] = xalloc(sizeof(int32_t));
        if (NULL != hub->points[i]) {
            *hub->points[i] = (int32_t)i;
        }
    }
    hub->values = xalloc(values * sizeof(int32_t));
    hub->value_count = values;
    for (size_t i = 0; i < values; ++i) {
        hub->values[i] = (int32_t)i;
    }
    return hub;
}

int main(void)
{
    printf("reflect_bench: wide_t (32 fields), hub_t (%d point targets, "
           "%d scalars)\n", 1000, 5000);

    /*
     * Silence the log modules first: measurement loops must not pay for
     * logging (the miss path logs at TRACE, and reflect's clone map uses
     * the ds rbtree which logs its own TRACE per operation). The tiny
     * warm-up copy registers the ds module before it is looked up.
     */
    hub_t seed;
    hub_t *probe = NULL;
    memset(&seed, 0, sizeof(seed));
    (void)shrewd_basic_reflect_copy((void **)&probe, &seed, &hub_type);
    (void)shrewd_basic_reflect_destroy(probe, &hub_type);
    const shrewd_basic_reflect_field_t *warmup = NULL;
    (void)shrewd_basic_reflect_type_find_field(&wide_type, "warmup", &warmup);
    shrewd_basic_log_module_t *quiet = NULL;
    shrewd_basic_log_module_t *quiet_ds = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("reflect", &quiet)) {
        (void)shrewd_basic_log_module_set_level(quiet, SHREWD_BASIC_LOG_LEVEL_NONE);
    }
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("ds", &quiet_ds)) {
        (void)shrewd_basic_log_module_set_level(quiet_ds, SHREWD_BASIC_LOG_LEVEL_NONE);
    }

    wide_t wide;
    memset(&wide, 0, sizeof(wide));
    wide.f31 = 7;
    hub_t *hub = hub_build(1000, 5000);
    if (NULL == hub) {
        printf("reflect_bench: setup failed\n");
        return 1;
    }

    double start = 0.0;
    long iters = 0;

    /* find_field: end-of-table hit and a miss. */
    const shrewd_basic_reflect_field_t *field = NULL;
    iters = 200000;
    start = now_ns();
    for (long i = 0; i < iters; ++i) {
        (void)shrewd_basic_reflect_type_find_field(&wide_type, "f31", &field);
    }
    report("find_field (hit, last)", iters, now_ns() - start);
    start = now_ns();
    for (long i = 0; i < iters; ++i) {
        (void)shrewd_basic_reflect_type_find_field(&wide_type, "nope", &field);
    }
    report("find_field (miss)", iters, now_ns() - start);

    /* resolve / path_get on a deep-ish path. */
    size_t offset = 0;
    size_t size = 0;
    start = now_ns();
    for (long i = 0; i < iters; ++i) {
        (void)shrewd_basic_reflect_type_resolve(&wide_type, "f31", &field, &offset, &size);
    }
    report("resolve \"f31\"", iters, now_ns() - start);
    int32_t value = 0;
    start = now_ns();
    for (long i = 0; i < iters; ++i) {
        (void)shrewd_basic_reflect_path_get(&wide, &wide_type, "f31", &value, sizeof(value));
    }
    report("path_get \"f31\"", iters, now_ns() - start);

    /* field_get on the same member. */
    start = now_ns();
    for (long i = 0; i < iters; ++i) {
        (void)shrewd_basic_reflect_field_get(&wide, field, &value, sizeof(value));
    }
    report("field_get", iters, now_ns() - start);

    /* equal on the hub (deep, 6000 elements behind pointers). */
    hub_t *twin = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_copy((void **)&twin, hub, &hub_type)) {
        printf("reflect_bench: setup copy failed\n");
        return 1;
    }
    bool same = false;
    iters = 2000;
    start = now_ns();
    for (long i = 0; i < iters; ++i) {
        (void)shrewd_basic_reflect_equal(hub, twin, &hub_type, &same);
    }
    report("equal (hub vs clone)", iters, now_ns() - start);

    /* copy + destroy round trip (allocation-bound). */
    iters = 2000;
    start = now_ns();
    for (long i = 0; i < iters; ++i) {
        void *clone = NULL;
        if (SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_copy(&clone, hub, &hub_type)) {
            (void)shrewd_basic_reflect_destroy(clone, &hub_type);
        }
    }
    report("copy + destroy (hub)", iters, now_ns() - start);

    /* dump to /dev/null (stdio-bound). */
    FILE *sink = fopen("/dev/null", "w");
    if (NULL != sink) {
        (void)shrewd_basic_log_module_set_level(quiet, SHREWD_BASIC_LOG_LEVEL_INFO);
        (void)shrewd_basic_log_module_set_stream(quiet, sink);
        iters = 200;
        start = now_ns();
        for (long i = 0; i < iters; ++i) {
            (void)shrewd_basic_reflect_dump(&hub_type, hub, "hub");
        }
        report("dump (hub, 6000 el)", iters, now_ns() - start);
        (void)shrewd_basic_log_module_set_stream(quiet, stderr);
        (void)shrewd_basic_log_module_set_level(quiet, SHREWD_BASIC_LOG_LEVEL_NONE);
        fclose(sink);
    }

    (void)shrewd_basic_reflect_destroy(twin, &hub_type);
    (void)shrewd_basic_reflect_destroy(hub, &hub_type);
    return 0;
}

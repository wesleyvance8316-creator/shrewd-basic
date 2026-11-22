/**
 * @file dbus_bench.c
 * @brief Micro-benchmark for the sb_dbus hot paths.
 *
 * Measures the in-process access family (get/peek/set/touch), the
 * notify pipeline (publish with and without subscribers), batch writes,
 * hook-chain overhead and - where POSIX shared memory exists - the shm
 * data plane. The numbers are a baseline for optimization work.
 *
 * Usage: tools/dbus_bench.sh   (built by tools/dbus_bench.sh)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "shrewd-basic/dbus/dbus.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/reflect/reflect.h"

/* A representative value for the access paths. */
typedef struct {
    int32_t level;
    double ratio;
} bench_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(bench_reflect,
                                 bench_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bench_t, level),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(bench_t, ratio));

/* Eight scalar fields for the batch path. */
typedef struct {
    int32_t v[8];
} batch_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(batch_reflect,
                                 batch_t,
                                 SHREWD_BASIC_REFLECT_FIELD_ARRAY(batch_t,
                                                                  v,
                                                                  shrewd_basic_reflect_int32));

static double now_ns(void) {
    struct timespec ts = {0};
    timespec_get(&ts, TIME_UTC);
    return ((double)ts.tv_sec * 1e9) + (double)ts.tv_nsec;
}

/* One adaptive measurement: grow the batch until it runs ~100ms. */
typedef void (*batch_fn)(void *ctx, long n);

static double bench_ns(batch_fn fn, void *ctx, long *out_iters) {
    long iters = 256;
    for (;;) {
        double start = now_ns();
        double elapsed = 0.0;
        fn(ctx, iters);
        elapsed = now_ns() - start;
        if ((elapsed >= 1e8) || (iters >= (1L << 26))) {
            *out_iters = iters;
            return elapsed / (double)iters;
        }
        iters *= 2;
    }
}

static void report(const char *what, long iters, double ns_per_op) {
    printf("  %-30s %10.1f ns/op  %12.0f ops/s  (%ld iters)\n",
           what,
           ns_per_op,
           (0.0 < ns_per_op) ? (1e9 / ns_per_op) : 0.0,
           iters);
}

static void run_case(const char *what, batch_fn fn, void *ctx) {
    long iters = 0;
    double ns = bench_ns(fn, ctx, &iters);
    report(what, iters, ns);
}

/* --- scenario drivers ----------------------------------------------------- */

enum {
    OP_GET_WHOLE,
    OP_GET_FIELD,
    OP_PEEK,
    OP_SET_WHOLE,
    OP_SET_FIELD,
    OP_TOUCH,
    OP_GET_LINKED,
    OP_SET_LINKED,
    OP_SET_MANY,
    OP_PUBLISH_BARE,
    OP_PUBLISH_SUB,
    OP_SET_HOOKS
};

typedef struct {
    shrewd_basic_dbus_t *bus;
    bench_t value;
    bench_t out;
    const void *peeked;
    size_t peeked_size;
    shrewd_basic_dbus_stamp_t stamp;
    int mode;
    int32_t field;
} bench_ctx_t;

static void bench_op(void *arg, long n) {
    bench_ctx_t *c = (bench_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        switch (c->mode) {
        case OP_GET_WHOLE:
            (void)shrewd_basic_dbus_get(c->bus, "/bench/owned", &c->out, sizeof(c->out), NULL);
            break;
        case OP_GET_FIELD:
            (void)shrewd_basic_dbus_get(
                c->bus, "/bench/owned.level", &c->field, sizeof(c->field), NULL);
            break;
        case OP_PEEK:
            (void)shrewd_basic_dbus_peek(c->bus, "/bench/owned", &c->peeked, &c->peeked_size, NULL);
            break;
        case OP_SET_WHOLE:
            (void)shrewd_basic_dbus_set(c->bus, "/bench/owned", &c->value, sizeof(c->value), NULL);
            break;
        case OP_SET_FIELD:
            (void)shrewd_basic_dbus_set(
                c->bus, "/bench/owned.level", &c->field, sizeof(c->field), NULL);
            break;
        case OP_TOUCH:
            (void)shrewd_basic_dbus_touch(c->bus, "/bench/owned", &c->stamp);
            break;
        case OP_GET_LINKED:
            (void)shrewd_basic_dbus_get(c->bus, "/bench/linked", &c->out, sizeof(c->out), NULL);
            break;
        case OP_SET_LINKED:
            (void)shrewd_basic_dbus_set(c->bus, "/bench/linked", &c->value, sizeof(c->value), NULL);
            break;
        case OP_SET_HOOKS:
            (void)shrewd_basic_dbus_set(c->bus, "/bench/hooked", &c->value, sizeof(c->value), NULL);
            break;
        default:
            break;
        }
    }
}

static void bench_set_many(void *arg, long n) {
    bench_ctx_t *c = (bench_ctx_t *)arg;
    const char *paths[8];
    const void *ins[8];
    size_t sizes[8];
    shrewd_basic_dbus_stamp_t stamps[8];
    static batch_t batch;
    for (long i = 0; i < n; ++i) {
        for (size_t k = 0; k < 8; ++k) {
            ins[k] = &batch.v[k];
            sizes[k] = sizeof(batch.v[0]);
            memset(&stamps[k], 0, sizeof(stamps[k]));
        }
        /* Distinct paths per entry. */
        paths[0] = "/bench/many.v[0]";
        paths[1] = "/bench/many.v[1]";
        paths[2] = "/bench/many.v[2]";
        paths[3] = "/bench/many.v[3]";
        paths[4] = "/bench/many.v[4]";
        paths[5] = "/bench/many.v[5]";
        paths[6] = "/bench/many.v[6]";
        paths[7] = "/bench/many.v[7]";
        (void)shrewd_basic_dbus_set_many(c->bus, 8, paths, ins, sizes, stamps);
    }
}

static void bench_publish(void *arg, long n) {
    bench_ctx_t *c = (bench_ctx_t *)arg;
    for (long i = 0; i < n; ++i) {
        (void)shrewd_basic_dbus_publish(c->bus, "/bench/event", &c->field, sizeof(c->field));
    }
    /* Async pipeline: drain so the measurement includes delivery. */
    (void)shrewd_basic_dbus_drain(c->bus);
}

static int drop_note(void *ctx, const shrewd_basic_dbus_note_t *note) {
    (void)ctx;
    (void)note;
    return SHREWD_BASIC_ERR_OK;
}

static int noop_pre_set(void *ctx, shrewd_basic_dbus_req_t *req) {
    (void)ctx;
    (void)req;
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_dbus_ops_t g_one_hook = {.pre_set = noop_pre_set};
static const shrewd_basic_dbus_ops_t g_more_hooks = {.pre_set = noop_pre_set};

static void quiet_logs(void) {
    static const char *const names[] = {"dbus", "reflect", "json", "ds", "log"};
    for (size_t i = 0; i < (sizeof(names) / sizeof(names[0])); ++i) {
        shrewd_basic_log_module_t *quiet = NULL;
        if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find(names[i], &quiet)) {
            (void)shrewd_basic_log_module_set_level(quiet, SHREWD_BASIC_LOG_LEVEL_NONE);
        }
    }
}

static shrewd_basic_dbus_handle_t *mount_owned(shrewd_basic_dbus_t *bus, const char *path) {
    shrewd_basic_dbus_mount_t mount;
    shrewd_basic_dbus_handle_t *handle = NULL;
    memset(&mount, 0, sizeof(mount));
    mount.type = &bench_reflect;
    mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
    (void)shrewd_basic_dbus_mount(bus, path, &mount, &handle);
    return handle;
}

int main(void) {
    shrewd_basic_dbus_t *bus = NULL;
    bench_ctx_t ctx;
    static bench_t linked_value;
    shrewd_basic_dbus_handle_t *h_linked = NULL;
    shrewd_basic_dbus_handle_t *h_many = NULL;
    shrewd_basic_dbus_handle_t *h_hooked = NULL;
    shrewd_basic_dbus_handle_t *h_hook[3];
    shrewd_basic_dbus_handle_t *h_sub = NULL;

    memset(&ctx, 0, sizeof(ctx));
    memset(h_hook, 0, sizeof(h_hook));
    ctx.value.level = 7;
    ctx.value.ratio = 2.5;
    ctx.field = 3;

    if (SHREWD_BASIC_ERR_OK != shrewd_basic_dbus_create(NULL, &bus)) {
        printf("dbus_bench: create FAILED\n");
        return 1;
    }
    (void)mount_owned(bus, "/bench/owned");
    {
        shrewd_basic_dbus_mount_t mount;
        memset(&mount, 0, sizeof(mount));
        mount.type = &bench_reflect;
        mount.store = SHREWD_BASIC_DBUS_STORE_LINKED;
        mount.data = &linked_value;
        (void)shrewd_basic_dbus_mount(bus, "/bench/linked", &mount, &h_linked);
        memset(&mount, 0, sizeof(mount));
        mount.type = &batch_reflect;
        mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
        (void)shrewd_basic_dbus_mount(bus, "/bench/many", &mount, &h_many);
    }
    h_hooked = mount_owned(bus, "/bench/hooked");
    ctx.bus = bus;

    /* Warm-up registers the log modules before they are silenced. */
    bench_op(&ctx, 16);
    quiet_logs();

    printf("dbus_bench: inproc access family\n");
    ctx.mode = OP_GET_WHOLE;
    run_case("get (whole)", bench_op, &ctx);
    ctx.mode = OP_GET_FIELD;
    run_case("get (field)", bench_op, &ctx);
    ctx.mode = OP_PEEK;
    run_case("peek", bench_op, &ctx);
    ctx.mode = OP_SET_WHOLE;
    run_case("set (whole, OWNED)", bench_op, &ctx);
    ctx.mode = OP_SET_FIELD;
    run_case("set (field)", bench_op, &ctx);
    ctx.mode = OP_TOUCH;
    run_case("touch", bench_op, &ctx);
    ctx.mode = OP_GET_LINKED;
    run_case("get (LINKED)", bench_op, &ctx);
    ctx.mode = OP_SET_LINKED;
    run_case("set (LINKED)", bench_op, &ctx);

    printf("dbus_bench: notify pipeline\n");
    ctx.mode = OP_PUBLISH_BARE;
    run_case("publish (no subscriber)", bench_publish, &ctx);
    (void)shrewd_basic_dbus_subscribe(bus, "/bench/**", drop_note, NULL, &h_sub);
    run_case("publish (subscriber+drain)", bench_publish, &ctx);
    (void)shrewd_basic_dbus_unsubscribe(bus, h_sub);
    h_sub = NULL;

    printf("dbus_bench: batch and hooks\n");
    run_case("set_many (8 fields)", bench_set_many, &ctx);
    ctx.mode = OP_SET_HOOKS;
    run_case("set (no hooks)", bench_op, &ctx);
    {
        shrewd_basic_dbus_mount_t gate;
        memset(&gate, 0, sizeof(gate));
        gate.ops = &g_one_hook;
        (void)shrewd_basic_dbus_mount(bus, "/bench/hooked", &gate, &h_hook[0]);
        run_case("set (1 interceptor)", bench_op, &ctx);
        memset(&gate, 0, sizeof(gate));
        gate.ops = &g_more_hooks;
        (void)shrewd_basic_dbus_mount(bus, "/bench/hooked", &gate, &h_hook[1]);
        (void)shrewd_basic_dbus_mount(bus, "/bench/hooked", &gate, &h_hook[2]);
        run_case("set (3 interceptors)", bench_op, &ctx);
    }

#if !defined(_WIN32)
    printf("dbus_bench: shm data plane\n");
    {
        shrewd_basic_dbus_t *shared = NULL;
        if (SHREWD_BASIC_ERR_OK
            == shrewd_basic_dbus_attach("/shrewd-bench", (size_t)1 << 20, NULL, &shared)) {
            shrewd_basic_dbus_handle_t *h = mount_owned(shared, "/bench/owned");
            bench_ctx_t sctx;
            memset(&sctx, 0, sizeof(sctx));
            sctx.bus = shared;
            sctx.value.level = 7;
            sctx.value.ratio = 2.5;
            sctx.field = 3;
            bench_op(&sctx, 16);
            sctx.mode = OP_GET_WHOLE;
            run_case("shm get (whole)", bench_op, &sctx);
            sctx.mode = OP_SET_WHOLE;
            run_case("shm set (whole)", bench_op, &sctx);
            sctx.mode = OP_GET_FIELD;
            run_case("shm get (field)", bench_op, &sctx);
            (void)shrewd_basic_dbus_unmount(shared, h);
            shrewd_basic_dbus_destroy(shared);
            (void)shm_unlink("/shrewd-bench");
        }
    }
#endif

    (void)shrewd_basic_dbus_unmount(bus, h_hook[2]);
    (void)shrewd_basic_dbus_unmount(bus, h_hook[1]);
    (void)shrewd_basic_dbus_unmount(bus, h_hook[0]);
    (void)shrewd_basic_dbus_unmount(bus, h_hooked);
    (void)shrewd_basic_dbus_unmount(bus, h_many);
    (void)shrewd_basic_dbus_unmount(bus, h_linked);
    shrewd_basic_dbus_destroy(bus);
    printf("dbus_bench: done\n");
    return 0;
}

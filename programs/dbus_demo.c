/**
 * @file dbus_demo.c
 * @brief Every sb_dbus interface: inproc mounts (owned/linked/virtual/
 *        interceptor/topic), the access family (get/peek/set/touch/
 *        publish), subscriptions, batch, coalesce, guard and policy
 *        hooks, export/import/dump/drain, and the shm attach path.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shrewd-basic/dbus/dbus.h"
#include "shrewd-basic/formats/json.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

typedef struct {
    int32_t level;
    double ratio;
} cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(cfg_reflect,
                                 cfg_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, level),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, ratio));

static int on_change(void *ctx, const shrewd_basic_dbus_note_t *note) {
    size_t *count = (size_t *)ctx;
    *count += 1;
    printf("  [recv] %s kind=%d ver=%llu size=%zu\n",
           note->path,
           note->kind,
           (unsigned long long)note->version,
           note->size);
    return SHREWD_BASIC_ERR_OK;
}

static int adc_get(void *ctx, const shrewd_basic_dbus_req_t *req, void *out, size_t *size) {
    (void)ctx;
    (void)req;
    *(int32_t *)out = 42; /* "hardware" read */
    *size = sizeof(int32_t);
    return SHREWD_BASIC_ERR_OK;
}

static int adc_set(void *ctx, const shrewd_basic_dbus_req_t *req, const void *in, size_t size) {
    (void)ctx;
    (void)req;
    (void)in;
    (void)size;
    return SHREWD_BASIC_ERR_OK; /* "hardware" write */
}

static const shrewd_basic_dbus_ops_t adc_ops = {.get = adc_get, .set = adc_set};

static void post_set_log(void *ctx, shrewd_basic_dbus_req_t *req) {
    (void)ctx;
    printf("  [post_set] %s v%llu\n",
           shrewd_basic_dbus_req_path(req),
           (unsigned long long)shrewd_basic_dbus_req_version(req));
}

static const shrewd_basic_dbus_ops_t audit_ops = {.post_set = post_set_log};

/* Policy table for the RANGE demo (file scope: DEFINE is extern-linked). */
SHREWD_BASIC_JSON_DEFINE(pol_info, cfg_reflect, SHREWD_BASIC_JSON_RANGE(level, 0, 5));

int main(void) {
    shrewd_basic_dbus_t *bus = NULL;
    shrewd_basic_dbus_options_t options;
    shrewd_basic_dbus_mount_t mount;
    shrewd_basic_dbus_handle_t *h_cfg = NULL;
    shrewd_basic_dbus_handle_t *h_link = NULL;
    shrewd_basic_dbus_handle_t *h_adc = NULL;
    shrewd_basic_dbus_handle_t *h_hook = NULL;
    shrewd_basic_dbus_handle_t *h_topic = NULL;
    shrewd_basic_dbus_handle_t *h_sub = NULL;
    shrewd_basic_dbus_ops_t guard;
    shrewd_basic_dbus_ops_t policy_ops;
    shrewd_basic_dbus_stamp_t stamp;
    static cfg_t linked = {0, 0.0};
    cfg_t cfg;
    int32_t level = 0;
    const void *peeked = NULL;
    size_t peeked_size = 0;
    size_t notes = 0;
    char *json = NULL;

    printf("== shrewd-basic dbus demo ==\n");
    memset(&options, 0, sizeof(options));
    options.sync_notify = 1;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_dbus_create(&options, &bus)) {
        printf("create FAILED\n");
        return 1;
    }
    printf("default bus: %s\n", (NULL != shrewd_basic_dbus_default()) ? "up" : "none");

    /* 1. OWNED mount + the access family. */
    memset(&mount, 0, sizeof(mount));
    mount.type = &cfg_reflect;
    mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
    shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h_cfg);
    memset(&cfg, 0, sizeof(cfg));
    cfg.level = 7;
    cfg.ratio = 2.5;
    shrewd_basic_dbus_set(bus, "/config/log", &cfg, sizeof(cfg), NULL);
    shrewd_basic_dbus_get(bus, "/config/log.level", &level, sizeof(level), NULL);
    shrewd_basic_dbus_peek(bus, "/config/log.ratio", &peeked, &peeked_size, NULL);
    printf(
        "get/peek: level=%d ratio=%g\n", level, (NULL != peeked) ? *(const double *)peeked : 0.0);
    memset(&stamp, 0, sizeof(stamp));
    shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), &stamp);
    level = 9;
    shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), &stamp); /* CAS ok */
    shrewd_basic_dbus_touch(bus, "/config/log", &stamp);
    printf("CAS + touch: version now %llu\n", (unsigned long long)stamp.version);

    /* 2. LINKED mount + touch (zero-copy write path). */
    memset(&mount, 0, sizeof(mount));
    mount.type = &cfg_reflect;
    mount.store = SHREWD_BASIC_DBUS_STORE_LINKED;
    mount.data = &linked;
    shrewd_basic_dbus_mount(bus, "/sensors/imu0", &mount, &h_link);
    linked.level = 5;
    shrewd_basic_dbus_touch(bus, "/sensors/imu0", NULL);

    /* 3. Virtual node (driver mapping) + event topic. */
    memset(&mount, 0, sizeof(mount));
    mount.type = &cfg_reflect;
    mount.ops = &adc_ops;
    shrewd_basic_dbus_mount(bus, "/hw/adc0", &mount, &h_adc);
    shrewd_basic_dbus_get(bus, "/hw/adc0", &cfg, sizeof(cfg), NULL);
    printf("virtual: adc read level=%d\n", cfg.level);
    memset(&mount, 0, sizeof(mount));
    mount.flags = SHREWD_BASIC_DBUS_MOUNT_EVENT;
    shrewd_basic_dbus_mount(bus, "/event/alarm", &mount, &h_topic);

    /* 4. Subscriptions (resource pattern covers field writes). */
    shrewd_basic_dbus_subscribe(bus, "/config/**", on_change, &notes, &h_sub);
    level = 3;
    shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL);
    shrewd_basic_dbus_publish(bus, "/event/alarm", "bang", 5);
    shrewd_basic_dbus_drain(bus);
    printf("notify: %zu note(s) delivered\n", notes);

    /* 5. Batch: all-or-nothing. */
    {
        const char *paths[2];
        const void *ins[2];
        size_t sizes[2];
        shrewd_basic_dbus_stamp_t stamps[2];
        int32_t v0 = 20;
        double v1 = 1.5;
        memset(stamps, 0, sizeof(stamps));
        paths[0] = "/config/log.level";
        paths[1] = "/config/log.ratio";
        ins[0] = &v0;
        ins[1] = &v1;
        sizes[0] = sizeof(v0);
        sizes[1] = sizeof(v1);
        if (SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_set_many(bus, 2, paths, ins, sizes, stamps)) {
            shrewd_basic_dbus_get_many(bus, 2, paths, (void *const *)ins, sizes, stamps);
            printf("set_many/get_many: ok (v=%llu/%llu)\n",
                   (unsigned long long)stamps[0].version,
                   (unsigned long long)stamps[1].version);
        }
    }

    /* 6. coalesce + guard + policy hooks. */
    shrewd_basic_dbus_coalesce(bus, 1);
    level = 4;
    shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL);
    shrewd_basic_dbus_drain(bus);
    shrewd_basic_dbus_coalesce(bus, 0);
    shrewd_basic_dbus_make_guard(SHREWD_BASIC_DBUS_ALLOW_READ, &guard);
    memset(&mount, 0, sizeof(mount));
    mount.ops = &guard;
    shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h_hook);
    level = 5;
    printf("guard: write -> %s\n",
           (SHREWD_BASIC_ERR_OK
            != shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL))
               ? "denied (expected)"
               : "ACCEPTED (unexpected)");
    shrewd_basic_dbus_unmount(bus, h_hook);
    h_hook = NULL;

    /* policy ops: RANGE-style virtual wrapper (see dbus_ops.c note). */
    {
        static struct {
            void *target;
            const shrewd_basic_json_info_t *info;
        } pol_ctx;
        shrewd_basic_dbus_handle_t *h_pol = NULL;
        pol_ctx.target = &linked;
        pol_ctx.info = &pol_info;
        shrewd_basic_dbus_make_policy_ops(&pol_info, &policy_ops);
        memset(&mount, 0, sizeof(mount));
        mount.type = &cfg_reflect;
        mount.ops = &policy_ops;
        mount.ctx = &pol_ctx;
        shrewd_basic_dbus_mount(bus, "/config/guarded", &mount, &h_pol);
        level = 9;
        printf("policy: level=9 -> %s\n",
               (SHREWD_BASIC_ERR_OK
                != shrewd_basic_dbus_set(bus, "/config/guarded.level", &level, sizeof(level), NULL))
                   ? "refused (expected)"
                   : "ACCEPTED (unexpected)");
        level = 3;
        shrewd_basic_dbus_set(bus, "/config/guarded.level", &level, sizeof(level), NULL);
        shrewd_basic_dbus_get(bus, "/config/guarded.level", &level, sizeof(level), NULL);
        printf("policy: level=3 accepted -> %d\n", level);
        shrewd_basic_dbus_unmount(bus, h_pol);
    }
    /* interceptor with audit hook (post_set). */
    memset(&mount, 0, sizeof(mount));
    mount.ops = &audit_ops;
    shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h_hook);
    level = 6;
    shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL);
    shrewd_basic_dbus_unmount(bus, h_hook);
    h_hook = NULL;

    /* 7. export / import / dump. */
    shrewd_basic_dbus_export(bus, "/config/**", &json);
    level = 99;
    shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL);
    shrewd_basic_dbus_import(bus, "/config/**", json, (NULL != json) ? strlen(json) : 0);
    shrewd_basic_dbus_get(bus, "/config/log.level", &level, sizeof(level), NULL);
    printf("export/import: level restored to %d\n", level);
    shrewd_basic_json_text_free(json);
    shrewd_basic_dbus_dump(bus, "/config/**");

    /* 8. teardown. */
    shrewd_basic_dbus_unsubscribe(bus, h_sub);
    shrewd_basic_dbus_unmount(bus, h_adc);
    shrewd_basic_dbus_unmount(bus, h_topic);
    shrewd_basic_dbus_unmount(bus, h_link);
    shrewd_basic_dbus_unmount(bus, h_cfg);
    shrewd_basic_dbus_drain(bus);
    shrewd_basic_dbus_destroy(bus);

#if !defined(_WIN32)
    /* 9. shm transport: attach/ABI-check round trip with a second process. */
    {
        shrewd_basic_dbus_t *shared = NULL;
        shrewd_basic_dbus_handle_t *h = NULL;
        if (SHREWD_BASIC_ERR_OK
            == shrewd_basic_dbus_attach("/demo_bus", 1 << 20, &options, &shared)) {
            pid_t pid = 0;
            memset(&mount, 0, sizeof(mount));
            mount.type = &cfg_reflect;
            mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
            if (SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(shared, "/config/log", &mount, &h)) {
                pid = fork();
                if (0 == pid) {
                    shrewd_basic_dbus_t *peer = NULL;
                    shrewd_basic_dbus_handle_t *h_peer = NULL;
                    if (SHREWD_BASIC_ERR_OK
                        == shrewd_basic_dbus_attach("/demo_bus", 1 << 20, &options, &peer)) {
                        /* Per-process mount registers the local view;
                           the value slot is shared by path. */
                        memset(&mount, 0, sizeof(mount));
                        mount.type = &cfg_reflect;
                        mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
                        if (SHREWD_BASIC_ERR_OK
                            == shrewd_basic_dbus_mount(peer, "/config/log", &mount, &h_peer)) {
                            cfg.level = 42;
                            (void)shrewd_basic_dbus_set(
                                peer, "/config/log.level", &cfg.level, sizeof(cfg.level), NULL);
                            shrewd_basic_dbus_unmount(peer, h_peer);
                        }
                        shrewd_basic_dbus_destroy(peer);
                    }
                    _exit(0);
                }
                (void)waitpid(pid, NULL, 0);
                shrewd_basic_dbus_get(shared, "/config/log.level", &level, sizeof(level), NULL);
                printf("shm: cross-process value = %d\n", level);
                shrewd_basic_dbus_unmount(shared, h);
            }
            shrewd_basic_dbus_destroy(shared);
            (void)shm_unlink("/demo_bus");
        }
    }
#endif

    printf("dbus demo: ok\n");
    return 0;
}

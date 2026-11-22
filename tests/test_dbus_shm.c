#define _POSIX_C_SOURCE 200809L
/**
 * @file test_dbus_shm.c
 * @brief shm transport tests: single-process full chain plus a
 *        two-process smoke (child sets, parent sees value + notify).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shrewd-basic/dbus/dbus.h"

static int g_failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("test_dbus_shm: FAILED %s\n", name);
        ++g_failures;
    }
}

typedef struct {
    int32_t level;
    double ratio;
} shm_cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_shm_cfg_reflect,
                                 shm_cfg_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(shm_cfg_t, level),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(shm_cfg_t, ratio));

static int g_notes = 0;
static int g_last_level = -1;

static int recv_cb(void *ctx, const shrewd_basic_dbus_note_t *note) {
    (void)ctx;
    ++g_notes;
    if ((SHREWD_BASIC_DBUS_NOTE_CHANGE == note->kind) && (sizeof(int32_t) == note->size)) {
        memcpy(&g_last_level, note->value, sizeof(g_last_level));
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Wait up to ~2s for a condition (wake thread is asynchronous). */
static bool wait_for(const int *value, int want) {
    for (int i = 0; i < 400; ++i) {
        struct timespec ts;
        if (*value >= want) {
            return true;
        }
        ts.tv_sec = 0;
        ts.tv_nsec = 5000000;
        (void)nanosleep(&ts, NULL);
    }
    return false;
}

int main(void) {
    shrewd_basic_dbus_t *bus = NULL;
    shrewd_basic_dbus_handle_t *h = NULL;
    shrewd_basic_dbus_handle_t *sub = NULL;
    shrewd_basic_dbus_mount_t mount;
    shrewd_basic_dbus_options_t options;
    shrewd_basic_dbus_stamp_t stamp;
    shm_cfg_t cfg;
    const char *name = "/sdbus-test";
    memset(&mount, 0, sizeof(mount));
    memset(&options, 0, sizeof(options));
    memset(&cfg, 0, sizeof(cfg));
    (void)shm_unlink(name);

    /* --- single-process chain ---------------------------------------- */
    expect("attach", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_attach(name, 1u << 16, NULL, &bus));
    if (NULL == bus) {
        return 1;
    }
    mount.type = &g_shm_cfg_reflect;
    mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
    expect("mount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/cfg/log", &mount, &h));
    cfg.level = 7;
    cfg.ratio = 2.5;
    expect("set",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_set(bus, "/cfg/log", &cfg, sizeof(cfg), NULL));
    memset(&cfg, 0, sizeof(cfg));
    expect("get",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_get(bus, "/cfg/log", &cfg, sizeof(cfg), NULL));
    expect("get value", (7 == cfg.level) && (2.5 == cfg.ratio));
    {
        const void *ptr = NULL;
        size_t size = 0;
        expect("peek",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_peek(bus, "/cfg/log", &ptr, &size, NULL));
        expect("peek shared", (NULL != ptr) && (sizeof(shm_cfg_t) == size));
    }
    memset(&stamp, 0, sizeof(stamp));
    expect("set ver",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(
                   bus, "/cfg/log.level", &(int32_t){3}, sizeof(int32_t), &stamp));
    {
        shrewd_basic_dbus_stamp_t stale = stamp;
        expect("cas ok",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(
                       bus, "/cfg/log.level", &(int32_t){4}, sizeof(int32_t), &stamp));
        expect("cas conflict",
               SHREWD_BASIC_ERR_DBUS_CONFLICT
                   == shrewd_basic_dbus_set(
                       bus, "/cfg/log.level", &(int32_t){5}, sizeof(int32_t), &stale));
    }
    expect("touch", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_touch(bus, "/cfg/log.level", NULL));

    /* --- two-process smoke ------------------------------------------- */
    {
        int32_t level = 0;
        pid_t child = 0;
        int wstatus = 0;
        expect("subscribe",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_subscribe(bus, "/cfg/log", recv_cb, NULL, &sub));
        g_notes = 0;
        g_last_level = -1;
        child = fork();
        if (0 == child) {
            /* child: attach the same region, mount, set, exit */
            shrewd_basic_dbus_t *cbus = NULL;
            shrewd_basic_dbus_handle_t *ch = NULL;
            shrewd_basic_dbus_mount_t cm;
            int32_t v = 42;
            int rc = 1;
            memset(&cm, 0, sizeof(cm));
            cm.type = &g_shm_cfg_reflect;
            cm.store = SHREWD_BASIC_DBUS_STORE_OWNED;
            if ((SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_attach(name, 1u << 16, NULL, &cbus))
                && (NULL != cbus)
                && (SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(cbus, "/cfg/log", &cm, &ch))
                && (SHREWD_BASIC_ERR_OK
                    == shrewd_basic_dbus_set(cbus, "/cfg/log.level", &v, sizeof(v), NULL))) {
                rc = 0;
            }
            if (NULL != cbus) {
                shrewd_basic_dbus_destroy(cbus);
            }
            _exit(rc);
        }
        expect("child started", 0 < child);
        if (0 < child) {
            (void)waitpid(child, &wstatus, 0);
            expect("child ok", (0 != WIFEXITED(wstatus)) && (0 == WEXITSTATUS(wstatus)));
        }
        /* value visible and the change notification arrived */
        expect("remote value",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/cfg/log.level", &level, sizeof(level), NULL));
        expect("remote value is 42", 42 == level);
        expect("remote notify", wait_for(&g_notes, 1));
        expect("remote note value", 42 == g_last_level);
        expect("unsubscribe", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(bus, sub));
        sub = NULL;
    }

    expect("unmount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h));
    h = NULL;
    expect("drain", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_drain(bus));
    shrewd_basic_dbus_destroy(bus);
    (void)shm_unlink(name);

    /* --- non-default change-ring capacity ------------------------------ */
    {
        shrewd_basic_dbus_t *rbus = NULL;
        shrewd_basic_dbus_handle_t *rh = NULL;
        shrewd_basic_dbus_options_t ropt;
        shrewd_basic_dbus_mount_t rm;
        int32_t level = 0;
        pid_t child = 0;
        int wstatus = 0;
        const char *rname = "/sdbus-ring";
        memset(&ropt, 0, sizeof(ropt));
        ropt.ring_size = 8;
        memset(&rm, 0, sizeof(rm));
        rm.type = &g_shm_cfg_reflect;
        rm.store = SHREWD_BASIC_DBUS_STORE_OWNED;
        (void)shm_unlink(rname);
        expect("ring attach",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_attach(rname, 1u << 16, &ropt, &rbus));
        if (NULL != rbus) {
            expect("ring mount",
                   SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(rbus, "/cfg/log", &rm, &rh));
            child = fork();
            if (0 == child) {
                shrewd_basic_dbus_t *peer = NULL;
                shrewd_basic_dbus_handle_t *ph = NULL;
                shrewd_basic_dbus_mount_t pm;
                int32_t v = 42;
                int rc = 1;
                memset(&pm, 0, sizeof(pm));
                pm.type = &g_shm_cfg_reflect;
                pm.store = SHREWD_BASIC_DBUS_STORE_OWNED;
                if ((SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_attach(rname, 1u << 16, NULL, &peer))
                    && (NULL != peer)
                    && (SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(peer, "/cfg/log", &pm, &ph))
                    && (SHREWD_BASIC_ERR_OK
                        == shrewd_basic_dbus_set(peer, "/cfg/log.level", &v, sizeof(v), NULL))) {
                    rc = 0;
                }
                if (NULL != peer) {
                    shrewd_basic_dbus_destroy(peer);
                }
                _exit(rc);
            }
            if (0 < child) {
                (void)waitpid(child, &wstatus, 0);
                expect("ring child ok", (0 != WIFEXITED(wstatus)) && (0 == WEXITSTATUS(wstatus)));
            }
            expect(
                "ring value",
                SHREWD_BASIC_ERR_OK
                    == shrewd_basic_dbus_get(rbus, "/cfg/log.level", &level, sizeof(level), NULL));
            expect("ring value is 42", 42 == level);
            expect("ring unmount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(rbus, rh));
            shrewd_basic_dbus_destroy(rbus);
            (void)shm_unlink(rname);
        }
    }

    if (0 != g_failures) {
        printf("test_dbus_shm: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_dbus_shm: ok\n");
    return 0;
}

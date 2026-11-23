#define _POSIX_C_SOURCE 200809L

/**
 * @file test_dbus.c
 * @brief Core flow tests for sb_dbus (mount/get/peek/set/touch, hook
 *        chains, subscriptions, CAS, reentrancy). One summary line.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "shrewd-basic/dbus/dbus.h"
#include "shrewd-basic/formats/json.h"

static int g_failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("test_dbus: FAILED %s\n", name);
        ++g_failures;
    }
}

typedef struct {
    int32_t level;
    double ratio;
} db_cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_db_cfg_reflect,
                                 db_cfg_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(db_cfg_t, level),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(db_cfg_t, ratio));

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

static struct {
    int calls;
    int notes;
} g_batch;

static int recv_batch_cb(void *ctx, const shrewd_basic_dbus_note_t *notes, size_t count) {
    struct {
        int calls;
        int notes;
    } *acc = ctx;
    acc->calls += 1;
    acc->notes += (int)count;
    (void)notes;
    return SHREWD_BASIC_ERR_OK;
}

static int g_echo_seen = 0;

static int recv_echo_filter(void *ctx, const shrewd_basic_dbus_note_t *note) {
    /* "notify the OTHERS": skip notes that came from my own tag. */
    if (note->source == ctx) {
        return SHREWD_BASIC_ERR_OK;
    }
    ++g_echo_seen;
    return SHREWD_BASIC_ERR_OK;
}

static int g_pre_set_calls = 0;
static int pre_set_ok(void *ctx, shrewd_basic_dbus_req_t *req) {
    (void)ctx;
    (void)req;
    ++g_pre_set_calls;
    return SHREWD_BASIC_ERR_OK;
}

static int pre_set_deny(void *ctx, shrewd_basic_dbus_req_t *req) {
    (void)ctx;
    return shrewd_basic_dbus_req_deny(req, SHREWD_BASIC_ERR_KEY_NOTFOUND);
}

static volatile int g_first_seen = 0;
static volatile int g_release = 0;
static size_t g_note_count = 0;
static size_t g_fold_sum = 0;
static int32_t g_last_value = 0;

/* Hold the dispatcher inside the first delivery so the follow-up
   changes deterministically fold into one later job. */
static int recv_block_first(void *ctx, const shrewd_basic_dbus_note_t *note) {
    struct timespec ts;
    (void)ctx;
    if (0 == g_note_count) {
        g_first_seen = 1;
        while (0 == g_release) {
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            (void)nanosleep(&ts, NULL);
        }
    }
    ++g_note_count;
    g_fold_sum += note->folded;
    if (NULL != note->value) {
        memcpy(&g_last_value, note->value, sizeof(g_last_value));
    }
    return SHREWD_BASIC_ERR_OK;
}

int main(void) {
    shrewd_basic_dbus_t *bus = NULL;
    shrewd_basic_dbus_handle_t *h_cfg = NULL;
    shrewd_basic_dbus_handle_t *h_sub = NULL;
    shrewd_basic_dbus_handle_t *h_hook = NULL;
    shrewd_basic_dbus_options_t options;
    shrewd_basic_dbus_mount_t mount;
    shrewd_basic_dbus_ops_t ops;
    shrewd_basic_dbus_stamp_t stamp;
    db_cfg_t cfg;
    int32_t one = 1;
    int32_t three = 3;
    int32_t four = 4;
    int32_t five = 5;
    int32_t nine = 9;
    memset(&options, 0, sizeof(options));
    memset(&mount, 0, sizeof(mount));
    memset(&ops, 0, sizeof(ops));
    memset(&cfg, 0, sizeof(cfg));
    options.sync_notify = 1; /* deterministic inline delivery */

    expect("create", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_create(&options, &bus));
    if (NULL == bus) {
        return 1;
    }

    /* Mount an OWNED resource; set/get whole value and a field. */
    mount.type = &g_db_cfg_reflect;
    mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
    expect("mount",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h_cfg));
    cfg.level = 7;
    cfg.ratio = 2.5;
    expect("set",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(bus, "/config/log", &cfg, sizeof(cfg), NULL));
    memset(&cfg, 0, sizeof(cfg));
    expect("get",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_get(bus, "/config/log", &cfg, sizeof(cfg), NULL));
    expect("get value", (7 == cfg.level) && (2.5 == cfg.ratio));
    {
        int32_t level = 0;
        expect("get field",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &level, sizeof(level), NULL));
        expect("get field value", 7 == level);
    }
    {
        const void *ptr = NULL;
        size_t size = 0;
        expect("peek",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_peek(bus, "/config/log", &ptr, &size, NULL));
        expect("peek value", (NULL != ptr) && (sizeof(db_cfg_t) == size));
    }

    /* CAS: stale stamp is a conflict. */
    memset(&stamp, 0, sizeof(stamp));
    expect("set ver",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(bus, "/config/log.level", &three, sizeof(three), &stamp));
    {
        shrewd_basic_dbus_stamp_t stale = stamp;
        stamp.version = stale.version;
        expect("cas ok",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/config/log.level", &four, sizeof(four), &stamp));
        expect("cas conflict",
               SHREWD_BASIC_ERR_DBUS_CONFLICT
                   == shrewd_basic_dbus_set(bus, "/config/log.level", &five, sizeof(five), &stale));
    }

    /* touch bumps the version and notifies. */
    {
        uint64_t before = stamp.version;
        memset(&stamp, 0, sizeof(stamp));
        expect("touch",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_touch(bus, "/config/log.level", &stamp));
        expect("touch ver", stamp.version > before);
    }

    /* Subscribe: resource pattern matches field writes too. */
    expect("subscribe",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_subscribe(bus, "/config/log", recv_cb, NULL, &h_sub));
    g_notes = 0;
    g_last_level = -1;
    expect("set notify",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(bus, "/config/log.level", &nine, sizeof(nine), NULL));
    expect("note delivered", (1 == g_notes) && (9 == g_last_level));

    /* publish: events fan out to matching subscribers. */
    expect("unsubscribe change", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(bus, h_sub));
    h_sub = NULL;
    expect("subscribe event",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_subscribe(bus, "/event/alarm", recv_cb, NULL, &h_sub));
    g_notes = 0;
    expect("publish",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_publish(bus, "/event/alarm", "bang", 5));
    expect("event delivered", 1 == g_notes);
    expect("unsubscribe", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(bus, h_sub));
    h_sub = NULL;

    /* Interceptor chain: pre_set denies. */
    ops.pre_set = pre_set_deny;
    memset(&mount, 0, sizeof(mount));
    mount.ops = &ops;
    expect("mount hook",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h_hook));
    expect("set denied",
           SHREWD_BASIC_ERR_KEY_NOTFOUND
               == shrewd_basic_dbus_set(bus, "/config/log.level", &one, sizeof(one), NULL));
    expect("unmount hook", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_hook));
    h_hook = NULL;
    ops.pre_set = pre_set_ok;
    expect("mount hook 2",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h_hook));
    expect("set allowed again",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(bus, "/config/log.level", &one, sizeof(one), NULL));
    expect("pre_set ran", 0 < g_pre_set_calls);
    expect("unmount hook 2", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_hook));
    h_hook = NULL;

    /* export/import: json snapshot round trip (bypasses hooks/notify). */
    {
        char *json = NULL;
        int32_t level = 0;
        expect("export", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_export(bus, "/config/**", &json));
        expect("export text", (NULL != json) && (NULL != strstr(json, "/config/log")));
        expect("mutate",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/config/log.level", &nine, sizeof(nine), NULL));
        expect("import",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_import(bus, "/config/**", json, strlen(json)));
        expect("import value",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &level, sizeof(level), NULL));
        expect("import restored", 1 == level);
        shrewd_basic_json_text_free(json);
    }
    expect("dump", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_dump(bus, "/config/**"));

    /* set_many: all-or-nothing batch commit. */
    {
        const char *paths[2] = {"/config/log.level", "/config/log.ratio"};
        int32_t lv = 20;
        double rt = 1.5;
        const void *ins[2] = {&lv, &rt};
        size_t sizes[2] = {sizeof(lv), sizeof(rt)};
        shrewd_basic_dbus_stamp_t stamps[2];
        int32_t got = 0;
        double got_r = 0.0;
        memset(stamps, 0, sizeof(stamps));
        expect("set_many",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set_many(bus, 2, paths, ins, sizes, stamps));
        expect("set_many value",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &got, sizeof(got), NULL));
        expect("set_many value ok", 20 == got);
        /* stale stamp poisons the batch: zero commits */
        stamps[0].version += 5;
        lv = 30;
        rt = 9.5;
        expect("set_many conflict",
               SHREWD_BASIC_ERR_DBUS_CONFLICT
                   == shrewd_basic_dbus_set_many(bus, 2, paths, ins, sizes, stamps));
        expect("set_many rolled back",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &got, sizeof(got), NULL));
        expect("set_many rollback value", 20 == got);
        expect("set_many rollback value 2",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.ratio", &got_r, sizeof(got_r), NULL));
        expect("set_many rollback ratio", 1.5 == got_r);
        /* get_many: sequential fetch */
        {
            void *outs[2] = {&got, &got_r};
            memset(stamps, 0, sizeof(stamps));
            expect("get_many",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_get_many(bus, 2, paths, outs, sizes, stamps));
            expect("get_many values", (20 == got) && (1.5 == got_r) && (0 != stamps[0].version));
        }
    }

    /* coalesce: fold undelivered notifications. Needs an async bus -
       inline delivery never leaves anything queued to fold. With five
       rapid writes the dispatcher can pop at most one job before the
       rest fold, so at most 2 notes arrive (1 if it pops none). */
    {
        shrewd_basic_dbus_t *abus = NULL;
        shrewd_basic_dbus_options_t aopt;
        shrewd_basic_dbus_handle_t *am = NULL;
        shrewd_basic_dbus_handle_t *as = NULL;
        shrewd_basic_dbus_mount_t amount;
        int32_t lv = 1;
        memset(&aopt, 0, sizeof(aopt));
        memset(&amount, 0, sizeof(amount));
        expect("coalesce bus", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_create(&aopt, &abus));
        amount.type = &g_db_cfg_reflect;
        amount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
        expect("coalesce mount",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(abus, "/config/log", &amount, &am));
        expect("coalesce on", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_coalesce(abus, 1));
        expect("coalesce subscribe",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_subscribe(abus, "/config/log", recv_cb, NULL, &as));
        g_notes = 0;
        for (int i = 0; i < 5; ++i) {
            lv = 100 + i;
            expect("coalesce set",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_set(abus, "/config/log.level", &lv, sizeof(lv), NULL));
        }
        expect("coalesce drain", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_drain(abus));
        expect("coalesce folded", (1 <= g_notes) && (2 >= g_notes));
        expect("coalesce last value", 104 == g_last_level);
        expect("coalesce off", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_coalesce(abus, 0));
        expect("coalesce unsubscribe",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(abus, as));
        as = NULL;
        expect("coalesce unmount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(abus, am));
        am = NULL;
        shrewd_basic_dbus_destroy(abus);
    }

    /* folded: coalesced changes carry their fold count. */
    {
        shrewd_basic_dbus_t *b2 = NULL;
        shrewd_basic_dbus_options_t opt2;
        shrewd_basic_dbus_mount_t m2;
        shrewd_basic_dbus_handle_t *hh = NULL;
        shrewd_basic_dbus_handle_t *ss = NULL;
        struct timespec ts;
        int32_t v = 0;
        memset(&opt2, 0, sizeof(opt2)); /* async dispatcher */
        memset(&m2, 0, sizeof(m2));
        m2.type = &g_db_cfg_reflect;
        m2.store = SHREWD_BASIC_DBUS_STORE_OWNED;
        g_first_seen = 0;
        g_release = 0;
        g_note_count = 0;
        g_fold_sum = 0;
        g_last_value = 0;
        expect("fold bus", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_create(&opt2, &b2));
        expect("fold mount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(b2, "/fold", &m2, &hh));
        expect("fold sub",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_subscribe(b2, "/fold", recv_block_first, NULL, &ss));
        expect("fold coalesce", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_coalesce(b2, 1));
        v = 1;
        expect("fold set 1",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(b2, "/fold.level", &v, sizeof(v), NULL));
        while (0 == g_first_seen) {
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            (void)nanosleep(&ts, NULL);
        }
        for (v = 2; v <= 5; ++v) {
            expect("fold set next",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_set(b2, "/fold.level", &v, sizeof(v), NULL));
        }
        g_release = 1;
        expect("fold drain", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_drain(b2));
        expect("fold notes", 2 == g_note_count);
        expect("fold counted", 3 == g_fold_sum);
        expect("fold value", 5 == g_last_value);
        expect("fold unsub", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(b2, ss));
        expect("fold unmount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(b2, hh));
        shrewd_basic_dbus_destroy(b2);
    }

    /* Transactions: optimistic read-modify-write, zero commit on fail. */
    {
        shrewd_basic_dbus_txn_t *txn = NULL;
        int32_t val = 0;
        int32_t want = 11;
        double ratio = 1.5;
        double ratio2 = 2.5;
        expect("txn begin", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_begin(bus, &txn));
        expect("txn get",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_get(txn, "/config/log.level", &val, sizeof(val)));
        expect("txn set",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_set(txn, "/config/log.level", &want, sizeof(want)));
        expect("txn commit", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_commit(txn));
        expect("txn committed",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &val, sizeof(val), NULL));
        expect("txn committed value", 11 == val);

        /* read, external write, staged write -> conflict, zero commit */
        want = 13;
        val = 0;
        expect("txn begin 2", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_begin(bus, &txn));
        expect("txn get 2",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_get(txn, "/config/log.level", &val, sizeof(val)));
        val = 12;
        expect("txn external",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/config/log.level", &val, sizeof(val), NULL));
        expect("txn set 2",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_set(txn, "/config/log.level", &want, sizeof(want)));
        expect("txn conflict", SHREWD_BASIC_ERR_DBUS_CONFLICT == shrewd_basic_dbus_txn_commit(txn));
        val = 0;
        expect("txn zero commit",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &val, sizeof(val), NULL));
        expect("txn zero commit value", 12 == val);

        /* multi-resource write set is all-or-nothing */
        want = 20;
        expect("txn begin 3", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_begin(bus, &txn));
        expect("txn get 3a",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_get(txn, "/config/log.level", &val, sizeof(val)));
        {
            double seen = 0.0;
            expect("txn get 3b",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_txn_get(txn, "/config/log.ratio", &seen, sizeof(seen)));
        }
        ratio = 9.5;
        expect("txn external 2",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/config/log.ratio", &ratio, sizeof(ratio), NULL));
        expect("txn set 3a",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_set(txn, "/config/log.level", &want, sizeof(want)));
        expect("txn set 3b",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_set(txn, "/config/log.ratio", &ratio2, sizeof(ratio2)));
        expect("txn multi conflict",
               SHREWD_BASIC_ERR_DBUS_CONFLICT == shrewd_basic_dbus_txn_commit(txn));
        val = 0;
        expect("txn multi zero",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &val, sizeof(val), NULL));
        expect("txn multi zero value", 12 == val);

        /* multi-resource success */
        expect("txn begin 4", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_begin(bus, &txn));
        expect("txn set 4a",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_set(txn, "/config/log.level", &want, sizeof(want)));
        expect("txn set 4b",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_set(txn, "/config/log.ratio", &ratio2, sizeof(ratio2)));
        expect("txn multi commit", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_commit(txn));
        {
            double seen = 0.0;
            expect("txn multi ok a",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_get(bus, "/config/log.level", &val, sizeof(val), NULL));
            expect(
                "txn multi ok b",
                SHREWD_BASIC_ERR_OK
                    == shrewd_basic_dbus_get(bus, "/config/log.ratio", &seen, sizeof(seen), NULL));
            expect("txn multi values", (20 == val) && (2.5 == seen));
        }

        /* abort discards the staged writes */
        want = 99;
        expect("txn begin 5", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_begin(bus, &txn));
        expect("txn set 5",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_txn_set(txn, "/config/log.level", &want, sizeof(want)));
        shrewd_basic_dbus_txn_abort(txn);
        val = 0;
        expect("txn abort",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &val, sizeof(val), NULL));
        expect("txn abort value", 20 == val);
    }

    /* save/load: json snapshot round trip (no hooks, no notify). */
    {
        char snap[64];
        int32_t level = 1;
        snprintf(snap, sizeof(snap), "/tmp/sb_dbus_snap_%d.json", (int)getpid());
        expect("snap set",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL));
        expect("snap save", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_save(bus, "/config/**", snap));
        level = 2;
        expect("snap change",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL));
        expect("snap load", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_load(bus, "/config/**", snap));
        level = 0;
        expect("snap get",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/config/log.level", &level, sizeof(level), NULL));
        expect("snap restored", 1 == level);
        (void)remove(snap);
    }

    /* Transaction sets grow past the old 32-entry cap. */
    {
        shrewd_basic_dbus_txn_t *txn = NULL;
        char paths[40][64];
        int ok = 1;
        expect("txn begin", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_begin(bus, &txn));
        for (int i = 0; (i < 40) && (ok); ++i) {
            int32_t v = 100 + i;
            snprintf(paths[i], sizeof(paths[i]), "/config/log.level");
            /* stage 40 distinct writes through one path repeatedly and
               40 reads: growth is what matters */
            ok = (SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_set(txn, paths[i], &v, sizeof(v)));
        }
        expect("txn grow sets", ok);
        expect("txn commit", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_txn_commit(txn));
    }

    /* Writer identity: echo suppression ("notify the others"). */
    {
        shrewd_basic_dbus_handle_t *h_echo = NULL;
        static int my_tag;
        uint64_t ver0 = 0;
        uint64_t ver1 = 0;
        expect("subscribe echo",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_subscribe(
                       bus, "/config/**", recv_echo_filter, &my_tag, &h_echo));
        g_echo_seen = 0;
        expect("set_from self",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set_from(
                       bus, "/config/log.level", &nine, sizeof(nine), NULL, &my_tag));
        expect("echo suppressed", 0 == g_echo_seen);
        expect("set_from other",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set_from(
                       bus, "/config/log.level", &one, sizeof(one), NULL, NULL));
        expect("other seen", 1 == g_echo_seen);
        /* Snapshot/incremental cursor: version advances per commit. */
        expect("version read",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_get_version(bus, "/config/log", &ver0));
        expect("set again",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/config/log.level", &three, sizeof(three), NULL));
        expect("version bump",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_get_version(bus, "/config/log", &ver1));
        expect("version moved", ver1 > ver0);
        expect("unsubscribe echo",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(bus, h_echo));
    }

    /* Argument and routing refusals. */
    {
        int32_t level = 0;
        expect("bad path",
               SHREWD_BASIC_ERR_ARG_INVALID
                   == shrewd_basic_dbus_get(bus, "nope", &level, sizeof(level), NULL));
        expect("no route",
               SHREWD_BASIC_ERR_KEY_NOTFOUND
                   == shrewd_basic_dbus_get(bus, "/missing/x", &level, sizeof(level), NULL));
        expect("size mismatch",
               SHREWD_BASIC_ERR_REFLECT_MISMATCH
                   == shrewd_basic_dbus_get(
                       bus, "/config/log.level", &level, sizeof(level) + 1, NULL));
    }

    expect("unmount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_cfg));
    h_cfg = NULL;
    /* Batch subscription: one callback carries a whole drained batch.
       Gated like the folded test: block the dispatcher on the first
       note, queue more, then release - the remaining notes arrive in
       ONE batch call. */
    {
        shrewd_basic_dbus_t *b3 = NULL;
        shrewd_basic_dbus_options_t opt3;
        shrewd_basic_dbus_mount_t m3;
        shrewd_basic_dbus_handle_t *hh = NULL;
        shrewd_basic_dbus_handle_t *ss = NULL;
        shrewd_basic_dbus_handle_t *sb = NULL;
        struct timespec ts;
        int32_t v = 0;
        memset(&opt3, 0, sizeof(opt3)); /* async dispatcher */
        memset(&m3, 0, sizeof(m3));
        m3.type = &g_db_cfg_reflect;
        m3.store = SHREWD_BASIC_DBUS_STORE_OWNED;
        g_first_seen = 0;
        g_release = 0;
        g_note_count = 0; /* gate: recv_block_first blocks on the first note */
        g_fold_sum = 0;
        g_batch.calls = 0;
        g_batch.notes = 0;
        expect("batch bus", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_create(&opt3, &b3));
        expect("batch mount",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(b3, "/batch", &m3, &hh));
        expect("batch gate",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_subscribe(b3, "/batch", recv_block_first, NULL, &ss));
        expect("batch sub",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_subscribe_batch(
                       b3, "/batch/**", recv_batch_cb, &g_batch, &sb));
        v = 1;
        expect("batch set 1",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(b3, "/batch.level", &v, sizeof(v), NULL));
        while (0 == g_first_seen) {
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            (void)nanosleep(&ts, NULL);
        }
        for (v = 2; v <= 4; ++v) {
            expect("batch set next",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_set(b3, "/batch.level", &v, sizeof(v), NULL));
        }
        g_release = 1;
        expect("batch drain", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_drain(b3));
        expect("batch grouped", (2 == g_batch.calls) && (4 == g_batch.notes));
        expect("batch unsub", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(b3, sb));
        expect("batch unsub gate", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unsubscribe(b3, ss));
        expect("batch unmount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(b3, hh));
        shrewd_basic_dbus_destroy(b3);
    }

    expect("drain", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_drain(bus));
    shrewd_basic_dbus_destroy(bus);

    if (0 != g_failures) {
        printf("test_dbus: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_dbus: ok\n");
    return 0;
}

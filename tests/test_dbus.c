/**
 * @file test_dbus.c
 * @brief Core flow tests for sb_dbus (mount/get/peek/set/touch, hook
 *        chains, subscriptions, CAS, reentrancy). One summary line.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    expect("drain", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_drain(bus));
    shrewd_basic_dbus_destroy(bus);

    if (0 != g_failures) {
        printf("test_dbus: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_dbus: ok\n");
    return 0;
}

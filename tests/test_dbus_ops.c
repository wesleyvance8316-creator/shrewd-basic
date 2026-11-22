/**
 * @file test_dbus_ops.c
 * @brief Tests for the sb_dbus hook factories: permission guards and
 *        json-policy value hooks (docs/dbus-design.md §11). One
 *        summary line, non-zero exit on failure.
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
        printf("test_dbus_ops: FAILED %s\n", name);
        ++g_failures;
    }
}

typedef struct {
    int32_t val;
    double ratio;
} cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_cfg_reflect,
                                 cfg_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, val),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, ratio));

typedef struct {
    int32_t val;
} pol_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_pol_reflect,
                                 pol_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(pol_t, val));

/* Policy table: RANGE 0..5 on `val`. */
SHREWD_BASIC_JSON_DEFINE(g_pol, g_pol_reflect, SHREWD_BASIC_JSON_RANGE(val, 0, 5));

typedef struct {
    int32_t secret;
    int32_t open;
} hide_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_hide_reflect,
                                 hide_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(hide_t, secret),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(hide_t, open));

/* Policy table: HIDDEN on `secret`. */
SHREWD_BASIC_JSON_DEFINE(g_hide_pol, g_hide_reflect, SHREWD_BASIC_JSON_HIDDEN(secret));

/* Caller-side ctx contract for the policy hooks (see dbus_ops.c). */
typedef struct {
    void *target;
    const shrewd_basic_json_info_t *info;
} policy_ctx_t;

static int mount_interceptor(shrewd_basic_dbus_t *bus,
                             const shrewd_basic_dbus_ops_t *ops,
                             shrewd_basic_dbus_handle_t **out) {
    shrewd_basic_dbus_mount_t mount;
    memset(&mount, 0, sizeof(mount));
    mount.ops = ops;
    return shrewd_basic_dbus_mount(bus, "/cfg", &mount, out);
}

int main(void) {
    shrewd_basic_dbus_t *bus = NULL;
    shrewd_basic_dbus_handle_t *h_cfg = NULL;
    shrewd_basic_dbus_handle_t *h_hook = NULL;
    shrewd_basic_dbus_handle_t *h_pol = NULL;
    shrewd_basic_dbus_options_t options;
    shrewd_basic_dbus_mount_t mount;
    shrewd_basic_dbus_ops_t guard;
    shrewd_basic_dbus_ops_t pops;
    cfg_t cfg;
    pol_t shadow;
    policy_ctx_t pctx;
    int32_t three = 3;
    int32_t nine = 9;
    int32_t level = 0;

    memset(&options, 0, sizeof(options));
    memset(&mount, 0, sizeof(mount));
    memset(&guard, 0, sizeof(guard));
    memset(&pops, 0, sizeof(pops));
    memset(&cfg, 0, sizeof(cfg));
    memset(&shadow, 0, sizeof(shadow));
    memset(&pctx, 0, sizeof(pctx));
    options.sync_notify = 1;

    expect("create", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_create(&options, &bus));
    if (NULL == bus) {
        return 1;
    }

    /* Base resource for the guard tests. */
    mount.type = &g_cfg_reflect;
    mount.store = SHREWD_BASIC_DBUS_STORE_OWNED;
    expect("mount", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/cfg", &mount, &h_cfg));
    cfg.val = 1;
    cfg.ratio = 2.5;
    expect("set base",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_set(bus, "/cfg", &cfg, sizeof(cfg), NULL));

    /* READ-only guard: set refused, get allowed. */
    expect("guard read",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_make_guard(SHREWD_BASIC_DBUS_ALLOW_READ, &guard));
    expect("mount read", SHREWD_BASIC_ERR_OK == mount_interceptor(bus, &guard, &h_hook));
    expect("read denies write",
           SHREWD_BASIC_ERR_KEY_NOTFOUND
               == shrewd_basic_dbus_set(bus, "/cfg.val", &three, sizeof(three), NULL));
    expect("read allows get",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_get(bus, "/cfg.val", &level, sizeof(level), NULL));
    expect("unmount read", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_hook));
    h_hook = NULL;

    /* WRITE-only guard: get refused, set allowed. */
    expect("guard write",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_make_guard(SHREWD_BASIC_DBUS_ALLOW_WRITE, &guard));
    expect("mount write", SHREWD_BASIC_ERR_OK == mount_interceptor(bus, &guard, &h_hook));
    expect("write denies read",
           SHREWD_BASIC_ERR_KEY_NOTFOUND
               == shrewd_basic_dbus_get(bus, "/cfg.val", &level, sizeof(level), NULL));
    expect("write allows set",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(bus, "/cfg.val", &three, sizeof(three), NULL));
    expect("unmount write", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_hook));
    h_hook = NULL;

    /* Full mask: everything passes. */
    expect("guard full",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_make_guard(
                   SHREWD_BASIC_DBUS_ALLOW_READ | SHREWD_BASIC_DBUS_ALLOW_WRITE, &guard));
    expect("mount full", SHREWD_BASIC_ERR_OK == mount_interceptor(bus, &guard, &h_hook));
    expect("full get",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_get(bus, "/cfg.val", &level, sizeof(level), NULL));
    expect("full set",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(bus, "/cfg.val", &three, sizeof(three), NULL));
    expect("unmount full", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_hook));
    h_hook = NULL;

    /* Policy hooks: RANGE 0..5 enforced on set, value lands in the
       shadow target; get serves it back. */
    expect("policy ops", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_make_policy_ops(&g_pol, &pops));
    pctx.target = &shadow;
    pctx.info = &g_pol;
    mount.type = &g_pol_reflect;
    mount.data = NULL;
    mount.ops = &pops;
    mount.ctx = &pctx;
    expect("mount policy",
           SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/pol", &mount, &h_pol));
    expect("policy set ok",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_set(bus, "/pol.val", &three, sizeof(three), NULL));
    expect("policy stored", 3 == shadow.val);
    level = 0;
    expect("policy get",
           SHREWD_BASIC_ERR_OK
               == shrewd_basic_dbus_get(bus, "/pol.val", &level, sizeof(level), NULL));
    expect("policy get value", 3 == level);
    expect("policy set out of range",
           SHREWD_BASIC_ERR_REFLECT_TEXT
               == shrewd_basic_dbus_set(bus, "/pol.val", &nine, sizeof(nine), NULL));
    expect("policy unchanged", 3 == shadow.val);

    /* Whole-resource access writes the shadow too (no field row match). */
    {
        pol_t whole;
        memset(&whole, 0, sizeof(whole));
        whole.val = 4;
        expect("policy whole set",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/pol", &whole, sizeof(whole), NULL));
        expect("policy whole stored", 4 == shadow.val);
    }

    expect("unmount policy", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_pol));
    h_pol = NULL;

    /* HIDDEN rows veto their field both ways (docs 12.3). */
    {
        static struct {
            void *target;
            const shrewd_basic_json_info_t *info;
        } hctx;
        static hide_t shadow2;
        int32_t probe = 7;
        memset(&shadow2, 0, sizeof(shadow2));
        hctx.target = &shadow2;
        hctx.info = &g_hide_pol;
        expect("hidden ops",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_make_policy_ops(&g_hide_pol, &pops));
        memset(&mount, 0, sizeof(mount));
        mount.type = &g_hide_reflect;
        mount.ops = &pops;
        mount.ctx = &hctx;
        expect("mount hidden",
               SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/hid", &mount, &h_pol));
        expect("hidden get denied",
               SHREWD_BASIC_ERR_DBUS_UNADDRESSABLE
                   == shrewd_basic_dbus_get(bus, "/hid.secret", &probe, sizeof(probe), NULL));
        expect("hidden set denied",
               SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED
                   == shrewd_basic_dbus_set(bus, "/hid.secret", &probe, sizeof(probe), NULL));

        /* Whole-resource semantics: hidden never surfaces on reads and
           keeps its value across whole writes (docs/dbus-api.md). */
        shadow2.secret = 0x5ec;
        shadow2.open = 1;
        {
            hide_t whole;
            memset(&whole, 0, sizeof(whole));
            expect("whole get",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_get(bus, "/hid", &whole, sizeof(whole), NULL));
            expect("hidden zeroed in whole get", (0 == whole.secret) && (1 == whole.open));
            whole.secret = 0xbad;
            whole.open = 2;
            expect("whole set",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_dbus_set(bus, "/hid", &whole, sizeof(whole), NULL));
            expect("hidden preserved on whole set",
                   (0x5ec == shadow2.secret) && (2 == shadow2.open));
        }
        probe = 5;
        expect("open set",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_set(bus, "/hid.open", &probe, sizeof(probe), NULL));
        probe = 0;
        expect("open get",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_dbus_get(bus, "/hid.open", &probe, sizeof(probe), NULL));
        expect("open value", 5 == probe);
        expect("unmount hidden", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_pol));
        h_pol = NULL;
    }
    expect("unmount base", SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_unmount(bus, h_cfg));
    h_cfg = NULL;
    shrewd_basic_dbus_destroy(bus);

    /* Argument refusals. */
    expect("guard null", SHREWD_BASIC_ERR_ARG_INVALID == shrewd_basic_dbus_make_guard(0, NULL));
    expect("policy null",
           SHREWD_BASIC_ERR_ARG_INVALID == shrewd_basic_dbus_make_policy_ops(NULL, &pops));

    if (0 != g_failures) {
        printf("test_dbus_ops: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_dbus_ops: ok\n");
    return 0;
}

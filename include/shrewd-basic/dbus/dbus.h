#ifndef SHREWD_BASIC_DBUS_DBUS_H
#define SHREWD_BASIC_DBUS_DBUS_H

/**
 * @file dbus.h
 * @brief sb_dbus: a typed global data bus - configuration (stateful
 *        key/value) and events (publish/subscribe) over one namespace,
 *        with hook chains that map drivers (virtual nodes) into the
 *        same addressing scheme. Design: docs/dbus-design.md.
 *
 * Paths address resources with an optional field selector:
 *
 *     /config/log            the whole resource
 *     /config/log.level      one field inside it (reflect path syntax,
 *                            arrays/nesting included)
 *
 * Access style is chosen per call, never at registration:
 * shrewd_basic_dbus_get() copies out, shrewd_basic_dbus_peek() borrows
 * a const address (valid until the resource's next write),
 * shrewd_basic_dbus_set() copies in, shrewd_basic_dbus_touch() announces
 * a change the module made directly to LINKED storage. Storage mode
 * (mount) only declares lifetime: OWNED (bus allocates/frees) vs
 * LINKED (module owns the bytes). A mount with no storage and get/set
 * hooks is a virtual (computed) node.
 *
 * Hooks (the pre/do/post x get/set/notify matrix) run synchronously in
 * the caller's thread; notifications fan out asynchronously through a
 * dispatcher thread (options.sync_notify delivers inline instead).
 * Notes carry the new value snapshot only - capture old values in
 * pre_set if needed. Hook reentry: get/peek anywhere, set/touch on
 * other nodes, never on the node being processed (ERR_DBUS_REENTRANT).
 *
 * Ownership: mounts/subscriptions are handles the caller returns to
 * shrewd_basic_dbus_unmount()/shrewd_basic_dbus_unsubscribe(); OWNED
 * values are released on unmount (shrewd_basic_reflect_destroy, deep).
 * set() on OWNED storage is a raw memcpy of the value (flat value
 * types; deep pointer graphs belong in LINKED storage or the
 * export/import path).
 */

#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/** SHREWD_BASIC_ERR_DBUS_CONFLICT: stamp version does not match. */
/** SHREWD_BASIC_ERR_DBUS_REENTRANT: set/touch of the node being hooked. */
/** SHREWD_BASIC_ERR_DBUS_UNADDRESSABLE: peek on a virtual node. */

typedef struct shrewd_basic_dbus shrewd_basic_dbus_t;
typedef struct shrewd_basic_json_info shrewd_basic_json_info_t;
typedef struct shrewd_basic_dbus_handle shrewd_basic_dbus_handle_t;
typedef struct shrewd_basic_dbus_req shrewd_basic_dbus_req_t;

/** Version stamp: expected version on set/touch (0 = unconditional),
    observed/new version on return. */
typedef struct shrewd_basic_dbus_stamp {
    uint64_t version;
} shrewd_basic_dbus_stamp_t;

/** Options for shrewd_basic_dbus_create(). Every zero field is default. */
typedef struct shrewd_basic_dbus_options {
    int sync_notify; /**< 1: deliver notifications inline (tests); 0: dispatcher thread. */
} shrewd_basic_dbus_options_t;

/** Storage lifetime (access style is chosen per call, not here). */
typedef enum shrewd_basic_dbus_store {
    SHREWD_BASIC_DBUS_STORE_OWNED = 0, /**< Bus allocates and deep-frees. */
    SHREWD_BASIC_DBUS_STORE_LINKED     /**< Module owns the bytes; bus writes through. */
} shrewd_basic_dbus_store_t;

/** Mount flags. */
#define SHREWD_BASIC_DBUS_MOUNT_EVENT ((uint32_t)1u)    /**< Pure event topic (type NULL). */
#define SHREWD_BASIC_DBUS_MOUNT_READONLY ((uint32_t)2u) /**< set/touch refused. */

/** What a notification carries. */
#define SHREWD_BASIC_DBUS_NOTE_CHANGE 0 /**< A resource was written. */
#define SHREWD_BASIC_DBUS_NOTE_EVENT 1  /**< A topic was published. */

typedef struct shrewd_basic_dbus_note {
    const char *path;  /**< Full path (field selector included). */
    const void *value; /**< New value snapshot; valid during recv only. */
    size_t size;
    uint64_t version;
    int kind; /**< SHREWD_BASIC_DBUS_NOTE_*. */
} shrewd_basic_dbus_note_t;

/** Subscriber receive callback (the recv end of notify). */
typedef int (*shrewd_basic_dbus_recv_fn)(void *ctx, const shrewd_basic_dbus_note_t *note);

/**
 * Hook matrix (docs/dbus-design.md §4). pre_ hooks may deny (return an
 * error) or rewrite (shrewd_basic_dbus_req_rewrite on pre_set); CORE
 * get/set belong to the primary mount (storage or virtual provider) -
 * interceptor mounts supply pre/post only. NULL members fall back to
 * the bus default behavior.
 */
typedef struct shrewd_basic_dbus_ops {
    int (*pre_get)(void *ctx, shrewd_basic_dbus_req_t *req);
    int (*get)(void *ctx, const shrewd_basic_dbus_req_t *req, void *out, size_t *size);
    void (*post_get)(void *ctx, shrewd_basic_dbus_req_t *req);
    int (*pre_set)(void *ctx, shrewd_basic_dbus_req_t *req);
    int (*set)(void *ctx, const shrewd_basic_dbus_req_t *req, const void *in, size_t size);
    void (*post_set)(void *ctx, shrewd_basic_dbus_req_t *req);
    int (*pre_notify)(void *ctx, shrewd_basic_dbus_req_t *req);
    void (*post_notify)(void *ctx, shrewd_basic_dbus_req_t *req);
} shrewd_basic_dbus_ops_t;

/** What a mount provides at a path (see docs/dbus-design.md §3). */
typedef struct shrewd_basic_dbus_mount {
    const shrewd_basic_reflect_type_t *type; /**< Node type; NULL with EVENT = topic. */
    shrewd_basic_dbus_store_t store;
    void *data;                         /**< LINKED: the module-owned bytes. */
    const shrewd_basic_dbus_ops_t *ops; /**< Hook chain, may be NULL. */
    void *ctx;
    uint32_t flags;
} shrewd_basic_dbus_mount_t;

/* --- request inspection (hook context) --------------------------------- */

const char *shrewd_basic_dbus_req_path(const shrewd_basic_dbus_req_t *req);
const char *shrewd_basic_dbus_req_field(const shrewd_basic_dbus_req_t *req);
const shrewd_basic_reflect_type_t *shrewd_basic_dbus_req_type(const shrewd_basic_dbus_req_t *req);
uint64_t shrewd_basic_dbus_req_version(const shrewd_basic_dbus_req_t *req);

/** pre_*: veto the operation with @p err (returned to the caller). */
int shrewd_basic_dbus_req_deny(shrewd_basic_dbus_req_t *req, int err);
/** pre_set: replace the value being committed (copied). */
int shrewd_basic_dbus_req_rewrite(shrewd_basic_dbus_req_t *req, const void *in, size_t size);

/* --- lifecycle ---------------------------------------------------------- */

int shrewd_basic_dbus_create(const shrewd_basic_dbus_options_t *options, shrewd_basic_dbus_t **out);
void shrewd_basic_dbus_destroy(shrewd_basic_dbus_t *bus);
/** Process-wide default bus (created on first use). */
shrewd_basic_dbus_t *shrewd_basic_dbus_default(void);

/* --- resources ---------------------------------------------------------- */

int shrewd_basic_dbus_mount(shrewd_basic_dbus_t *bus,
                            const char *path,
                            const shrewd_basic_dbus_mount_t *mount,
                            shrewd_basic_dbus_handle_t **out);
int shrewd_basic_dbus_unmount(shrewd_basic_dbus_t *bus, shrewd_basic_dbus_handle_t *handle);

int shrewd_basic_dbus_subscribe(shrewd_basic_dbus_t *bus,
                                const char *pattern,
                                shrewd_basic_dbus_recv_fn recv,
                                void *ctx,
                                shrewd_basic_dbus_handle_t **out);
int shrewd_basic_dbus_unsubscribe(shrewd_basic_dbus_t *bus, shrewd_basic_dbus_handle_t *handle);

/* --- data --------------------------------------------------------------- */

int shrewd_basic_dbus_get(shrewd_basic_dbus_t *bus,
                          const char *path,
                          void *out,
                          size_t size,
                          shrewd_basic_dbus_stamp_t *stamp);
int shrewd_basic_dbus_peek(shrewd_basic_dbus_t *bus,
                           const char *path,
                           const void **out,
                           size_t *size,
                           shrewd_basic_dbus_stamp_t *stamp);
int shrewd_basic_dbus_set(shrewd_basic_dbus_t *bus,
                          const char *path,
                          const void *in,
                          size_t size,
                          shrewd_basic_dbus_stamp_t *stamp);
int shrewd_basic_dbus_touch(shrewd_basic_dbus_t *bus,
                            const char *path,
                            shrewd_basic_dbus_stamp_t *stamp);
int shrewd_basic_dbus_publish(shrewd_basic_dbus_t *bus,
                              const char *path,
                              const void *payload,
                              size_t size);

/* --- observation / persistence ------------------------------------------ */

int shrewd_basic_dbus_dump(shrewd_basic_dbus_t *bus, const char *pattern);
int shrewd_basic_dbus_export(shrewd_basic_dbus_t *bus, const char *pattern, char **out_json);
int shrewd_basic_dbus_import(shrewd_basic_dbus_t *bus,
                             const char *pattern,
                             const char *json,
                             size_t length);
/** Wait until queued notifications are delivered (tests/shutdown). */
int shrewd_basic_dbus_drain(shrewd_basic_dbus_t *bus);

/* --- phase 2: shm transport ---------------------------------------------- */

/**
 * Open (or create) a shared-memory bus of @p size bytes named @p name:
 * values live in the region (reflect layout, ABI fingerprint-checked at
 * attach), a POSIX robust mutex guards commits and a shared condvar
 * carries cross-process change notifications. Local hooks and
 * subscriptions behave exactly like the in-process bus.
 */
int shrewd_basic_dbus_attach(const char *name,
                             size_t size,
                             const shrewd_basic_dbus_options_t *options,
                             shrewd_basic_dbus_t **out);

/* --- phase 3: batch/coalesce/guard/policy -------------------------------- */

/**
 * All-or-nothing batch write: every stamp is checked and every value
 * committed under one lock, or nothing is (zero commits on any
 * conflict/mismatch). posts and notifications fire only on success.
 */
int shrewd_basic_dbus_set_many(shrewd_basic_dbus_t *bus,
                               size_t count,
                               const char *const *paths,
                               const void *const *ins,
                               const size_t *sizes,
                               shrewd_basic_dbus_stamp_t *stamps);
int shrewd_basic_dbus_get_many(shrewd_basic_dbus_t *bus,
                               size_t count,
                               const char *const *paths,
                               void *const *outs,
                               const size_t *sizes,
                               shrewd_basic_dbus_stamp_t *stamps);

/** Fold undelivered notifications per path down to the newest snapshot. */
int shrewd_basic_dbus_coalesce(shrewd_basic_dbus_t *bus, int on);

/** Allow mask for shrewd_basic_dbus_make_guard(). */
#define SHREWD_BASIC_DBUS_ALLOW_READ ((uint32_t)1u)
#define SHREWD_BASIC_DBUS_ALLOW_WRITE ((uint32_t)2u)

/** Fill @p out with pre_get/pre_set guards denying what the mask omits. */
int shrewd_basic_dbus_make_guard(uint32_t allow_mask, shrewd_basic_dbus_ops_t *out);

/** Fill @p out with a pre_set hook enforcing the policy's RANGE rows. */
int shrewd_basic_dbus_make_policy_ops(const shrewd_basic_json_info_t *info,
                                      shrewd_basic_dbus_ops_t *out);

#endif /* SHREWD_BASIC_DBUS_DBUS_H */

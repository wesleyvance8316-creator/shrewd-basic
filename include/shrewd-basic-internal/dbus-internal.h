#ifndef SHREWD_BASIC_INTERNAL_DBUS_INTERNAL_H
#define SHREWD_BASIC_INTERNAL_DBUS_INTERNAL_H

/*
 * Internal contract between the dbus core (library/dbus/dbus.c) and
 * storage transports (library/dbus/dbus_shm.c): the core routes,
 * runs hook chains and delivers notifications the same way for every
 * transport; the arena supplies "where the bytes live" and "how to
 * wake the other processes". Design: docs/dbus-design.md section 10.
 */

#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/dbus/dbus.h"
#include "shrewd-basic/reflect/reflect.h"

typedef struct shrewd_basic_dbus_arena {
    void *ctx;
    /*
     * Find or create the shared value slot for a mounted resource.
     * Fills the storage address and a pointer to the authoritative
     * version counter (shared in the region, local otherwise).
     */
    int (*val_acquire)(void *ctx,
                       const char *path,
                       const shrewd_basic_reflect_type_t *type,
                       void **out_data,
                       uint64_t **out_seq);
    /** Detach a slot on unmount/destroy (storage stays shared). */
    void (*val_release)(void *ctx, const char *path, void *data);
    /**
     * Called after a successful commit/touch: record the change and
     * wake the other attached processes (their dispatchers synthesize
     * local notifications for their own subscribers).
     */
    void (*commit)(void *ctx, const char *path, const void *value, size_t size, uint64_t seq);
    /** Release the arena when the bus is destroyed (detach/threads). */
    void (*close)(void *ctx);
} shrewd_basic_dbus_arena_t;

/**
 * Build a bus over a storage arena (create() is this with a NULL
 * arena = in-process platform storage). Only dbus_shm.c calls this.
 */
int shrewd_basic_dbus_create_arena(const shrewd_basic_dbus_options_t *options,
                                   const shrewd_basic_dbus_arena_t *arena,
                                   shrewd_basic_dbus_t **out);

/**
 * Synthesize a local CHANGE notification for this process's own
 * subscribers (used by a transport's wake thread when another process
 * committed; the snapshot is read live from the shared slot).
 */
void shrewd_basic_dbus_notify_remote(shrewd_basic_dbus_t *bus, const char *path, uint64_t seq);

#endif /* SHREWD_BASIC_INTERNAL_DBUS_INTERNAL_H */

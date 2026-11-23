#ifndef SHREWD_BASIC_REFLECT_REACT_H
#define SHREWD_BASIC_REFLECT_REACT_H

/**
 * @file react.h
 * @brief Reactive primitives: ref / effect / computed / watch - the
 *        dependency-graph half of "one value changes, every consumer
 *        follows automatically". GUI-agnostic; LVGL bindings, protocol
 *        adapters and derived module states are all consumers.
 *
 * Mechanics: an effect registers which refs it READS while running
 * (track-during-run via a current-effect cursor); shrewd_basic_reflect_ref_set()
 * re-runs every subscribed effect. ref/effect/computed/watch objects
 * are caller-embedded (no allocation); only the link nodes between
 * them are allocated (and freed on dispose).
 *
 *     shrewd_basic_reflect_ref_t count;
 *     int32_t storage = 0;
 *     shrewd_basic_reflect_ref_init(&count, &storage, sizeof(storage));
 *     shrewd_basic_reflect_effect_init(&fx, render, ctx);   (runs now)
 *     shrewd_basic_reflect_ref_set(&count, &next, sizeof(next));
 *            (render() re-runs automatically)
 *
 * Threading: effects run synchronously inside the ref_set call - the
 * the caller thread owns the update (UI kits marshal ref_set onto
 * their UI thread first, see docs/reflect.md).
 */

#include <stddef.h>

#include "shrewd-basic/platform/error.h"

typedef struct shrewd_basic_reflect_ref shrewd_basic_reflect_ref_t;
typedef struct shrewd_basic_reflect_effect shrewd_basic_reflect_effect_t;
typedef struct shrewd_basic_reflect_computed shrewd_basic_reflect_computed_t;
typedef struct shrewd_basic_reflect_watch shrewd_basic_reflect_watch_t;

/** Effect body: read refs via shrewd_basic_reflect_ref_get() to track them. */
typedef void (*shrewd_basic_reflect_effect_fn)(void *ctx);

/** Computed body: derive the value into @p out (=@c size bytes). */
typedef int (*shrewd_basic_reflect_computed_fn)(void *ctx, void *out, size_t size);

/** Watch callback: fires on change with the previous and new value. */
typedef void (*shrewd_basic_reflect_watch_fn)(void *ctx,
                                              const void *old_value,
                                              const void *new_value,
                                              size_t size);

struct shrewd_basic_reflect_ref {
    void *slot;       /**< Value storage, caller-owned @c size bytes. */
    size_t size;      /**< Value width in bytes. */
    void *react_subs; /**< internal: subscriber edges. */
};

struct shrewd_basic_reflect_effect {
    shrewd_basic_reflect_effect_fn fn;
    void *ctx;
    void *react_deps; /**< internal: dependency edges. */
    int react_flags;  /**< internal: dirty/running state. */
};

struct shrewd_basic_reflect_computed {
    shrewd_basic_reflect_ref_t ref;
    shrewd_basic_reflect_effect_t effect;
    shrewd_basic_reflect_computed_fn fn;
    void *ctx;
};

struct shrewd_basic_reflect_watch {
    shrewd_basic_reflect_effect_t effect;
    shrewd_basic_reflect_ref_t *ref;
    shrewd_basic_reflect_watch_fn fn;
    void *ctx;
    void *react_memo;    /**< internal: previous value snapshot. */
    size_t react_memosz; /**< internal: snapshot width. */
    int react_first;     /**< internal: skip the initial run. */
};

/* --- ref: value cell + subscriber list ---------------------------------- */

int shrewd_basic_reflect_ref_init(shrewd_basic_reflect_ref_t *ref, void *slot, size_t size);

/**
 * Write @p value and re-run subscribed effects (synchronously).
 * @p size must match the ref width (REFLECT_MISMATCH otherwise).
 */
int shrewd_basic_reflect_ref_set(shrewd_basic_reflect_ref_t *ref, const void *value, size_t size);

/**
 * Read the current value AND register the running effect as a
 * subscriber (outside an effect run this is a plain read).
 */
const void *shrewd_basic_reflect_ref_get(shrewd_basic_reflect_ref_t *ref);

/* --- effect: auto-tracked re-computation -------------------------------- */

/** Bind @p fn to @p ctx and run it once (dependencies collected). */
int shrewd_basic_reflect_effect_init(shrewd_basic_reflect_effect_t *effect,
                                     shrewd_basic_reflect_effect_fn fn,
                                     void *ctx);

/** Re-run by hand (drops and re-collects dependencies). */
int shrewd_basic_reflect_effect_run(shrewd_basic_reflect_effect_t *effect);

/** Unsubscribe from all refs; safe to re-init afterwards. */
void shrewd_basic_reflect_effect_dispose(shrewd_basic_reflect_effect_t *effect);

/* --- computed: a ref whose value is derived automatically --------------- */

/**
 * Derived value: @p fn fills the storage of @p computed.ref from its
 * own dependencies; every ref_get inside @p fn is tracked. Read the
 * result with shrewd_basic_reflect_ref_get(&computed.ref).
 */
int shrewd_basic_reflect_computed_init(shrewd_basic_reflect_computed_t *computed,
                                       shrewd_basic_reflect_computed_fn fn,
                                       void *ctx,
                                       void *slot,
                                       size_t size);

void shrewd_basic_reflect_computed_dispose(shrewd_basic_reflect_computed_t *computed);

/* --- watch: fire on change (not on the initial run) --------------------- */

/**
 * Call @p fn(old, new) whenever @p ref changes; @p memo is caller
 * storage of @c size bytes holding the previous value between calls.
 */
int shrewd_basic_reflect_watch_init(shrewd_basic_reflect_watch_t *watch,
                                    shrewd_basic_reflect_ref_t *ref,
                                    shrewd_basic_reflect_watch_fn fn,
                                    void *ctx,
                                    void *memo,
                                    size_t size);

void shrewd_basic_reflect_watch_dispose(shrewd_basic_reflect_watch_t *watch);

#endif /* SHREWD_BASIC_REFLECT_REACT_H */

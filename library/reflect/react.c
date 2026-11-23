/**
 * @file react.c
 * @brief Reactive primitives: the dependency graph behind
 *        ref/effect/computed/watch (docs: reflect.md / react.h).
 *
 * Dependency edges are the only allocations in the module (one edge
 * per (ref, effect) pair, freed on re-track or dispose); the ref and
 * effect objects themselves are caller-embedded. Tricking used for
 * auto-tracking: a module-level "current effect" cursor; every
 * ref_get() while an effect runs records the (ref, effect) edge.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/react.h"

/* --- internal: dependency edges ----------------------------------------- */

typedef struct react_edge {
    shrewd_basic_reflect_ref_t *ref;
    shrewd_basic_reflect_effect_t *effect;
    struct react_edge *ref_next;
    struct react_edge *ref_prev;
    struct react_edge *fx_next;
    struct react_edge *fx_prev;
} react_edge_t;

#define REACT_F_RUNNING 1
#define REACT_F_DIRTY 2

/* The effect currently collecting dependencies (NULL outside a run). */
static shrewd_basic_reflect_effect_t *g_react_current = NULL;

static void react_edge_unlink(react_edge_t *edge) {
    if (NULL != edge->ref_prev) {
        edge->ref_prev->ref_next = edge->ref_next;
    } else if (NULL != edge->ref) {
        edge->ref->react_subs = edge->ref_next;
    }
    if (NULL != edge->ref_next) {
        edge->ref_next->ref_prev = edge->ref_prev;
    }
    if (NULL != edge->fx_prev) {
        edge->fx_prev->fx_next = edge->fx_next;
    } else if (NULL != edge->effect) {
        edge->effect->react_deps = edge->fx_next;
    }
    if (NULL != edge->fx_next) {
        edge->fx_next->fx_prev = edge->fx_prev;
    }
    {
        void *old = edge;
        (void)shrewd_basic_platform_free(&old);
    }
}

static void react_effect_clear(shrewd_basic_reflect_effect_t *effect) {
    react_edge_t *edge = (react_edge_t *)effect->react_deps;
    while (NULL != edge) {
        react_edge_t *next = edge->fx_next;
        react_edge_unlink(edge);
        edge = next;
    }
    effect->react_deps = NULL;
}

static int react_track(shrewd_basic_reflect_ref_t *ref, shrewd_basic_reflect_effect_t *effect) {
    react_edge_t *edge = NULL;
    for (react_edge_t *e = (react_edge_t *)effect->react_deps; NULL != e; e = e->fx_next) {
        if (e->ref == ref) {
            return SHREWD_BASIC_ERR_OK; /* already tracked this run */
        }
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&edge, 1, sizeof(*edge))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    edge->ref = ref;
    edge->effect = effect;
    edge->ref_next = (react_edge_t *)ref->react_subs;
    if (NULL != edge->ref_next) {
        edge->ref_next->ref_prev = edge;
    }
    ref->react_subs = edge;
    edge->fx_next = (react_edge_t *)effect->react_deps;
    if (NULL != edge->fx_next) {
        edge->fx_next->fx_prev = edge;
    }
    effect->react_deps = edge;
    return SHREWD_BASIC_ERR_OK;
}

static int react_effect_run(shrewd_basic_reflect_effect_t *effect);

/* Fire every subscriber of @p ref (synchronous; re-entry defers). */
static void react_ref_notify(shrewd_basic_reflect_ref_t *ref) {
    react_edge_t *edge = (react_edge_t *)ref->react_subs;
    while (NULL != edge) {
        react_edge_t *next = edge->ref_next;
        (void)react_effect_run(edge->effect);
        edge = next;
    }
}

static int react_effect_run(shrewd_basic_reflect_effect_t *effect) {
    if (0 != (effect->react_flags & REACT_F_RUNNING)) {
        effect->react_flags |= REACT_F_DIRTY; /* re-run after the current pass */
        return SHREWD_BASIC_ERR_OK;
    }
    effect->react_flags |= REACT_F_RUNNING;
    for (;;) {
        int status = 0;
        shrewd_basic_reflect_effect_t *saved = g_react_current;
        effect->react_flags &= (int)~REACT_F_DIRTY;
        react_effect_clear(effect);
        g_react_current = effect;
        effect->fn(effect->ctx);
        g_react_current = saved;
        if (0 == (effect->react_flags & REACT_F_DIRTY)) {
            break;
        }
        (void)status;
    }
    effect->react_flags &= (int)~REACT_F_RUNNING;
    return SHREWD_BASIC_ERR_OK;
}

/* --- ref ---------------------------------------------------------------- */

int shrewd_basic_reflect_ref_init(shrewd_basic_reflect_ref_t *ref, void *slot, size_t size) {
    if ((NULL == ref) || (NULL == slot) || (0 == size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    ref->slot = slot;
    ref->size = size;
    ref->react_subs = NULL;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_ref_set(shrewd_basic_reflect_ref_t *ref, const void *value, size_t size) {
    if ((NULL == ref) || (NULL == value)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (size != ref->size) {
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(ref->slot, value, size);
    react_ref_notify(ref);
    return SHREWD_BASIC_ERR_OK;
}

const void *shrewd_basic_reflect_ref_get(shrewd_basic_reflect_ref_t *ref) {
    if (NULL == ref) {
        return NULL;
    }
    if (NULL != g_react_current) {
        (void)react_track(ref, g_react_current); /* allocation failure: untracked dependency */
    }
    return ref->slot;
}

/* --- effect ------------------------------------------------------------- */

int shrewd_basic_reflect_effect_init(shrewd_basic_reflect_effect_t *effect,
                                     shrewd_basic_reflect_effect_fn fn,
                                     void *ctx) {
    if ((NULL == effect) || (NULL == fn)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    effect->fn = fn;
    effect->ctx = ctx;
    effect->react_deps = NULL;
    effect->react_flags = 0;
    return react_effect_run(effect);
}

int shrewd_basic_reflect_effect_run(shrewd_basic_reflect_effect_t *effect) {
    if ((NULL == effect) || (NULL == effect->fn)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    return react_effect_run(effect);
}

void shrewd_basic_reflect_effect_dispose(shrewd_basic_reflect_effect_t *effect) {
    if (NULL != effect) {
        react_effect_clear(effect);
    }
}

/* --- computed ----------------------------------------------------------- */

static void react_computed_run(void *ctx) {
    shrewd_basic_reflect_computed_t *computed = (shrewd_basic_reflect_computed_t *)ctx;
    if (SHREWD_BASIC_ERR_OK
        == computed->fn(computed->ctx, computed->ref.slot, computed->ref.size)) {
        react_ref_notify(&computed->ref); /* value replaced: wake consumers */
    }
}

int shrewd_basic_reflect_computed_init(shrewd_basic_reflect_computed_t *computed,
                                       shrewd_basic_reflect_computed_fn fn,
                                       void *ctx,
                                       void *slot,
                                       size_t size) {
    int status = 0;
    if ((NULL == computed) || (NULL == fn)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    computed->fn = fn;
    computed->ctx = ctx;
    status = shrewd_basic_reflect_ref_init(&computed->ref, slot, size);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_reflect_effect_init(&computed->effect, react_computed_run, computed);
}

void shrewd_basic_reflect_computed_dispose(shrewd_basic_reflect_computed_t *computed) {
    if (NULL != computed) {
        shrewd_basic_reflect_effect_dispose(&computed->effect);
    }
}

/* --- watch -------------------------------------------------------------- */

static void react_watch_run(void *ctx) {
    shrewd_basic_reflect_watch_t *watch = (shrewd_basic_reflect_watch_t *)ctx;
    const void *cur = shrewd_basic_reflect_ref_get(watch->ref); /* tracks */
    if (0 != watch->react_first) {
        watch->react_first = 0; /* snapshot only: watch fires on CHANGE */
        memcpy(watch->react_memo, cur, watch->react_memosz);
        return;
    }
    watch->fn(watch->ctx, watch->react_memo, cur, watch->react_memosz);
    memcpy(watch->react_memo, cur, watch->react_memosz);
}

int shrewd_basic_reflect_watch_init(shrewd_basic_reflect_watch_t *watch,
                                    shrewd_basic_reflect_ref_t *ref,
                                    shrewd_basic_reflect_watch_fn fn,
                                    void *ctx,
                                    void *memo,
                                    size_t size) {
    if ((NULL == watch) || (NULL == ref) || (NULL == fn) || (NULL == memo)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (size != ref->size) {
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    watch->ref = ref;
    watch->fn = fn;
    watch->ctx = ctx;
    watch->react_memo = memo;
    watch->react_memosz = size;
    watch->react_first = 1;
    memset(memo, 0, size);
    return shrewd_basic_reflect_effect_init(&watch->effect, react_watch_run, watch);
}

void shrewd_basic_reflect_watch_dispose(shrewd_basic_reflect_watch_t *watch) {
    if (NULL != watch) {
        shrewd_basic_reflect_effect_dispose(&watch->effect);
    }
}

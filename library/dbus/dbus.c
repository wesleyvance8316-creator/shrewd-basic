/**
 * @file dbus.c
 * @brief sb_dbus core: routing (exact radix + compiled patterns),
 *        hook chains, the access family (get/peek/set/touch) and the
 *        async notify pipeline. Design: docs/dbus-design.md.
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/config/config.h"
#include "shrewd-basic-internal/dbus-internal.h"
#include "shrewd-basic/dbus/dbus.h"
#include "shrewd-basic/ds/radix.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

#if SHREWD_BASIC_FORMAT_JSON
#include "shrewd-basic/formats/json.h"
#endif

#define DBUS_SEG_MAX 32
#define DBUS_PATH_MAX 256

SHREWD_BASIC_LOG_MODULE_DEFINE(shrewd_basic_dbus_log, "dbus", SHREWD_BASIC_LOG_LEVEL_ALL);

/* --- path grammar -------------------------------------------------------- */

/* Resource part + optional ".field" selector; '.' only in the last segment. */
static int dbus_split(const char *path, size_t *res_len, const char **field) {
    size_t n = 0;
    size_t last_slash = 0;
    size_t dot = 0;
    if ((NULL == path) || ('/' != path[0]) || ('\0' == path[1])) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    n = strlen(path);
    for (size_t i = 0; i < n; ++i) {
        if ('/' == path[i]) {
            last_slash = i;
        }
    }
    dot = n; /* no field selector */
    for (size_t i = 1; i < n; ++i) {
        if (('.' == path[i]) && (i < last_slash)) {
            return SHREWD_BASIC_ERR_ARG_INVALID; /* dots belong to the last segment */
        }
    }
    for (size_t i = last_slash + 1; i < n; ++i) {
        if ('.' == path[i]) {
            dot = i;
            break;
        }
    }
    /* empty segment check */
    for (size_t i = 1; i <= last_slash; ++i) {
        if (('/' == path[i]) && ('/' == path[i - 1])) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
    }
    if ((dot < n) && ((dot + 1) >= n)) {
        return SHREWD_BASIC_ERR_ARG_INVALID; /* trailing '.' or empty field */
    }
    *res_len = dot;
    *field = (dot < n) ? (path + dot + 1) : NULL;
    return SHREWD_BASIC_ERR_OK;
}

/* --- compiled subscription patterns -------------------------------------- */

#define DBUS_SEG_WILD 1

typedef struct {
    char seg[DBUS_SEG_MAX][64];
    unsigned char wild[DBUS_SEG_MAX];
    size_t count;
    int subtree;    /* trailing double-star: match the rest of the path */
    char field[64]; /* empty = any field */
    int has_field;
} dbus_pattern_t;

static int dbus_pattern_compile(const char *pattern, dbus_pattern_t *out) {
    size_t n = 0;
    size_t start = 1;
    size_t res_len = 0;
    const char *field = NULL;
    memset(out, 0, sizeof(*out));
    if (SHREWD_BASIC_ERR_OK != dbus_split(pattern, &res_len, &field)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((NULL != field) && (strlen(field) >= sizeof(out->field))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL != field) {
        memcpy(out->field, field, strlen(field) + 1);
        out->has_field = 1;
    }
    n = res_len;
    while (start < n) {
        size_t end = start;
        size_t len = 0;
        while ((end < n) && ('/' != pattern[end])) {
            ++end;
        }
        len = end - start;
        if ((0 == len) || (len >= sizeof(out->seg[0])) || (out->count >= DBUS_SEG_MAX)) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        if ((2 == len) && ('*' == pattern[start]) && ('*' == pattern[start + 1])) {
            if (end != n) {
                return SHREWD_BASIC_ERR_ARG_INVALID; /* ** is trailing only */
            }
            out->subtree = 1;
            start = end + 1;
            continue;
        }
        memcpy(out->seg[out->count], pattern + start, len);
        out->seg[out->count][len] = '\0';
        out->wild[out->count] = (0 == strcmp(out->seg[out->count], "*")) ? DBUS_SEG_WILD : 0;
        out->count += 1;
        start = end + 1;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Split a resource path (res_len bytes) into segments for matching. */
static int dbus_match(const dbus_pattern_t *pat, const char *path, size_t res_len) {
    size_t seg = 0;
    size_t start = 1;
    while (start < res_len) {
        size_t end = start;
        while ((end < res_len) && ('/' != path[end])) {
            ++end;
        }
        if (seg >= pat->count) {
            /* only a subtree tail can consume extra segments */
            return (0 != pat->subtree) ? 1 : 0;
        }
        if (0 == pat->wild[seg]) {
            size_t len = end - start;
            if ((len != strlen(pat->seg[seg])) || (0 != memcmp(pat->seg[seg], path + start, len))) {
                return 0;
            }
        }
        ++seg;
        start = end + 1;
    }
    return ((seg == pat->count) || ((0 != pat->subtree) && (seg == pat->count))) ? 1 : 0;
}

/* --- nodes, hooks, subscriptions ----------------------------------------- */

typedef struct dbus_hook {
    const shrewd_basic_dbus_ops_t *ops;
    void *ctx;
    struct dbus_hook *next;
} dbus_hook_t;

typedef struct dbus_sub {
    dbus_pattern_t pattern;
    shrewd_basic_dbus_recv_fn recv;
    void *ctx;
    int refs; /* registration + in-flight deliveries */
    int dead;
    struct dbus_sub *next;
} dbus_sub_t;

typedef struct dbus_node {
    char *path; /* resource path */
    const shrewd_basic_reflect_type_t *type;
    shrewd_basic_dbus_store_t store;
    void *data;
    uint64_t version; /* local backing; pver points here or at the arena seq */
    uint64_t *pver;   /* authoritative version counter */
    uint32_t flags;
    const shrewd_basic_dbus_ops_t *ops; /* primary mount hooks (CORE + pre/post) */
    void *ops_ctx;
    dbus_hook_t *hooks; /* interceptors, registration order */
    int hook_active;
    pthread_t hook_tid;
    int refs; /* in-flight operations */
    int dead;
    struct dbus_node *next;
} dbus_node_t;

struct shrewd_basic_dbus_handle {
    int is_sub;
    dbus_node_t *node;
    dbus_sub_t *sub;
    dbus_hook_t *hook; /* interceptor entry, or NULL */
};

struct shrewd_basic_dbus_req {
    char path[DBUS_PATH_MAX];
    const char *field;
    const shrewd_basic_reflect_type_t *type;
    uint64_t version;
    int denied;
    int err;
    int rewritten;
    const void *rewrite;
    size_t rewrite_size;
};

typedef struct dbus_job {
    char *path;
    void *value;
    size_t size;
    uint64_t version;
    size_t folded; /* same-path changes folded into this job (coalesce) */
    int kind;
    dbus_sub_t **subs;
    size_t sub_count;
    struct dbus_job *next;
} dbus_job_t;

struct shrewd_basic_dbus {
    pthread_mutex_t lock;
    pthread_cond_t idle;
    shrewd_basic_radix_t *exact;
    dbus_node_t *nodes;
    dbus_sub_t *subs;
    dbus_job_t *jobs_head;
    dbus_job_t *jobs_tail;
    int active;                             /* deliveries in flight */
    int running;                            /* dispatcher alive */
    int sync;                               /* deliver inline */
    int coalesce;                           /* fold undelivered notifications per path */
    const shrewd_basic_dbus_arena_t *arena; /* NULL = in-process storage */
    pthread_t thread;
    int thread_made;
};

/* --- helpers -------------------------------------------------------------- */

static char *dbus_strdup(const char *text) {
    size_t n = strlen(text);
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, n + 1)) {
        return NULL;
    }
    memcpy(block, text, n + 1);
    return (char *)block;
}

static void dbus_lock(shrewd_basic_dbus_t *bus) {
    (void)pthread_mutex_lock(&bus->lock);
}

static void dbus_unlock(shrewd_basic_dbus_t *bus) {
    (void)pthread_mutex_unlock(&bus->lock);
}

/* Release node storage per the active transport (shared slots stay). */
static void dbus_node_free_data(shrewd_basic_dbus_t *bus, dbus_node_t *node) {
    if (NULL != bus->arena) {
        bus->arena->val_release(bus->arena->ctx, node->path, node->data);
        return;
    }
    if ((SHREWD_BASIC_DBUS_STORE_OWNED == node->store) && (NULL != node->data)
        && (NULL != node->type)) {
        (void)shrewd_basic_reflect_destroy(node->data, node->type);
    }
}

/* Locate the node + value slot for a full path; bumps node refs. */
static int dbus_resolve(shrewd_basic_dbus_t *bus,
                        const char *path,
                        dbus_node_t **out_node,
                        unsigned char **out_at,
                        size_t *out_size,
                        const shrewd_basic_reflect_type_t **out_type,
                        char *field_copy,
                        size_t field_cap) {
    size_t res_len = 0;
    const char *field = NULL;
    dbus_node_t *node = NULL;
    int status = dbus_split(path, &res_len, &field);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    {
        char key[DBUS_PATH_MAX];
        if (res_len >= sizeof(key)) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        memcpy(key, path, res_len);
        key[res_len] = '\0';
        dbus_lock(bus);
        status = shrewd_basic_radix_search(bus->exact, key, (void **)&node);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        dbus_unlock(bus);
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    if (0 != node->dead) {
        dbus_unlock(bus);
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    node->refs += 1;
    dbus_unlock(bus);
    {
        unsigned char *at = (unsigned char *)node->data;
        size_t size = 0;
        const shrewd_basic_reflect_type_t *type = node->type;
        size = (NULL != type) ? type->sb_refl_size : 0;
        if (NULL != field) {
            const shrewd_basic_reflect_field_t *desc = NULL;
            size_t off = 0;
            if (NULL == type) {
                status = SHREWD_BASIC_ERR_ARG_INVALID;
            } else {
                status = shrewd_basic_reflect_type_resolve(type, field, &desc, &off, &size);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                node->refs -= 1;
                return status;
            }
            type = desc->sb_refl_type;
            at = at + off;
        }
        *out_node = node;
        *out_at = at;
        *out_size = size;
        *out_type = type;
        if ((NULL != field_copy) && (NULL != field)) {
            snprintf(field_copy, field_cap, "%s", field);
        } else if (NULL != field_copy) {
            field_copy[0] = '\0';
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static void dbus_release(shrewd_basic_dbus_t *bus, dbus_node_t *node) {
    dbus_lock(bus);
    node->refs -= 1;
    if ((0 != node->dead) && (0 >= node->refs)) {
        (void)pthread_cond_broadcast(&bus->idle);
    }
    dbus_unlock(bus);
}

/* --- hook chains ---------------------------------------------------------- */

/* Chain = primary mount ops first, then interceptors (registration order);
   post hooks run the onion (reverse). */

typedef struct {
    const shrewd_basic_dbus_ops_t *ops;
    void *ctx;
} dbus_slot_t;

static size_t dbus_chain(shrewd_basic_dbus_t *bus,
                         dbus_node_t *node,
                         dbus_slot_t *slots,
                         size_t cap) {
    size_t n = 0;
    if ((NULL != node->ops) && (n < cap)) {
        slots[n].ops = node->ops;
        slots[n].ctx = node->ops_ctx;
        ++n;
    }
    for (dbus_hook_t *hook = node->hooks; (NULL != hook) && (n < cap); hook = hook->next) {
        slots[n].ops = hook->ops;
        slots[n].ctx = hook->ctx;
        ++n;
    }
    (void)bus;
    return n;
}

static int dbus_run_pre(shrewd_basic_dbus_t *bus,
                        dbus_node_t *node,
                        shrewd_basic_dbus_req_t *req,
                        int is_get) {
    dbus_slot_t slots[64];
    size_t count = dbus_chain(bus, node, slots, 64);
    for (size_t i = 0; i < count; ++i) {
        int (*pre)(void *, shrewd_basic_dbus_req_t *)
            = (0 != is_get) ? slots[i].ops->pre_get : slots[i].ops->pre_set;
        int status = 0;
        if (NULL == pre) {
            continue;
        }
        dbus_lock(bus);
        node->refs += 1;
        node->hook_active = 1;
        node->hook_tid = pthread_self();
        dbus_unlock(bus);
        status = pre(slots[i].ctx, req);
        dbus_lock(bus);
        node->hook_active = 0;
        dbus_unlock(bus);
        dbus_release(bus, node);
        if (SHREWD_BASIC_ERR_OK != status) {
            if (0 == req->denied) {
                req->denied = 1;
                req->err = status;
            }
            return req->err;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static void dbus_run_post(shrewd_basic_dbus_t *bus,
                          dbus_node_t *node,
                          shrewd_basic_dbus_req_t *req,
                          int is_get) {
    dbus_slot_t slots[64];
    size_t count = dbus_chain(bus, node, slots, 64);
    for (size_t i = count; 0 < i; --i) {
        void (*post)(void *, shrewd_basic_dbus_req_t *)
            = (0 != is_get) ? slots[i - 1].ops->post_get : slots[i - 1].ops->post_set;
        if (NULL == post) {
            continue;
        }
        dbus_lock(bus);
        node->refs += 1;
        node->hook_active = 1;
        node->hook_tid = pthread_self();
        dbus_unlock(bus);
        post(slots[i - 1].ctx, req);
        dbus_lock(bus);
        node->hook_active = 0;
        dbus_unlock(bus);
        dbus_release(bus, node);
    }
}

/* Same-node set/touch from inside a hook is refused (reentrancy rule). */
static int dbus_reentrant(shrewd_basic_dbus_t *bus, dbus_node_t *node) {
    int bad = 0;
    dbus_lock(bus);
    bad = ((0 != node->hook_active) && (0 != pthread_equal(node->hook_tid, pthread_self()))) ? 1
                                                                                             : 0;
    dbus_unlock(bus);
    return bad;
}

static void dbus_req_init(shrewd_basic_dbus_req_t *req,
                          const char *path,
                          const char *field,
                          const shrewd_basic_reflect_type_t *type,
                          uint64_t version) {
    memset(req, 0, sizeof(*req));
    snprintf(req->path, sizeof(req->path), "%s", path);
    req->field = (0 != field[0]) ? field : NULL;
    req->type = type;
    req->version = version;
}

const char *shrewd_basic_dbus_req_path(const shrewd_basic_dbus_req_t *req) {
    return (NULL != req) ? req->path : NULL;
}
const char *shrewd_basic_dbus_req_field(const shrewd_basic_dbus_req_t *req) {
    return (NULL != req) ? req->field : NULL;
}
const shrewd_basic_reflect_type_t *shrewd_basic_dbus_req_type(const shrewd_basic_dbus_req_t *req) {
    return (NULL != req) ? req->type : NULL;
}
uint64_t shrewd_basic_dbus_req_version(const shrewd_basic_dbus_req_t *req) {
    return (NULL != req) ? req->version : 0;
}
int shrewd_basic_dbus_req_deny(shrewd_basic_dbus_req_t *req, int err) {
    if (NULL == req) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    req->denied = 1;
    req->err = (SHREWD_BASIC_ERR_OK != err) ? err : SHREWD_BASIC_ERR_ARG_INVALID;
    return req->err;
}
int shrewd_basic_dbus_req_rewrite(shrewd_basic_dbus_req_t *req, const void *in, size_t size) {
    if ((NULL == req) || (NULL == in)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    req->rewrite = in;
    req->rewrite_size = size;
    req->rewritten = 1;
    return SHREWD_BASIC_ERR_OK;
}

/* --- notify pipeline ------------------------------------------------------ */

static void dbus_deliver(shrewd_basic_dbus_t *bus, dbus_job_t *job) {
    shrewd_basic_dbus_note_t note;
    memset(&note, 0, sizeof(note));
    note.path = job->path;
    note.value = job->value;
    note.size = job->size;
    note.version = job->version;
    note.folded = job->folded;
    note.kind = job->kind;
    for (size_t i = 0; i < job->sub_count; ++i) {
        dbus_sub_t *sub = job->subs[i];
        if ((0 == sub->dead) && (NULL != sub->recv)) {
            (void)sub->recv(sub->ctx, &note);
        }
    }
    dbus_lock(bus);
    for (size_t i = 0; i < job->sub_count; ++i) {
        dbus_sub_t *sub = job->subs[i];
        sub->refs -= 1;
        if ((0 != sub->dead) && (0 >= sub->refs)) {
            void *old = sub;
            (void)shrewd_basic_platform_free(&old);
        }
    }
    bus->active -= 1;
    if ((NULL == bus->jobs_head) && (0 >= bus->active)) {
        (void)pthread_cond_broadcast(&bus->idle);
    }
    dbus_unlock(bus);
    {
        void *old = job->value;
        (void)shrewd_basic_platform_free(&old);
        old = job->subs;
        (void)shrewd_basic_platform_free(&old);
        old = job->path;
        (void)shrewd_basic_platform_free(&old);
        old = job;
        (void)shrewd_basic_platform_free(&old);
    }
}

static void *dbus_dispatch(void *arg) {
    shrewd_basic_dbus_t *bus = (shrewd_basic_dbus_t *)arg;
    for (;;) {
        dbus_job_t *job = NULL;
        dbus_lock(bus);
        while ((NULL == bus->jobs_head) && (0 != bus->running)) {
            (void)pthread_cond_wait(&bus->idle, &bus->lock);
        }
        if ((0 == bus->running) && (NULL == bus->jobs_head)) {
            dbus_unlock(bus);
            return NULL;
        }
        job = bus->jobs_head;
        bus->jobs_head = job->next;
        if (NULL == bus->jobs_head) {
            bus->jobs_tail = NULL;
        }
        dbus_unlock(bus);
        dbus_deliver(bus, job);
    }
}

static int dbus_match_note(const dbus_sub_t *sub, const char *path) {
    size_t res_len = 0;
    const char *field = NULL;
    if (SHREWD_BASIC_ERR_OK != dbus_split(path, &res_len, &field)) {
        return 0;
    }
    if (0 == dbus_match(&sub->pattern, path, res_len)) {
        return 0;
    }
    if ((0 != sub->pattern.has_field)
        && ((NULL == field) || (0 != strcmp(sub->pattern.field, field)))) {
        return 0;
    }
    return 1;
}

static void dbus_enqueue(shrewd_basic_dbus_t *bus,
                         const char *path,
                         const void *value,
                         size_t size,
                         uint64_t version,
                         int kind) {
    dbus_job_t *job = NULL;
    size_t matches = 0;
    dbus_sub_t **subs = NULL;
    size_t i = 0;
    dbus_lock(bus);
    if (0 != bus->coalesce) {
        /* Fold into an undelivered job for the same path (keep its
           subscriber list; only the snapshot and version move on). */
        for (dbus_job_t *j = bus->jobs_head; NULL != j; j = j->next) {
            void *fresh = NULL;
            size_t cap = 0;
            if ((j->kind != kind) || (0 != strcmp(j->path, path))) {
                continue;
            }
            cap = (0 < size) ? size : 1;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&fresh, j->value, cap)) {
                break;
            }
            j->value = fresh;
            if (0 < size) {
                memcpy(j->value, value, size);
            }
            j->size = size;
            j->version = version;
            j->folded += 1;
            dbus_unlock(bus);
            return;
        }
    }
    for (dbus_sub_t *sub = bus->subs; NULL != sub; sub = sub->next) {
        if ((0 == sub->dead) && (0 != dbus_match_note(sub, path))) {
            ++matches;
        }
    }
    if (0 == matches) {
        dbus_unlock(bus);
        return;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&job, 1, sizeof(*job))) {
        dbus_unlock(bus);
        return;
    }
    if ((SHREWD_BASIC_ERR_OK
         != shrewd_basic_platform_calloc((void **)&subs, matches, sizeof(*subs)))
        || (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_calloc((void **)&job->value, 1, (0 < size) ? size : 1))
        || (NULL == (job->path = dbus_strdup(path)))) {
        void *old = job->value;
        (void)shrewd_basic_platform_free(&old);
        old = subs;
        (void)shrewd_basic_platform_free(&old);
        old = job;
        (void)shrewd_basic_platform_free(&old);
        dbus_unlock(bus);
        return;
    }
    if (0 < size) {
        memcpy(job->value, value, size);
    }
    job->size = size;
    job->version = version;
    job->kind = kind;
    for (dbus_sub_t *sub = bus->subs; (NULL != sub) && (i < matches); sub = sub->next) {
        if ((0 == sub->dead) && (0 != dbus_match_note(sub, path))) {
            sub->refs += 1;
            subs[i] = sub;
            ++i;
        }
    }
    job->subs = subs;
    job->sub_count = matches;
    bus->active += 1;
    if (NULL != bus->jobs_tail) {
        bus->jobs_tail->next = job;
    } else {
        bus->jobs_head = job;
    }
    bus->jobs_tail = job;
    if (0 != bus->sync) {
        dbus_job_t *head = bus->jobs_head;
        bus->jobs_head = head->next;
        if (NULL == bus->jobs_head) {
            bus->jobs_tail = NULL;
        }
        dbus_unlock(bus);
        dbus_deliver(bus, head);
        return;
    }
    (void)pthread_cond_signal(&bus->idle);
    dbus_unlock(bus);
}

/* --- lifecycle ------------------------------------------------------------- */

int shrewd_basic_dbus_create(const shrewd_basic_dbus_options_t *options,
                             shrewd_basic_dbus_t **out) {
    return shrewd_basic_dbus_create_arena(options, NULL, out);
}

int shrewd_basic_dbus_create_arena(const shrewd_basic_dbus_options_t *options,
                                   const shrewd_basic_dbus_arena_t *arena,
                                   shrewd_basic_dbus_t **out) {
    shrewd_basic_dbus_t *bus = NULL;
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&bus, 1, sizeof(*bus))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    (void)pthread_mutex_init(&bus->lock, NULL);
    (void)pthread_cond_init(&bus->idle, NULL);
    bus->sync = ((NULL != options) && (0 != options->sync_notify)) ? 1 : 0;
    bus->arena = arena;
    bus->running = 1;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_radix_create(&bus->exact)) {
        shrewd_basic_dbus_destroy(bus);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if (0 == bus->sync) {
        if (0 != pthread_create(&bus->thread, NULL, dbus_dispatch, bus)) {
            shrewd_basic_dbus_destroy(bus);
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        bus->thread_made = 1;
    }
    *out = bus;
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_dbus_destroy(shrewd_basic_dbus_t *bus) {
    if (NULL == bus) {
        return;
    }
    dbus_lock(bus);
    bus->running = 0;
    (void)pthread_cond_broadcast(&bus->idle);
    dbus_unlock(bus);
    if (0 != bus->thread_made) {
        (void)pthread_join(bus->thread, NULL);
    }
    for (dbus_job_t *job = bus->jobs_head; NULL != job;) {
        dbus_job_t *next = job->next;
        void *old = job->value;
        (void)shrewd_basic_platform_free(&old);
        old = job->subs;
        (void)shrewd_basic_platform_free(&old);
        old = job->path;
        (void)shrewd_basic_platform_free(&old);
        old = job;
        (void)shrewd_basic_platform_free(&old);
        job = next;
    }
    bus->jobs_head = NULL;
    bus->jobs_tail = NULL;
    for (dbus_node_t *node = bus->nodes; NULL != node;) {
        dbus_node_t *next = node->next;
        for (dbus_hook_t *hook = node->hooks; NULL != hook;) {
            dbus_hook_t *hnext = hook->next;
            void *old = hook;
            (void)shrewd_basic_platform_free(&old);
            hook = hnext;
        }
        dbus_node_free_data(bus, node);
        {
            void *old = node->path;
            (void)shrewd_basic_platform_free(&old);
            old = node;
            (void)shrewd_basic_platform_free(&old);
        }
        node = next;
    }
    for (dbus_sub_t *sub = bus->subs; NULL != sub;) {
        dbus_sub_t *next = sub->next;
        void *old = sub;
        (void)shrewd_basic_platform_free(&old);
        sub = next;
    }
    if (NULL != bus->exact) {
        (void)shrewd_basic_radix_destroy(bus->exact);
    }
    if ((NULL != bus->arena) && (NULL != bus->arena->close)) {
        bus->arena->close(bus->arena->ctx);
    }
    (void)pthread_cond_destroy(&bus->idle);
    (void)pthread_mutex_destroy(&bus->lock);
    {
        void *old = bus;
        (void)shrewd_basic_platform_free(&old);
    }
}

static shrewd_basic_dbus_t *g_default_bus = NULL;

static void dbus_default_init(void) {
    (void)shrewd_basic_dbus_create(NULL, &g_default_bus);
}

shrewd_basic_dbus_t *shrewd_basic_dbus_default(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    (void)pthread_once(&once, dbus_default_init);
    return g_default_bus;
}

/* --- mount/unmount --------------------------------------------------------- */

int shrewd_basic_dbus_mount(shrewd_basic_dbus_t *bus,
                            const char *path,
                            const shrewd_basic_dbus_mount_t *mount,
                            shrewd_basic_dbus_handle_t **out) {
    size_t res_len = 0;
    const char *field = NULL;
    dbus_node_t *node = NULL;
    shrewd_basic_dbus_handle_t *handle = NULL;
    int is_event = 0;
    int is_interceptor = 0;
    int is_virtual = 0;
    char key[DBUS_PATH_MAX];
    if ((NULL == bus) || (NULL == mount) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if ((SHREWD_BASIC_ERR_OK != dbus_split(path, &res_len, &field)) || (NULL != field)) {
        return SHREWD_BASIC_ERR_ARG_INVALID; /* mounts name whole resources */
    }
    if (res_len >= sizeof(key)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memcpy(key, path, res_len);
    key[res_len] = '\0';
    is_event = (0u != (mount->flags & SHREWD_BASIC_DBUS_MOUNT_EVENT)) ? 1 : 0;
    is_interceptor = ((0 == is_event) && (NULL == mount->type) && (NULL == mount->data)
                      && (NULL != mount->ops))
                         ? 1
                         : 0;
    is_virtual = ((0 == is_event) && (NULL != mount->type) && (NULL == mount->data)
                  && (NULL != mount->ops) && (NULL != mount->ops->get) && (NULL != mount->ops->set))
                     ? 1
                     : 0;
    if ((0 == is_event) && (0 == is_interceptor) && (NULL == mount->type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((0 != is_event) && (0 == is_interceptor) && (NULL != mount->type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID; /* EVENT <=> type NULL */
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&handle, 1, sizeof(*handle))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if (0 != is_interceptor) {
        dbus_hook_t *hook = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&hook, 1, sizeof(*hook))) {
            void *old = handle;
            (void)shrewd_basic_platform_free(&old);
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        hook->ops = mount->ops;
        hook->ctx = mount->ctx;
        dbus_lock(bus);
        {
            int status = shrewd_basic_radix_search(bus->exact, key, (void **)&node);
            if ((SHREWD_BASIC_ERR_OK != status) || (0 != node->dead)) {
                dbus_unlock(bus);
                {
                    void *old = hook;
                    (void)shrewd_basic_platform_free(&old);
                    old = handle;
                    (void)shrewd_basic_platform_free(&old);
                }
                return SHREWD_BASIC_ERR_KEY_NOTFOUND;
            }
            /* append to tail: registration order for pre_ hooks */
            if (NULL == node->hooks) {
                node->hooks = hook;
            } else {
                dbus_hook_t *tail = node->hooks;
                while (NULL != tail->next) {
                    tail = tail->next;
                }
                tail->next = hook;
            }
        }
        dbus_unlock(bus);
        handle->node = node;
        handle->hook = hook;
        *out = handle;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&node, 1, sizeof(*node))) {
        void *old = handle;
        (void)shrewd_basic_platform_free(&old);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    node->pver = &node->version;
    node->path = dbus_strdup(key);
    if (NULL == node->path) {
        void *old = node;
        (void)shrewd_basic_platform_free(&old);
        old = handle;
        (void)shrewd_basic_platform_free(&old);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    node->type = mount->type;
    node->store = mount->store;
    node->flags = mount->flags;
    node->ops = mount->ops;
    node->ops_ctx = mount->ctx;
    (*node->pver) = 1;
    if ((0 == is_virtual) && (0 == is_event) && (NULL != mount->type)) {
        if (NULL != bus->arena) {
            /* Shared storage: the arena owns the bytes and the version. */
            uint64_t *seq = NULL;
            int astatus = bus->arena->val_acquire(
                bus->arena->ctx, node->path, mount->type, &node->data, &seq);
            if (SHREWD_BASIC_ERR_OK != astatus) {
                void *old = node->path;
                (void)shrewd_basic_platform_free(&old);
                old = node;
                (void)shrewd_basic_platform_free(&old);
                old = handle;
                (void)shrewd_basic_platform_free(&old);
                return astatus;
            }
            node->pver = seq;
        } else if (SHREWD_BASIC_DBUS_STORE_LINKED == mount->store) {
            if (NULL == mount->data) {
                void *old = node->path;
                (void)shrewd_basic_platform_free(&old);
                old = node;
                (void)shrewd_basic_platform_free(&old);
                old = handle;
                (void)shrewd_basic_platform_free(&old);
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            node->data = mount->data;
        } else {
            if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_calloc(&node->data, 1, mount->type->sb_refl_size)) {
                void *old = node->path;
                (void)shrewd_basic_platform_free(&old);
                old = node;
                (void)shrewd_basic_platform_free(&old);
                old = handle;
                (void)shrewd_basic_platform_free(&old);
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
        }
    }
    dbus_lock(bus);
    {
        int status = shrewd_basic_radix_insert(bus->exact, node->path, node);
        if (SHREWD_BASIC_ERR_OK == status) {
            node->next = bus->nodes;
            bus->nodes = node;
        } else {
            status = SHREWD_BASIC_ERR_KEY_EXISTS;
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            dbus_unlock(bus);
            dbus_node_free_data(bus, node);
            {
                void *old = node->path;
                (void)shrewd_basic_platform_free(&old);
                old = node;
                (void)shrewd_basic_platform_free(&old);
                old = handle;
                (void)shrewd_basic_platform_free(&old);
            }
            return status;
        }
    }
    dbus_unlock(bus);
    handle->node = node;
    *out = handle;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_unmount(shrewd_basic_dbus_t *bus, shrewd_basic_dbus_handle_t *handle) {
    dbus_node_t *node = NULL;
    if ((NULL == bus) || (NULL == handle) || (0 != handle->is_sub)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    node = handle->node;
    dbus_lock(bus);
    if (NULL != handle->hook) {
        /* interceptor: unlink the hook entry */
        dbus_hook_t **link = &node->hooks;
        while ((NULL != *link) && (*link != handle->hook)) {
            link = &(*link)->next;
        }
        if (NULL != *link) {
            *link = handle->hook->next;
        }
        dbus_unlock(bus);
        {
            void *old = handle->hook;
            (void)shrewd_basic_platform_free(&old);
            old = handle;
            (void)shrewd_basic_platform_free(&old);
        }
        return SHREWD_BASIC_ERR_OK;
    }
    /* primary: retire the node once in-flight work drains */
    node->dead = 1;
    {
        char key[DBUS_PATH_MAX];
        snprintf(key, sizeof(key), "%s", node->path);
        (void)shrewd_basic_radix_remove(bus->exact, key, NULL);
    }
    {
        dbus_node_t **link = &bus->nodes;
        while ((NULL != *link) && (*link != node)) {
            link = &(*link)->next;
        }
        if (NULL != *link) {
            *link = node->next;
        }
    }
    while (0 < node->refs) {
        (void)pthread_cond_wait(&bus->idle, &bus->lock);
    }
    dbus_unlock(bus);
    for (dbus_hook_t *hook = node->hooks; NULL != hook;) {
        dbus_hook_t *next = hook->next;
        void *old = hook;
        (void)shrewd_basic_platform_free(&old);
        hook = next;
    }
    dbus_node_free_data(bus, node);
    {
        void *old = node->path;
        (void)shrewd_basic_platform_free(&old);
        old = node;
        (void)shrewd_basic_platform_free(&old);
        old = handle;
        (void)shrewd_basic_platform_free(&old);
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_subscribe(shrewd_basic_dbus_t *bus,
                                const char *pattern,
                                shrewd_basic_dbus_recv_fn recv,
                                void *ctx,
                                shrewd_basic_dbus_handle_t **out) {
    dbus_sub_t *sub = NULL;
    shrewd_basic_dbus_handle_t *handle = NULL;
    if ((NULL == bus) || (NULL == recv) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&sub, 1, sizeof(*sub))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if (SHREWD_BASIC_ERR_OK != dbus_pattern_compile(pattern, &sub->pattern)) {
        void *old = sub;
        (void)shrewd_basic_platform_free(&old);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    sub->recv = recv;
    sub->ctx = ctx;
    sub->refs = 1;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&handle, 1, sizeof(*handle))) {
        void *old = sub;
        (void)shrewd_basic_platform_free(&old);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    handle->is_sub = 1;
    handle->sub = sub;
    dbus_lock(bus);
    sub->next = bus->subs;
    bus->subs = sub;
    dbus_unlock(bus);
    *out = handle;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_unsubscribe(shrewd_basic_dbus_t *bus, shrewd_basic_dbus_handle_t *handle) {
    dbus_sub_t *sub = NULL;
    if ((NULL == bus) || (NULL == handle) || (0 == handle->is_sub)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    sub = handle->sub;
    dbus_lock(bus);
    sub->dead = 1;
    {
        dbus_sub_t **link = &bus->subs;
        while ((NULL != *link) && (*link != sub)) {
            link = &(*link)->next;
        }
        if (NULL != *link) {
            *link = sub->next;
        }
    }
    sub->refs -= 1;
    if (0 >= sub->refs) {
        void *old = sub;
        (void)shrewd_basic_platform_free(&old);
    }
    dbus_unlock(bus);
    {
        void *old = handle;
        (void)shrewd_basic_platform_free(&old);
    }
    return SHREWD_BASIC_ERR_OK;
}

/* --- data ------------------------------------------------------------------ */

int shrewd_basic_dbus_get(shrewd_basic_dbus_t *bus,
                          const char *path,
                          void *out,
                          size_t size,
                          shrewd_basic_dbus_stamp_t *stamp) {
    dbus_node_t *node = NULL;
    unsigned char *at = NULL;
    size_t want = 0;
    const shrewd_basic_reflect_type_t *type = NULL;
    char field[64];
    shrewd_basic_dbus_req_t req;
    int status = 0;
    if ((NULL == bus) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = dbus_resolve(bus, path, &node, &at, &want, &type, field, sizeof(field));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    dbus_lock(bus);
    dbus_req_init(&req, path, field, type, (*node->pver));
    dbus_unlock(bus);
    status = dbus_run_pre(bus, node, &req, 1);
    if (SHREWD_BASIC_ERR_OK == status) {
        if (size != want) {
            status = SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        } else if ((NULL != node->ops) && (NULL != node->ops->get)) {
            size_t produced = size;
            status = node->ops->get(node->ops_ctx, &req, out, &produced);
            if ((SHREWD_BASIC_ERR_OK == status) && (produced != size)) {
                status = SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
        } else if (NULL != node->data) {
            dbus_lock(bus);
            memcpy(out, at, size);
            dbus_unlock(bus);
        } else {
            status = SHREWD_BASIC_ERR_ARG_INVALID; /* event topic */
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        dbus_run_post(bus, node, &req, 1);
    }
    if (NULL != stamp) {
        dbus_lock(bus);
        stamp->version = (*node->pver);
        dbus_unlock(bus);
    }
    dbus_release(bus, node);
    return status;
}

int shrewd_basic_dbus_peek(shrewd_basic_dbus_t *bus,
                           const char *path,
                           const void **out,
                           size_t *size,
                           shrewd_basic_dbus_stamp_t *stamp) {
    dbus_node_t *node = NULL;
    unsigned char *at = NULL;
    size_t want = 0;
    const shrewd_basic_reflect_type_t *type = NULL;
    char field[64];
    shrewd_basic_dbus_req_t req;
    int status = 0;
    if ((NULL == bus) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = dbus_resolve(bus, path, &node, &at, &want, &type, field, sizeof(field));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    dbus_lock(bus);
    dbus_req_init(&req, path, field, type, (*node->pver));
    dbus_unlock(bus);
    status = dbus_run_pre(bus, node, &req, 1);
    if (SHREWD_BASIC_ERR_OK == status) {
        if (NULL == node->data) {
            status = SHREWD_BASIC_ERR_DBUS_UNADDRESSABLE; /* virtual/topic: no stable bytes */
        } else {
            *out = at;
            if (NULL != size) {
                *size = want;
            }
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        dbus_run_post(bus, node, &req, 1);
    }
    if (NULL != stamp) {
        dbus_lock(bus);
        stamp->version = (*node->pver);
        dbus_unlock(bus);
    }
    dbus_release(bus, node);
    return status;
}

int shrewd_basic_dbus_set(shrewd_basic_dbus_t *bus,
                          const char *path,
                          const void *in,
                          size_t size,
                          shrewd_basic_dbus_stamp_t *stamp) {
    dbus_node_t *node = NULL;
    unsigned char *at = NULL;
    size_t want = 0;
    const shrewd_basic_reflect_type_t *type = NULL;
    char field[64];
    shrewd_basic_dbus_req_t req;
    const void *payload = in;
    size_t payload_size = size;
    uint64_t version = 0;
    int status = 0;
    if ((NULL == bus) || (NULL == in)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = dbus_resolve(bus, path, &node, &at, &want, &type, field, sizeof(field));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0u != (node->flags & SHREWD_BASIC_DBUS_MOUNT_READONLY)) {
        dbus_release(bus, node);
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
    }
    if ((0u != (node->flags & SHREWD_BASIC_DBUS_MOUNT_EVENT)) || (NULL == node->type)) {
        dbus_release(bus, node);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 != dbus_reentrant(bus, node)) {
        dbus_release(bus, node);
        return SHREWD_BASIC_ERR_DBUS_REENTRANT;
    }
    dbus_lock(bus);
    dbus_req_init(&req, path, field, type, (*node->pver));
    dbus_unlock(bus);
    status = dbus_run_pre(bus, node, &req, 0);
    if (SHREWD_BASIC_ERR_OK == status) {
        if (0 != req.rewritten) {
            payload = req.rewrite;
            payload_size = req.rewrite_size;
        }
        if (payload_size != want) {
            status = SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        } else if ((NULL != node->ops) && (NULL != node->ops->set)) {
            status = node->ops->set(node->ops_ctx, &req, payload, payload_size);
        } else if (NULL != node->data) {
            dbus_lock(bus);
            if ((NULL != stamp) && (0 != stamp->version) && (stamp->version != (*node->pver))) {
                dbus_unlock(bus);
                dbus_release(bus, node);
                return SHREWD_BASIC_ERR_DBUS_CONFLICT;
            }
            memcpy(at, payload, payload_size);
            (*node->pver) += 1;
            version = (*node->pver);
            dbus_unlock(bus);
        } else {
            status = SHREWD_BASIC_ERR_ARG_INVALID;
        }
    }
    if ((SHREWD_BASIC_ERR_OK == status) && (0 != version)) {
        dbus_run_post(bus, node, &req, 0);
        dbus_enqueue(bus, path, at, want, version, SHREWD_BASIC_DBUS_NOTE_CHANGE);
        if (NULL != bus->arena) {
            bus->arena->commit(bus->arena->ctx, path, at, want, version);
        }
    }
    if ((NULL != stamp) && (SHREWD_BASIC_ERR_OK == status) && (0 != version)) {
        stamp->version = version;
    }
    dbus_release(bus, node);
    return status;
}

int shrewd_basic_dbus_touch(shrewd_basic_dbus_t *bus,
                            const char *path,
                            shrewd_basic_dbus_stamp_t *stamp) {
    dbus_node_t *node = NULL;
    unsigned char *at = NULL;
    size_t want = 0;
    const shrewd_basic_reflect_type_t *type = NULL;
    char field[64];
    uint64_t version = 0;
    int status = 0;
    if (NULL == bus) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = dbus_resolve(bus, path, &node, &at, &want, &type, field, sizeof(field));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 != dbus_reentrant(bus, node)) {
        dbus_release(bus, node);
        return SHREWD_BASIC_ERR_DBUS_REENTRANT;
    }
    dbus_lock(bus);
    if ((NULL != stamp) && (0 != stamp->version) && (stamp->version != (*node->pver))) {
        dbus_unlock(bus);
        dbus_release(bus, node);
        return SHREWD_BASIC_ERR_DBUS_CONFLICT;
    }
    (*node->pver) += 1;
    version = (*node->pver);
    dbus_unlock(bus);
    dbus_enqueue(bus, path, at, want, version, SHREWD_BASIC_DBUS_NOTE_CHANGE);
    if (NULL != bus->arena) {
        bus->arena->commit(bus->arena->ctx, path, at, want, version);
    }
    if (NULL != stamp) {
        stamp->version = version;
    }
    dbus_release(bus, node);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_publish(shrewd_basic_dbus_t *bus,
                              const char *path,
                              const void *payload,
                              size_t size) {
    size_t res_len = 0;
    const char *field = NULL;
    if ((NULL == bus) || ((NULL == payload) && (0 != size))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_ERR_OK != dbus_split(path, &res_len, &field)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    dbus_enqueue(bus, path, payload, size, 0, SHREWD_BASIC_DBUS_NOTE_EVENT);
    return SHREWD_BASIC_ERR_OK;
}

/* --- observation / persistence --------------------------------------------- */

/* Pattern match against a node's resource path (field selector on the
   pattern is ignored here: dump/export work at resource granularity). */
static int dbus_match_node(const dbus_pattern_t *pat, const char *path) {
    size_t res_len = 0;
    const char *field = NULL;
    if (SHREWD_BASIC_ERR_OK != dbus_split(path, &res_len, &field)) {
        return 0;
    }
    (void)field;
    return dbus_match(pat, path, res_len);
}

int shrewd_basic_dbus_dump(shrewd_basic_dbus_t *bus, const char *pattern) {
    dbus_pattern_t pat;
    if (NULL == bus) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&pat, 0, sizeof(pat));
    if (NULL == pattern) {
        pat.subtree = 1; /* match everything */
    } else if (SHREWD_BASIC_ERR_OK != dbus_pattern_compile(pattern, &pat)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    dbus_lock(bus);
    for (dbus_node_t *node = bus->nodes; NULL != node; node = node->next) {
        if ((0 == node->dead) && (0 != dbus_match_node(&pat, node->path))) {
            node->refs += 1;
            dbus_unlock(bus);
            if ((NULL != node->type) && (NULL != node->data)) {
                shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO,
                                 shrewd_basic_dbus_log,
                                 "dump",
                                 "%s (v%llu)\n",
                                 node->path,
                                 (unsigned long long)(*node->pver));
                (void)shrewd_basic_reflect_dump(node->type, node->data, node->path);
            }
            dbus_lock(bus);
            node->refs -= 1;
        }
    }
    dbus_unlock(bus);
    return SHREWD_BASIC_ERR_OK;
}

#if SHREWD_BASIC_FORMAT_JSON

int shrewd_basic_dbus_export(shrewd_basic_dbus_t *bus, const char *pattern, char **out_json) {
    dbus_pattern_t pat;
    shrewd_basic_json_value_t *root = NULL;
    char *text = NULL;
    int status = 0;
    if ((NULL == bus) || (NULL == out_json)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_json = NULL;
    memset(&pat, 0, sizeof(pat));
    if (NULL == pattern) {
        pat.subtree = 1; /* match everything */
    } else {
        status = dbus_pattern_compile(pattern, &pat);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &root);
    dbus_lock(bus);
    for (dbus_node_t *node = bus->nodes; (NULL != node) && (SHREWD_BASIC_ERR_OK == status);
         node = node->next) {
        if ((0 != node->dead) || (0 == dbus_match_node(&pat, node->path)) || (NULL == node->type)
            || (NULL == node->data)) {
            continue;
        }
        node->refs += 1;
        dbus_unlock(bus);
        {
            shrewd_basic_json_value_t *val = NULL;
            status = shrewd_basic_json_encode_value(node->data, node->type, NULL, &val, NULL, NULL);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = shrewd_basic_json_set(root, node->path, val);
                if (SHREWD_BASIC_ERR_OK != status) {
                    shrewd_basic_json_value_free(val);
                }
            }
        }
        dbus_lock(bus);
        node->refs -= 1;
    }
    dbus_unlock(bus);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_json_serialize(root, &text, NULL, NULL);
    }
    shrewd_basic_json_value_free(root);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    *out_json = text;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_import(shrewd_basic_dbus_t *bus,
                             const char *pattern,
                             const char *json,
                             size_t length) {
    dbus_pattern_t pat;
    shrewd_basic_json_value_t *root = NULL;
    int status = 0;
    if ((NULL == bus) || (NULL == json)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&pat, 0, sizeof(pat));
    if (NULL == pattern) {
        pat.subtree = 1; /* match everything */
    } else {
        status = dbus_pattern_compile(pattern, &pat);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    status = shrewd_basic_json_parse(json, length, &root, NULL, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    /* Bulk initialization: commits values without hooks or notify. */
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < shrewd_basic_json_count(root));
         ++i) {
        const char *key = shrewd_basic_json_key_at(root, i);
        const shrewd_basic_json_value_t *val = shrewd_basic_json_at(root, i);
        size_t res_len = 0;
        const char *field = NULL;
        dbus_node_t *node = NULL;
        if ((NULL == key) || (SHREWD_BASIC_ERR_OK != dbus_split(key, &res_len, &field))
            || (0 == dbus_match_node(&pat, key)) || (NULL != field)) {
            continue;
        }
        dbus_lock(bus);
        status = shrewd_basic_radix_search(bus->exact, key, (void **)&node);
        if ((SHREWD_BASIC_ERR_OK == status)
            && ((0 != node->dead) || (NULL == node->type) || (NULL == node->data))) {
            status = SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            node->refs += 1;
        }
        dbus_unlock(bus);
        if (SHREWD_BASIC_ERR_OK != status) {
            status = SHREWD_BASIC_ERR_OK; /* unmatched keys are skipped */
            continue;
        }
        if (0 != dbus_reentrant(bus, node)) {
            status = SHREWD_BASIC_ERR_DBUS_REENTRANT;
        } else if (SHREWD_BASIC_ERR_OK
                   == shrewd_basic_json_decode_value(
                       val, node->data, node->type, NULL, NULL, NULL)) {
            dbus_lock(bus);
            (*node->pver) += 1;
            dbus_unlock(bus);
        }
        dbus_release(bus, node);
    }
    shrewd_basic_json_value_free(root);
    return status;
}

int shrewd_basic_dbus_save(shrewd_basic_dbus_t *bus, const char *pattern, const char *file) {
    char *text = NULL;
    FILE *out = NULL;
    int status = 0;
    if ((NULL == bus) || (NULL == file)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = shrewd_basic_dbus_export(bus, pattern, &text);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    out = fopen(file, "w");
    if (NULL == out) {
        shrewd_basic_json_text_free(text);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    (void)fputs(text, out);
    (void)fclose(out);
    shrewd_basic_json_text_free(text);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_load(shrewd_basic_dbus_t *bus, const char *pattern, const char *file) {
    FILE *in = NULL;
    char *text = NULL;
    size_t cap = 4096;
    size_t len = 0;
    int status = 0;
    if ((NULL == bus) || (NULL == file)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    in = fopen(file, "r");
    if (NULL == in) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&text, cap)) {
        (void)fclose(in);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    for (;;) {
        size_t got = fread(text + len, 1, cap - len - 1, in);
        len += got;
        if (got < (cap - len - 1)) {
            break; /* short read: eof or error */
        }
        {
            void *old = text;
            void *bigger = NULL;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&bigger, old, cap * 2)) {
                (void)fclose(in);
                (void)shrewd_basic_platform_free(&old);
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            text = bigger;
            cap *= 2;
        }
    }
    (void)fclose(in);
    text[len] = '\0';
    status = shrewd_basic_dbus_import(bus, pattern, text, len);
    {
        void *old = text;
        (void)shrewd_basic_platform_free(&old);
    }
    return status;
}

#else /* !SHREWD_BASIC_FORMAT_JSON */

int shrewd_basic_dbus_export(shrewd_basic_dbus_t *bus, const char *pattern, char **out_json) {
    (void)bus;
    (void)pattern;
    (void)out_json;
    return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
}

int shrewd_basic_dbus_import(shrewd_basic_dbus_t *bus,
                             const char *pattern,
                             const char *json,
                             size_t length) {
    (void)bus;
    (void)pattern;
    (void)json;
    (void)length;
    return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
}

int shrewd_basic_dbus_save(shrewd_basic_dbus_t *bus, const char *pattern, const char *file) {
    (void)bus;
    (void)pattern;
    (void)file;
    return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
}

int shrewd_basic_dbus_load(shrewd_basic_dbus_t *bus, const char *pattern, const char *file) {
    (void)bus;
    (void)pattern;
    (void)file;
    return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
}

#endif /* SHREWD_BASIC_FORMAT_JSON */

/* --- transactions (optimistic read-modify-write, docs §12.2) ------------ */

#define DBUS_TXN_MAX 32

struct shrewd_basic_dbus_txn {
    shrewd_basic_dbus_t *bus;
    struct {
        char path[DBUS_PATH_MAX];
        uint64_t version;
    } reads[DBUS_TXN_MAX];
    size_t read_count;
    struct {
        char path[DBUS_PATH_MAX];
        void *value;
        size_t size;
    } writes[DBUS_TXN_MAX];
    size_t write_count;
};

int shrewd_basic_dbus_txn_begin(shrewd_basic_dbus_t *bus, shrewd_basic_dbus_txn_t **out) {
    shrewd_basic_dbus_txn_t *txn = NULL;
    if ((NULL == bus) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&txn, 1, sizeof(*txn))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    txn->bus = bus;
    *out = txn;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_txn_get(shrewd_basic_dbus_txn_t *txn,
                              const char *path,
                              void *out,
                              size_t size) {
    shrewd_basic_dbus_stamp_t stamp;
    int status = 0;
    if ((NULL == txn) || (NULL == path) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((txn->read_count >= DBUS_TXN_MAX) || (txn->write_count >= DBUS_TXN_MAX)) {
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED; /* table full */
    }
    memset(&stamp, 0, sizeof(stamp));
    status = shrewd_basic_dbus_get(txn->bus, path, out, size, &stamp);
    if (SHREWD_BASIC_ERR_OK == status) {
        snprintf(
            txn->reads[txn->read_count].path, sizeof(txn->reads[txn->read_count].path), "%s", path);
        txn->reads[txn->read_count].version = stamp.version;
        txn->read_count += 1;
    }
    return status;
}

int shrewd_basic_dbus_txn_set(shrewd_basic_dbus_txn_t *txn,
                              const char *path,
                              const void *in,
                              size_t size) {
    void *copy = NULL;
    if ((NULL == txn) || (NULL == path) || (NULL == in)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (txn->write_count >= DBUS_TXN_MAX) {
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
    }
    /* Replace a staged write for the same path (keep the newest). */
    for (size_t i = 0; i < txn->write_count; ++i) {
        if (0 == strcmp(txn->writes[i].path, path)) {
            void *old = txn->writes[i].value;
            (void)shrewd_basic_platform_free(&old);
            txn->writes[i].value = NULL;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&copy, (0 < size) ? size : 1)) {
                txn->writes[i].size = 0;
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            memcpy(copy, in, size);
            txn->writes[i].value = copy;
            txn->writes[i].size = size;
            return SHREWD_BASIC_ERR_OK;
        }
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&copy, (0 < size) ? size : 1)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    memcpy(copy, in, size);
    snprintf(
        txn->writes[txn->write_count].path, sizeof(txn->writes[txn->write_count].path), "%s", path);
    txn->writes[txn->write_count].value = copy;
    txn->writes[txn->write_count].size = size;
    txn->write_count += 1;
    return SHREWD_BASIC_ERR_OK;
}

static void txn_free(shrewd_basic_dbus_txn_t *txn) {
    for (size_t i = 0; i < txn->write_count; ++i) {
        void *old = txn->writes[i].value;
        (void)shrewd_basic_platform_free(&old);
    }
    {
        void *old = txn;
        (void)shrewd_basic_platform_free(&old);
    }
}

int shrewd_basic_dbus_txn_commit(shrewd_basic_dbus_txn_t *txn) {
    const char *paths[DBUS_TXN_MAX];
    const void *ins[DBUS_TXN_MAX];
    size_t sizes[DBUS_TXN_MAX];
    shrewd_basic_dbus_stamp_t stamps[DBUS_TXN_MAX];
    int status = 0;
    if (NULL == txn) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == txn->write_count) {
        txn_free(txn);
        return SHREWD_BASIC_ERR_OK;
    }
    /* CAS base: a written resource read in this txn commits against the
       observed version; write-only resources are unconditional. */
    for (size_t i = 0; i < txn->write_count; ++i) {
        paths[i] = txn->writes[i].path;
        ins[i] = txn->writes[i].value;
        sizes[i] = txn->writes[i].size;
        memset(&stamps[i], 0, sizeof(stamps[i]));
        for (size_t j = 0; j < txn->read_count; ++j) {
            if (0 == strcmp(txn->reads[j].path, txn->writes[i].path)) {
                stamps[i].version = txn->reads[j].version;
                break;
            }
        }
    }
    status = shrewd_basic_dbus_set_many(txn->bus, txn->write_count, paths, ins, sizes, stamps);
    txn_free(txn);
    return status;
}

void shrewd_basic_dbus_txn_abort(shrewd_basic_dbus_txn_t *txn) {
    if (NULL != txn) {
        txn_free(txn);
    }
}

/* --- batch / coalesce / remote notify (phase 3) --------------------------- */

int shrewd_basic_dbus_get_many(shrewd_basic_dbus_t *bus,
                               size_t count,
                               const char *const *paths,
                               void *const *outs,
                               const size_t *sizes,
                               shrewd_basic_dbus_stamp_t *stamps) {
    /* Sequential reads: not globally atomic - already fetched entries
       are kept when a later one fails. */
    if ((NULL == bus) || ((0 != count) && ((NULL == paths) || (NULL == outs) || (NULL == sizes)))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < count; ++i) {
        int status = shrewd_basic_dbus_get(
            bus, paths[i], outs[i], sizes[i], (NULL != stamps) ? &stamps[i] : NULL);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_set_many(shrewd_basic_dbus_t *bus,
                               size_t count,
                               const char *const *paths,
                               const void *const *ins,
                               const size_t *sizes,
                               shrewd_basic_dbus_stamp_t *stamps) {
    typedef struct {
        dbus_node_t *node;
        unsigned char *at;
        size_t want;
        char field[64];
        shrewd_basic_dbus_req_t req;
        const void *payload;
        size_t psize;
        uint64_t version;
    } entry_t;
    entry_t *ents = NULL;
    int status = 0;
    size_t done = 0;
    if ((NULL == bus) || ((0 != count) && ((NULL == paths) || (NULL == ins) || (NULL == sizes)))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == count) {
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&ents, count, sizeof(*ents))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    /* Phase 1: resolve and run pre_set hooks outside the lock. */
    for (done = 0; done < count; ++done) {
        entry_t *e = &ents[done];
        status = dbus_resolve(
            bus, paths[done], &e->node, &e->at, &e->want, &e->req.type, e->field, sizeof(e->field));
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        if ((0u != (e->node->flags & SHREWD_BASIC_DBUS_MOUNT_READONLY))
            || (0u != (e->node->flags & SHREWD_BASIC_DBUS_MOUNT_EVENT))
            || (NULL == e->node->type)) {
            status = SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
            break;
        }
        if (0 != dbus_reentrant(bus, e->node)) {
            status = SHREWD_BASIC_ERR_DBUS_REENTRANT;
            break;
        }
        if ((NULL != e->node->ops) && (NULL != e->node->ops->set)) {
            /* hook-committed nodes are not part of the atomic guarantee */
            status = SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
            break;
        }
        dbus_lock(bus);
        dbus_req_init(&e->req, paths[done], e->field, e->req.type, (*e->node->pver));
        dbus_unlock(bus);
        status = dbus_run_pre(bus, e->node, &e->req, 0);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        e->payload = (0 != e->req.rewritten) ? e->req.rewrite : ins[done];
        e->psize = (0 != e->req.rewritten) ? e->req.rewrite_size : sizes[done];
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        /* Phase 2: validate everything, then commit everything - one
           lock span, so the batch is all-or-nothing. */
        dbus_lock(bus);
        for (size_t i = 0; i < count; ++i) {
            entry_t *e = &ents[i];
            if (e->psize != e->want) {
                status = SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                break;
            }
            if ((NULL != stamps) && (0 != stamps[i].version)
                && (stamps[i].version != (*e->node->pver))) {
                status = SHREWD_BASIC_ERR_DBUS_CONFLICT;
                break;
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            for (size_t i = 0; i < count; ++i) {
                entry_t *e = &ents[i];
                memcpy(e->at, e->payload, e->psize);
                (*e->node->pver) += 1;
                e->version = (*e->node->pver);
            }
        }
        dbus_unlock(bus);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        for (size_t i = 0; i < done; ++i) {
            dbus_release(bus, ents[i].node);
        }
        {
            void *old = ents;
            (void)shrewd_basic_platform_free(&old);
        }
        return status;
    }
    /* Phase 3: posts and notifications only on success (zero on fail). */
    for (size_t i = 0; i < count; ++i) {
        entry_t *e = &ents[i];
        dbus_run_post(bus, e->node, &e->req, 0);
        dbus_enqueue(bus, paths[i], e->at, e->want, e->version, SHREWD_BASIC_DBUS_NOTE_CHANGE);
        if (NULL != bus->arena) {
            bus->arena->commit(bus->arena->ctx, paths[i], e->at, e->want, e->version);
        }
        if (NULL != stamps) {
            stamps[i].version = e->version;
        }
        dbus_release(bus, e->node);
    }
    {
        void *old = ents;
        (void)shrewd_basic_platform_free(&old);
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_coalesce(shrewd_basic_dbus_t *bus, int on) {
    if (NULL == bus) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    dbus_lock(bus);
    bus->coalesce = (0 != on) ? 1 : 0;
    dbus_unlock(bus);
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_dbus_notify_remote(shrewd_basic_dbus_t *bus, const char *path, uint64_t seq) {
    dbus_node_t *node = NULL;
    unsigned char *at = NULL;
    size_t want = 0;
    const shrewd_basic_reflect_type_t *type = NULL;
    char field[64];
    if ((NULL == bus) || (NULL == path)) {
        return;
    }
    /* Local subscribers only; snapshot read live from shared storage. */
    if (SHREWD_BASIC_ERR_OK
        != dbus_resolve(bus, path, &node, &at, &want, &type, field, sizeof(field))) {
        return;
    }
    dbus_enqueue(bus, path, at, want, seq, SHREWD_BASIC_DBUS_NOTE_CHANGE);
    dbus_release(bus, node);
}

int shrewd_basic_dbus_drain(shrewd_basic_dbus_t *bus) {
    if (NULL == bus) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    dbus_lock(bus);
    while ((NULL != bus->jobs_head) || (0 < bus->active)) {
        (void)pthread_cond_wait(&bus->idle, &bus->lock);
    }
    dbus_unlock(bus);
    return SHREWD_BASIC_ERR_OK;
}

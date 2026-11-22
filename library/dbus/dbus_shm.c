#define _POSIX_C_SOURCE 200809L

/**
 * @file dbus_shm.c
 * @brief shm transport for sb_dbus (docs/dbus-design.md section 10):
 *        values live in a named shared region under a POSIX robust
 *        mutex; a change ring plus a process-shared condition variable
 *        wakes the other attached processes, whose own dispatchers
 *        synthesize notifications for their own subscribers.
 *
 * Note: the brief sketched an eventfd registry, but file descriptors
 * are per-process - cross-process wake uses a process-shared
 * pthread_cond_t instead (same semantics, fewer kernel objects).
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "shrewd-basic-internal/dbus-internal.h"
#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

#define SHM_MAGIC 0x31424453u /* "SDB1" */
#define SHM_FORMAT 2u
#define SHM_TYPES 64
#define SHM_SLOTS 128
#define SHM_RING 64
#define SHM_PATH_MAX 128
#define SHM_TYPE_NAME 64

typedef struct {
    char name[SHM_TYPE_NAME];
    uint64_t abi;
    uint64_t size;
} shm_type_t;

typedef struct {
    char path[SHM_PATH_MAX];
    uint32_t type_idx;
    uint32_t in_use;
    uint64_t seq;
    uint32_t offset; /* into the pool */
    uint32_t size;
} shm_slot_t;

typedef struct {
    char path[SHM_PATH_MAX];
    uint64_t seq;
    int32_t writer; /* pid */
    uint32_t size;
} shm_change_t;

typedef struct {
    uint32_t magic;
    uint32_t format;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    uint32_t type_count;
    uint32_t slot_count;
    uint64_t pool_used;
    uint64_t ring_pos;
    uint64_t ring_cap; /* change-ring capacity, fixed at creation */
    shm_type_t types[SHM_TYPES];
    shm_slot_t slots[SHM_SLOTS];
    shm_change_t ring[]; /* ring_cap entries, then the value pool */
} shm_hdr_t;

typedef struct {
    int fd;
    size_t map_size;
    shm_hdr_t *hdr;
    unsigned char *pool;
    size_t pool_cap;
    shrewd_basic_dbus_t *bus;
    pthread_t wake_thread;
    int wake_made;
    int running;
    uint64_t seen;
    shrewd_basic_dbus_arena_t arena;
} shm_ctx_t;

/* --- robust lock ---------------------------------------------------------- */

static void shm_lock(shm_hdr_t *hdr) {
    for (;;) {
        int err = pthread_mutex_lock(&hdr->lock);
        if (0 == err) {
            return;
        }
        if (EOWNERDEAD == err) {
            /* the dead owner's half-committed work is dropped (v1) */
            (void)pthread_mutex_consistent(&hdr->lock);
            return;
        }
        if (ENOTRECOVERABLE == err) {
            return; /* unusable mutex: fail loudly at the call sites */
        }
        (void)sched_yield();
    }
}

static void shm_unlock(shm_hdr_t *hdr) {
    (void)pthread_mutex_unlock(&hdr->lock);
}

/* --- arena ---------------------------------------------------------------- */

static int shm_val_acquire(void *vctx,
                           const char *path,
                           const shrewd_basic_reflect_type_t *type,
                           void **out_data,
                           uint64_t **out_seq) {
    shm_ctx_t *c = (shm_ctx_t *)vctx;
    shm_hdr_t *hdr = c->hdr;
    size_t want = 0;
    uint32_t type_idx = UINT32_MAX;
    if ((NULL == path) || (NULL == type) || (strlen(path) >= SHM_PATH_MAX)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    want = (type->sb_refl_size + 7u) & ~(size_t)7u;
    shm_lock(hdr);
    /* Type table: register once, then fingerprint-check every mount. */
    for (uint32_t i = 0; i < hdr->type_count; ++i) {
        if (0 == strcmp(hdr->types[i].name, type->sb_refl_name)) {
            type_idx = i;
            break;
        }
    }
    if (UINT32_MAX == type_idx) {
        if (hdr->type_count >= SHM_TYPES) {
            shm_unlock(hdr);
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        type_idx = hdr->type_count;
        memset(&hdr->types[type_idx], 0, sizeof(hdr->types[type_idx]));
        snprintf(
            hdr->types[type_idx].name, sizeof(hdr->types[type_idx].name), "%s", type->sb_refl_name);
        hdr->types[type_idx].abi = (uint64_t)type->sb_refl_abi;
        hdr->types[type_idx].size = (uint64_t)type->sb_refl_size;
        hdr->type_count += 1;
    } else if (((uint64_t)type->sb_refl_abi != hdr->types[type_idx].abi)
               || ((uint64_t)type->sb_refl_size != hdr->types[type_idx].size)) {
        shm_unlock(hdr);
        return SHREWD_BASIC_ERR_DBUS_CONFLICT; /* ABI fingerprint mismatch */
    }
    /* Value slot: find by path (type must agree) or allocate. */
    for (uint32_t i = 0; i < hdr->slot_count; ++i) {
        if ((0 != hdr->slots[i].in_use) && (0 == strcmp(hdr->slots[i].path, path))) {
            if (hdr->slots[i].type_idx != type_idx) {
                shm_unlock(hdr);
                return SHREWD_BASIC_ERR_DBUS_CONFLICT;
            }
            *out_data = c->pool + hdr->slots[i].offset;
            *out_seq = &hdr->slots[i].seq;
            shm_unlock(hdr);
            return SHREWD_BASIC_ERR_OK;
        }
    }
    {
        uint32_t idx = hdr->slot_count;
        if (idx >= SHM_SLOTS) {
            shm_unlock(hdr);
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        if ((hdr->pool_used + want) > c->pool_cap) {
            shm_unlock(hdr);
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        memset(&hdr->slots[idx], 0, sizeof(hdr->slots[idx]));
        snprintf(hdr->slots[idx].path, sizeof(hdr->slots[idx].path), "%s", path);
        hdr->slots[idx].type_idx = type_idx;
        hdr->slots[idx].in_use = 1;
        hdr->slots[idx].offset = (uint32_t)hdr->pool_used;
        hdr->slots[idx].size = (uint32_t)type->sb_refl_size;
        hdr->slots[idx].seq = 1;
        hdr->pool_used += want;
        hdr->slot_count += 1;
        *out_data = c->pool + hdr->slots[idx].offset;
        *out_seq = &hdr->slots[idx].seq;
    }
    shm_unlock(hdr);
    return SHREWD_BASIC_ERR_OK;
}

static void shm_val_release(void *vctx, const char *path, void *data) {
    (void)vctx;
    (void)path;
    (void)data; /* shared slots outlive individual mounts */
}

static void shm_commit(void *vctx, const char *path, const void *value, size_t size, uint64_t seq) {
    shm_ctx_t *c = (shm_ctx_t *)vctx;
    shm_hdr_t *hdr = c->hdr;
    shm_change_t *slot = NULL;
    (void)value;
    shm_lock(hdr);
    slot = &hdr->ring[hdr->ring_pos % hdr->ring_cap];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    slot->seq = seq;
    slot->writer = (int32_t)getpid();
    slot->size = (uint32_t)size;
    hdr->ring_pos += 1;
    (void)pthread_cond_broadcast(&hdr->wake);
    shm_unlock(hdr);
}

static void shm_close(void *vctx) {
    shm_ctx_t *c = (shm_ctx_t *)vctx;
    if (NULL == c) {
        return;
    }
    c->running = 0;
    if (0 != c->wake_made) {
        shm_lock(c->hdr);
        (void)pthread_cond_broadcast(&c->hdr->wake);
        shm_unlock(c->hdr);
        (void)pthread_join(c->wake_thread, NULL);
    }
    if (NULL != c->hdr) {
        (void)munmap((void *)c->hdr, c->map_size);
    }
    if (0 <= c->fd) {
        (void)close(c->fd);
    }
    {
        void *old = c;
        (void)shrewd_basic_platform_free(&old);
    }
}

/* --- wake thread: drain the change ring into local notifications ---------- */

static void *shm_wake_main(void *arg) {
    shm_ctx_t *c = (shm_ctx_t *)arg;
    for (;;) {
        char paths[SHM_RING][SHM_PATH_MAX];
        uint64_t seqs[SHM_RING];
        size_t n = 0;
        shm_lock(c->hdr);
        while ((0 != c->running) && (c->seen >= c->hdr->ring_pos)) {
            (void)pthread_cond_wait(&c->hdr->wake, &c->hdr->lock);
        }
        if (0 == c->running) {
            shm_unlock(c->hdr);
            return NULL;
        }
        for (uint64_t pos = c->seen; (pos < c->hdr->ring_pos) && (n < SHM_RING); ++pos) {
            const shm_change_t *slot = &c->hdr->ring[pos % c->hdr->ring_cap];
            if (slot->writer == (int32_t)getpid()) {
                continue; /* own changes already notified locally */
            }
            memcpy(paths[n], slot->path, SHM_PATH_MAX);
            seqs[n] = slot->seq;
            ++n;
        }
        c->seen = c->hdr->ring_pos;
        shm_unlock(c->hdr);
        for (size_t i = 0; i < n; ++i) {
            shrewd_basic_dbus_notify_remote(c->bus, paths[i], seqs[i]);
        }
    }
}

/* --- attach --------------------------------------------------------------- */

int shrewd_basic_dbus_attach(const char *name,
                             size_t size,
                             const shrewd_basic_dbus_options_t *options,
                             shrewd_basic_dbus_t **out) {
    char full[96];
    shm_ctx_t *c = NULL;
    struct stat st;
    int creator = 0;
    int fd = -1;
    if ((NULL == name) || (NULL == out) || (0 == name[0])) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if ('/' == name[0]) {
        snprintf(full, sizeof(full), "%s", name);
    } else {
        snprintf(full, sizeof(full), "/%s", name);
    }
    if (strlen(full) >= sizeof(full) - 1) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    fd = shm_open(full, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (0 <= fd) {
        creator = 1;
    } else {
        if (EEXIST != errno) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        fd = shm_open(full, O_RDWR, 0600);
        if (0 > fd) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&c, 1, sizeof(*c))) {
        (void)close(fd);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    c->fd = fd;
    if (0 != creator) {
        size_t want = (0 != size) ? size : (size_t)(1u << 20);
        if (0 != ftruncate(fd, (off_t)want)) {
            shm_close(c);
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
    }
    if ((0 != fstat(fd, &st)) || ((size_t)st.st_size < sizeof(shm_hdr_t))) {
        shm_close(c);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    c->map_size = (size_t)st.st_size;
    c->hdr = (shm_hdr_t *)mmap(NULL, c->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (MAP_FAILED == (void *)c->hdr) {
        c->hdr = NULL;
        shm_close(c);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if (0 != creator) {
        pthread_mutexattr_t ma;
        pthread_condattr_t ca;
        size_t ring_cap = ((NULL != options) && (0 != options->ring_size)) ? options->ring_size
                                                                           : (size_t)SHM_RING;
        if (4096u < ring_cap) {
            ring_cap = 4096u;
        }
        memset(c->hdr, 0, sizeof(*c->hdr) + (ring_cap * sizeof(shm_change_t)));
        c->hdr->ring_cap = ring_cap;
        (void)pthread_mutexattr_init(&ma);
        (void)pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
        (void)pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST);
        (void)pthread_mutex_init(&c->hdr->lock, &ma);
        (void)pthread_mutexattr_destroy(&ma);
        (void)pthread_condattr_init(&ca);
        (void)pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
        (void)pthread_cond_init(&c->hdr->wake, &ca);
        (void)pthread_condattr_destroy(&ca);
        c->hdr->format = SHM_FORMAT;
        __atomic_store_n(&c->hdr->magic, SHM_MAGIC, __ATOMIC_RELEASE);
    } else {
        /* wait for the creator to finish initializing the header */
        struct timespec ts;
        int spins = 0;
        while ((SHM_MAGIC != __atomic_load_n(&c->hdr->magic, __ATOMIC_ACQUIRE)) && (spins < 2000)) {
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000; /* 1ms */
            (void)nanosleep(&ts, NULL);
            ++spins;
        }
        if (SHM_MAGIC != c->hdr->magic) {
            shm_close(c);
            return SHREWD_BASIC_ERR_DBUS_CONFLICT;
        }
        if (SHM_FORMAT != c->hdr->format) {
            shm_close(c);
            return SHREWD_BASIC_ERR_DBUS_CONFLICT;
        }
        if ((0 == c->hdr->ring_cap) || (4096u < c->hdr->ring_cap)) {
            shm_close(c);
            return SHREWD_BASIC_ERR_DBUS_CONFLICT;
        }
    }
    /* The value pool follows the header and its variable-size ring. */
    {
        uintptr_t base
            = (uintptr_t)c->hdr->ring + ((uintptr_t)c->hdr->ring_cap * sizeof(shm_change_t));
        base = (base + 7u) & ~(uintptr_t)7u;
        if ((base + 8u) > ((uintptr_t)c->hdr + c->map_size)) {
            shm_close(c);
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        c->pool = (unsigned char *)base;
        c->pool_cap = (size_t)(((uintptr_t)c->hdr + c->map_size) - base);
    }
    c->running = 1;
    c->seen = c->hdr->ring_pos;
    c->arena.ctx = c;
    c->arena.val_acquire = shm_val_acquire;
    c->arena.val_release = shm_val_release;
    c->arena.commit = shm_commit;
    c->arena.close = shm_close;
    if (0 != pthread_create(&c->wake_thread, NULL, shm_wake_main, c)) {
        shm_close(c);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    c->wake_made = 1;
    {
        int status = shrewd_basic_dbus_create_arena(options, &c->arena, &c->bus);
        if (SHREWD_BASIC_ERR_OK != status) {
            shm_close(c);
            return status;
        }
    }
    *out = c->bus;
    return SHREWD_BASIC_ERR_OK;
}

#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/platform/thread.h"
#include "shrewd-basic/platform/memory.h"

/*
 * Decode allocation registry: every block decode allocates is tracked
 * per decoded root, so cleanup frees exactly what decode created - pre-
 * set defaults (static literals or caller allocations) are never
 * touched, and refill/overwrite can release its own previous content.
 *
 * The root -> owner mapping lives in a small static table (like the
 * reflect lookup cache: no locking, single-threaded use assumed).
 */
#define JSON_OWNER_SLOTS 16

struct json_owner_block {
    void *block;
    struct json_owner_block *next;
};

struct json_owner {
    struct json_owner_block *first;
};

static struct {
    const void *root;
    json_owner_t *owner;
} g_json_owners[JSON_OWNER_SLOTS];

static size_t g_json_owner_next = 0;

static json_owner_t **json_owner_slot(const void *root)
{
    for (size_t i = 0; i < JSON_OWNER_SLOTS; ++i) {
        if (root == g_json_owners[i].root) {
            return &g_json_owners[i].owner;
        }
    }
    return NULL;
}

int json_owner_touch_locked(const void *root)
{
    json_owner_t **slot = json_owner_slot(root);
    if (NULL == slot) {
        for (size_t i = 0; i < JSON_OWNER_SLOTS; ++i) {
            if (NULL == g_json_owners[i].owner) {
                slot = &g_json_owners[i].owner;
                g_json_owners[i].root = root;
                break;
            }
        }
        if (NULL == slot) {
            slot = &g_json_owners[g_json_owner_next].owner;
            g_json_owners[g_json_owner_next].root = root;
            g_json_owner_next = (g_json_owner_next + 1) % JSON_OWNER_SLOTS;
        }
    }
    if (NULL == *slot) {
        void *fresh = NULL;
        if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_calloc(&fresh, 1, sizeof(**slot))) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        *slot = fresh;
    }
    return SHREWD_BASIC_ERR_OK;
}

int json_owner_calloc_locked(const void *root, void **out, size_t size)
{
    json_owner_t **slot = json_owner_slot(root);
    json_owner_t *owner = NULL;
    struct json_owner_block *entry = NULL;
    void *block = NULL;
    int status = json_owner_touch_locked(root);
    *out = NULL;
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    slot = json_owner_slot(root);
    owner = *slot;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, size)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_alloc((void **)&entry, sizeof(*entry))) {
        (void)shrewd_basic_platform_free(&block);
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    entry->block = block;
    entry->next = owner->first;
    owner->first = entry;
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

int json_owner_owns_locked(const void *root, const void *block)
{
    json_owner_t **slot = json_owner_slot(root);
    if ((NULL == slot) || (NULL == *slot) || (NULL == block)) {
        return 0;
    }
    for (struct json_owner_block *entry = (*slot)->first; NULL != entry;
         entry = entry->next) {
        if (entry->block == block) {
            return 1;
        }
    }
    return 0;
}

int json_owner_release_locked(const void *root, void **block)
{
    json_owner_t **slot = json_owner_slot(root);
    json_owner_t *owner = (NULL != slot) ? *slot : NULL;
    struct json_owner_block **link = (NULL != owner) ? &owner->first : NULL;
    if ((NULL == link) || (NULL == *block)) {
        return SHREWD_BASIC_ERR_OK;
    }
    while (NULL != *link) {
        if ((*link)->block == *block) {
            struct json_owner_block *entry = *link;
            *link = entry->next;
            (void)shrewd_basic_platform_free(&entry->block);
            (void)shrewd_basic_platform_free((void **)&entry);
            *block = NULL;
            return SHREWD_BASIC_ERR_OK;
        }
        link = &(*link)->next;
    }
    return SHREWD_BASIC_ERR_OK; /* not ours: leave it alone */
}

void json_owner_release_all_locked(const void *root)
{
    json_owner_t **slot = json_owner_slot(root);
    if ((NULL == slot) || (NULL == *slot)) {
        return;
    }
    while (NULL != (*slot)->first) {
        struct json_owner_block *entry = (*slot)->first;
        (*slot)->first = entry->next;
        (void)shrewd_basic_platform_free(&entry->block);
        (void)shrewd_basic_platform_free((void **)&entry);
    }
    (void)shrewd_basic_platform_free((void **)slot);
    for (size_t i = 0; i < JSON_OWNER_SLOTS; ++i) {
        if (root == g_json_owners[i].root) {
            g_json_owners[i].root = NULL;
        }
    }
}

/* Locked views: the registry is the one global table this module owns. */
int json_owner_touch(const void *root)
{
    int status = 0;
    shrewd_basic_platform_lock();
    status = json_owner_touch_locked(root);
    shrewd_basic_platform_unlock();
    return status;
}

int json_owner_calloc(const void *root, void **out, size_t size)
{
    int status = 0;
    shrewd_basic_platform_lock();
    status = json_owner_calloc_locked(root, out, size);
    shrewd_basic_platform_unlock();
    return status;
}

int json_owner_owns(const void *root, const void *block)
{
    int status = 0;
    shrewd_basic_platform_lock();
    status = json_owner_owns_locked(root, block);
    shrewd_basic_platform_unlock();
    return status;
}

int json_owner_release(const void *root, void **block)
{
    int status = 0;
    shrewd_basic_platform_lock();
    status = json_owner_release_locked(root, block);
    shrewd_basic_platform_unlock();
    return status;
}

void json_owner_release_all(const void *root)
{
    shrewd_basic_platform_lock();
    json_owner_release_all_locked(root);
    shrewd_basic_platform_unlock();
}

#include "shrewd-basic/ds/trie.h"

#include <string.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared ds log module, defined in library/ds/ds.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log);

static shrewd_basic_trie_node_t *trie_node_new(void)
{
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_trie_node_t))) {
        return NULL;
    }
    return block;
}

static shrewd_basic_trie_link_t *trie_link_new(unsigned char ch, shrewd_basic_trie_node_t *child)
{
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_trie_link_t))) {
        return NULL;
    }
    shrewd_basic_trie_link_t *link = block;
    link->ch = ch;
    link->child = child;
    return link;
}

/* Slot where a link labeled @p ch belongs; children are sorted by byte. */
static shrewd_basic_trie_link_t **trie_link_slot(shrewd_basic_trie_node_t *node, unsigned char ch)
{
    shrewd_basic_trie_link_t **slot = &node->children;
    while ((NULL != *slot) && ((*slot)->ch < ch)) {
        slot = &(*slot)->next;
    }
    return slot;
}

static int trie_free_node(shrewd_basic_trie_node_t *node)
{
    shrewd_basic_trie_link_t *link = node->children;
    while (NULL != link) {
        shrewd_basic_trie_link_t *next = link->next;
        int status = trie_free_node(link->child);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = shrewd_basic_platform_free((void **)&link);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        link = next;
    }
    return shrewd_basic_platform_free((void **)&node);
}

/*
 * Recursive removal: drops the value at the end of @p key and prunes the
 * dead leaf chain left behind on the way out.
 */
static int trie_remove_node(shrewd_basic_trie_node_t *node, const unsigned char *key, void **out)
{
    if ('\0' == *key) {
        if (!node->has_value) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        if (NULL != out) {
            *out = node->value;
        }
        node->value = NULL;
        node->has_value = false;
        return SHREWD_BASIC_ERR_OK;
    }
    shrewd_basic_trie_link_t **slot = trie_link_slot(node, *key);
    if ((NULL == *slot) || ((*slot)->ch != *key)) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    shrewd_basic_trie_link_t *link = *slot;
    int status = trie_remove_node(link->child, key + 1, out);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (!link->child->has_value && (NULL == link->child->children)) {
        int free_status = shrewd_basic_platform_free((void **)&link->child);
        if (SHREWD_BASIC_ERR_OK != free_status) {
            return free_status;
        }
        *slot = link->next;
        return shrewd_basic_platform_free((void **)&link);
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Depth-first walk appending one byte per link into buf; keys in byte order. */
static int trie_walk(const shrewd_basic_trie_node_t *node,
                     char *buf,
                     size_t len,
                     shrewd_basic_trie_visit_fn visit,
                     void *ctx)
{
    if (node->has_value) {
        buf[len] = '\0';
        int status = visit(buf, node->value, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    for (const shrewd_basic_trie_link_t *link = node->children; NULL != link; link = link->next) {
        buf[len] = (char)link->ch;
        int status = trie_walk(link->child, buf, len + 1, visit, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_trie_create(shrewd_basic_trie_t **out)
{
    if (NULL == out) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "trie", "create: out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_trie_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "create: allocation failed\n");
        return status;
    }
    shrewd_basic_trie_t *trie = block;
    status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_trie_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "create: root allocation failed\n");
        (void)shrewd_basic_platform_free((void **)&trie);
        return status;
    }
    trie->root = block;
    *out = trie;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "trie", "create ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_trie_destroy(shrewd_basic_trie_t *trie)
{
    if (NULL == trie) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "trie", "destroy: trie is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = trie_free_node(trie->root);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "destroy: node free failed\n");
        return status;
    }
    status = shrewd_basic_platform_free((void **)&trie);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "destroy: trie free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "trie", "destroy ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_trie_insert(shrewd_basic_trie_t *trie, const char *key, void *value)
{
    if ((NULL == trie) || (NULL == key)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "trie",
                         "insert: trie or key is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_trie_node_t *node = trie->root;
    for (const unsigned char *p = (const unsigned char *)key; '\0' != *p; ++p) {
        shrewd_basic_trie_link_t **slot = trie_link_slot(node, *p);
        if ((NULL == *slot) || ((*slot)->ch != *p)) {
            shrewd_basic_trie_node_t *child = trie_node_new();
            if (NULL == child) {
                shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                                 shrewd_basic_ds_log,
                                 "trie",
                                 "insert: allocation failed\n");
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            shrewd_basic_trie_link_t *link = trie_link_new(*p, child);
            if (NULL == link) {
                (void)shrewd_basic_platform_free((void **)&child);
                shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                                 shrewd_basic_ds_log,
                                 "trie",
                                 "insert: allocation failed\n");
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            link->next = *slot;
            *slot = link;
            node = child;
        } else {
            node = (*slot)->child;
        }
    }
    if (node->has_value) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "trie", "insert: key exists\n");
        return SHREWD_BASIC_ERR_KEY_EXISTS;
    }
    node->value = value;
    node->has_value = true;
    ++trie->size;
    size_t len = strlen(key);
    if (len > trie->max_key_len) {
        trie->max_key_len = len;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_ds_log,
                     "trie",
                     "insert ok, size %zu\n",
                     trie->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_trie_search(const shrewd_basic_trie_t *trie, const char *key, void **out)
{
    if ((NULL == trie) || (NULL == key) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "trie",
                         "search: trie, key or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_trie_node_t *node = trie->root;
    for (const unsigned char *p = (const unsigned char *)key; '\0' != *p; ++p) {
        const shrewd_basic_trie_link_t *link = node->children;
        while ((NULL != link) && (link->ch < *p)) {
            link = link->next;
        }
        if ((NULL == link) || (link->ch != *p)) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                             shrewd_basic_ds_log,
                             "trie",
                             "search: key not found\n");
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        node = link->child;
    }
    if (!node->has_value) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "trie", "search: key not found\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = node->value;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_trie_remove(shrewd_basic_trie_t *trie, const char *key, void **out)
{
    if ((NULL == trie) || (NULL == key)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "trie",
                         "remove: trie or key is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = trie_remove_node(trie->root, (const unsigned char *)key, out);
    if (SHREWD_BASIC_ERR_OK == status) {
        --trie->size;
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                         shrewd_basic_ds_log,
                         "trie",
                         "remove ok, size %zu\n",
                         trie->size);
    } else if (SHREWD_BASIC_ERR_KEY_NOTFOUND == status) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "trie", "remove: key not found\n");
    } else {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "remove: node free failed\n");
    }
    return status;
}

int shrewd_basic_trie_foreach(const shrewd_basic_trie_t *trie,
                              shrewd_basic_trie_visit_fn visit,
                              void *ctx)
{
    if ((NULL == trie) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach: trie or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, trie->max_key_len + 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach: buffer allocation failed\n");
        return status;
    }
    char *buf = block;
    status = trie_walk(trie->root, buf, 0, visit, ctx);
    int free_status = shrewd_basic_platform_free(&block);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach: visit aborted (%s)\n",
                         shrewd_basic_error_name(status));
        return status;
    }
    if (SHREWD_BASIC_ERR_OK != free_status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach: buffer free failed\n");
        return free_status;
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_trie_foreach_prefix(const shrewd_basic_trie_t *trie,
                                     const char *prefix,
                                     shrewd_basic_trie_visit_fn visit,
                                     void *ctx)
{
    if ((NULL == trie) || (NULL == prefix) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach_prefix: trie, prefix or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_trie_node_t *node = trie->root;
    for (const unsigned char *p = (const unsigned char *)prefix; '\0' != *p; ++p) {
        const shrewd_basic_trie_link_t *link = node->children;
        while ((NULL != link) && (link->ch < *p)) {
            link = link->next;
        }
        if ((NULL == link) || (link->ch != *p)) {
            return SHREWD_BASIC_ERR_OK;
        }
        node = link->child;
    }
    size_t prefix_len = strlen(prefix);
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, trie->max_key_len + 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach_prefix: buffer allocation failed\n");
        return status;
    }
    char *buf = block;
    memcpy(buf, prefix, prefix_len);
    status = trie_walk(node, buf, prefix_len, visit, ctx);
    int free_status = shrewd_basic_platform_free(&block);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach_prefix: visit aborted (%s)\n",
                         shrewd_basic_error_name(status));
        return status;
    }
    if (SHREWD_BASIC_ERR_OK != free_status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "trie",
                         "foreach_prefix: buffer free failed\n");
        return free_status;
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_trie_size(const shrewd_basic_trie_t *trie, size_t *out)
{
    if ((NULL == trie) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "trie",
                         "size: trie or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = trie->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_trie_empty(const shrewd_basic_trie_t *trie)
{
    return (NULL == trie) || (0 == trie->size);
}

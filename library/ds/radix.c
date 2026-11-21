#include "shrewd-basic/ds/radix.h"

#include <string.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared ds log module, defined in library/ds/ds.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log);

static shrewd_basic_radix_node_t *radix_node_new(void) {
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_radix_node_t))) {
        return NULL;
    }
    return block;
}

/* Owned NUL-terminated copy of @p len bytes (NUL is kept out of the length). */
static char *radix_label_new(const char *src, size_t len) {
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, len + 1)) {
        return NULL;
    }
    char *label = block;
    memcpy(label, src, len);
    label[len] = '\0';
    return label;
}

/* Owned concatenation of two labels. */
static char *radix_label_concat(const char *a, size_t alen, const char *b, size_t blen) {
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, alen + blen + 1)) {
        return NULL;
    }
    char *label = block;
    memcpy(label, a, alen);
    memcpy(label + alen, b, blen);
    label[alen + blen] = '\0';
    return label;
}

/*
 * How many leading bytes the edge label shares with @p key; stops at the
 * end of either side, so the result is <= edge->len and <= strlen(key).
 */
static size_t radix_match(const shrewd_basic_radix_edge_t *edge, const unsigned char *key) {
    size_t i = 0;
    while ((i < edge->len) && ('\0' != key[i]) && ((unsigned char)edge->label[i] == key[i])) {
        ++i;
    }
    return i;
}

/* Slot where an edge starting with @p first belongs; edges are sorted. */
static shrewd_basic_radix_edge_t **radix_edge_slot(shrewd_basic_radix_node_t *node,
                                                   unsigned char first) {
    shrewd_basic_radix_edge_t **slot = &node->edges;
    while ((NULL != *slot) && ((unsigned char)(*slot)->label[0] < first)) {
        slot = &(*slot)->next;
    }
    return slot;
}

static int radix_free_node(shrewd_basic_radix_node_t *node) {
    shrewd_basic_radix_edge_t *edge = node->edges;
    while (NULL != edge) {
        shrewd_basic_radix_edge_t *next = edge->next;
        int status = radix_free_node(edge->child);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = shrewd_basic_platform_free((void **)&edge->label);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = shrewd_basic_platform_free((void **)&edge);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        edge = next;
    }
    return shrewd_basic_platform_free((void **)&node);
}

/*
 * Recursive removal. On the way out every traversed edge is re-compressed:
 * a dead leaf edge is pruned, and a valueless single-child node is merged
 * into its parent edge. Merging needs one fresh label; when that allocation
 * fails the tree is left functional (only un-compressed) and the failure is
 * reported as SHREWD_BASIC_ERR_OK with a log line.
 */
static int radix_remove_rec(shrewd_basic_radix_node_t *node, const unsigned char *key, void **out) {
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
    shrewd_basic_radix_edge_t **slot = radix_edge_slot(node, *key);
    shrewd_basic_radix_edge_t *edge = *slot;
    if ((NULL == edge) || ((unsigned char)edge->label[0] != *key)) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    size_t m = radix_match(edge, key);
    if (m != edge->len) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    int status = radix_remove_rec(edge->child, key + m, out);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (;;) {
        shrewd_basic_radix_node_t *child = edge->child;
        if (child->has_value) {
            break;
        }
        if (NULL == child->edges) {
            /* Dead leaf: drop the whole edge. */
            int free_status = shrewd_basic_platform_free((void **)&child);
            if (SHREWD_BASIC_ERR_OK != free_status) {
                return free_status;
            }
            *slot = edge->next;
            free_status = shrewd_basic_platform_free((void **)&edge->label);
            if (SHREWD_BASIC_ERR_OK != free_status) {
                return free_status;
            }
            return shrewd_basic_platform_free((void **)&edge);
        }
        if (NULL != child->edges->next) {
            break;
        }
        /* Valueless single-child node: merge it into this edge. */
        shrewd_basic_radix_edge_t *grand = child->edges;
        char *merged = radix_label_concat(edge->label, edge->len, grand->label, grand->len);
        if (NULL == merged) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                             shrewd_basic_ds_log,
                             "radix",
                             "remove: compression skipped\n");
            break;
        }
        (void)shrewd_basic_platform_free((void **)&edge->label);
        edge->label = merged;
        edge->len += grand->len;
        edge->child = grand->child;
        (void)shrewd_basic_platform_free((void **)&grand->label);
        (void)shrewd_basic_platform_free((void **)&grand);
        (void)shrewd_basic_platform_free((void **)&child);
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Depth-first walk appending each edge label into buf; keys in byte order. */
static int radix_walk(const shrewd_basic_radix_node_t *node,
                      char *buf,
                      size_t len,
                      shrewd_basic_radix_visit_fn visit,
                      void *ctx) {
    if (node->has_value) {
        buf[len] = '\0';
        int status = visit(buf, node->value, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    for (const shrewd_basic_radix_edge_t *edge = node->edges; NULL != edge; edge = edge->next) {
        memcpy(buf + len, edge->label, edge->len);
        int status = radix_walk(edge->child, buf, len + edge->len, visit, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

/*
 * Walk the keys starting with @p prefix. The prefix is consumed edge by
 * edge into @p buf; it may also end inside an edge label, in which case the
 * whole subtree below that edge matches. A prefix that diverges or runs
 * beyond the stored paths simply visits nothing.
 */
static int radix_walk_prefix(shrewd_basic_radix_node_t *node,
                             const unsigned char *key,
                             char *buf,
                             size_t len,
                             shrewd_basic_radix_visit_fn visit,
                             void *ctx) {
    while ('\0' != *key) {
        shrewd_basic_radix_edge_t *edge = node->edges;
        while ((NULL != edge) && ((unsigned char)edge->label[0] < *key)) {
            edge = edge->next;
        }
        if ((NULL == edge) || ((unsigned char)edge->label[0] != *key)) {
            return SHREWD_BASIC_ERR_OK;
        }
        size_t m = radix_match(edge, key);
        if (m < edge->len) {
            if ('\0' != key[m]) {
                return SHREWD_BASIC_ERR_OK;
            }
            /* The prefix ends inside this edge: everything below matches. */
            memcpy(buf + len, edge->label, edge->len);
            return radix_walk(edge->child, buf, len + edge->len, visit, ctx);
        }
        memcpy(buf + len, edge->label, edge->len);
        len += edge->len;
        key += m;
        node = edge->child;
    }
    return radix_walk(node, buf, len, visit, ctx);
}

int shrewd_basic_radix_create(shrewd_basic_radix_t **out) {
    if (NULL == out) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "radix", "create: out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_radix_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "create: allocation failed\n");
        return status;
    }
    shrewd_basic_radix_t *tree = block;
    status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_radix_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "create: root allocation failed\n");
        (void)shrewd_basic_platform_free((void **)&tree);
        return status;
    }
    tree->root = block;
    *out = tree;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "radix", "create ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_radix_destroy(shrewd_basic_radix_t *tree) {
    if (NULL == tree) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "radix", "destroy: tree is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = radix_free_node(tree->root);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "destroy: node free failed\n");
        return status;
    }
    status = shrewd_basic_platform_free((void **)&tree);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "destroy: tree free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "radix", "destroy ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_radix_insert(shrewd_basic_radix_t *tree, const char *key, void *value) {
    if ((NULL == tree) || (NULL == key)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "radix",
                         "insert: tree or key is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_radix_node_t *node = tree->root;
    const unsigned char *k = (const unsigned char *)key;
    int placed = 0;
    while (('\0' != *k) && (0 == placed)) {
        shrewd_basic_radix_edge_t **slot = radix_edge_slot(node, *k);
        shrewd_basic_radix_edge_t *edge = *slot;
        if ((NULL == edge) || ((unsigned char)edge->label[0] != *k)) {
            /* No edge starts with this byte: the rest of the key becomes one. */
            size_t rest = strlen((const char *)k);
            shrewd_basic_radix_node_t *child = radix_node_new();
            char *label = radix_label_new((const char *)k, rest);
            void *block = NULL;
            int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_radix_edge_t));
            if ((NULL == child) || (NULL == label) || (SHREWD_BASIC_ERR_OK != status)) {
                (void)shrewd_basic_platform_free((void **)&child);
                (void)shrewd_basic_platform_free((void **)&label);
                shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                                 shrewd_basic_ds_log,
                                 "radix",
                                 "insert: allocation failed\n");
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            shrewd_basic_radix_edge_t *fresh = block;
            fresh->label = label;
            fresh->len = rest;
            fresh->child = child;
            fresh->next = *slot;
            *slot = fresh;
            child->value = value;
            child->has_value = true;
            placed = 1;
            break;
        }
        size_t m = radix_match(edge, k);
        if (m == edge->len) {
            k += m;
            node = edge->child;
            continue;
        }
        /* The key splits or ends inside this edge: split it at m (m >= 1). */
        size_t suffix_len = edge->len - m;
        k += m;
        int branches = ('\0' != *k);
        size_t rest = branches ? strlen((const char *)k) : 0;
        shrewd_basic_radix_node_t *mid = radix_node_new();
        char *suffix = radix_label_new(edge->label + m, suffix_len);
        char *trimmed = radix_label_new(edge->label, m);
        void *sub_block = NULL;
        int status = shrewd_basic_platform_calloc(&sub_block, 1, sizeof(shrewd_basic_radix_edge_t));
        shrewd_basic_radix_node_t *leaf = NULL;
        char *branch_label = NULL;
        void *branch_block = NULL;
        if (0 != branches) {
            leaf = radix_node_new();
            branch_label = radix_label_new((const char *)k, rest);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = shrewd_basic_platform_calloc(
                    &branch_block, 1, sizeof(shrewd_basic_radix_edge_t));
            }
        }
        if ((SHREWD_BASIC_ERR_OK != status) || (NULL == mid) || (NULL == suffix)
            || (NULL == trimmed)
            || ((0 != branches) && ((NULL == leaf) || (NULL == branch_label)))) {
            (void)shrewd_basic_platform_free((void **)&mid);
            (void)shrewd_basic_platform_free((void **)&suffix);
            (void)shrewd_basic_platform_free((void **)&trimmed);
            (void)shrewd_basic_platform_free(&sub_block);
            (void)shrewd_basic_platform_free((void **)&leaf);
            (void)shrewd_basic_platform_free((void **)&branch_label);
            (void)shrewd_basic_platform_free(&branch_block);
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                             shrewd_basic_ds_log,
                             "radix",
                             "insert: allocation failed\n");
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        shrewd_basic_radix_edge_t *sub = sub_block;
        sub->label = suffix;
        sub->len = suffix_len;
        sub->child = edge->child;
        mid->edges = sub;
        (void)shrewd_basic_platform_free((void **)&edge->label);
        edge->label = trimmed;
        edge->len = m;
        edge->child = mid;
        if (0 != branches) {
            shrewd_basic_radix_edge_t *branch = branch_block;
            branch->label = branch_label;
            branch->len = rest;
            branch->child = leaf;
            shrewd_basic_radix_edge_t **mslot = radix_edge_slot(mid, *k);
            branch->next = *mslot;
            *mslot = branch;
            leaf->value = value;
            leaf->has_value = true;
        } else {
            mid->value = value;
            mid->has_value = true;
        }
        placed = 1;
    }
    if (0 == placed) {
        if (node->has_value) {
            shrewd_basic_log(
                SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "radix", "insert: key exists\n");
            return SHREWD_BASIC_ERR_KEY_EXISTS;
        }
        node->value = value;
        node->has_value = true;
    }
    ++tree->size;
    size_t klen = strlen(key);
    if (klen > tree->max_key_len) {
        tree->max_key_len = klen;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_ds_log,
                     "radix",
                     "insert ok, size %zu\n",
                     tree->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_radix_search(const shrewd_basic_radix_t *tree, const char *key, void **out) {
    if ((NULL == tree) || (NULL == key) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "radix",
                         "search: tree, key or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_radix_node_t *node = tree->root;
    const unsigned char *k = (const unsigned char *)key;
    while ('\0' != *k) {
        const shrewd_basic_radix_edge_t *edge = node->edges;
        while ((NULL != edge) && ((unsigned char)edge->label[0] < *k)) {
            edge = edge->next;
        }
        if ((NULL == edge) || ((unsigned char)edge->label[0] != *k)) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                             shrewd_basic_ds_log,
                             "radix",
                             "search: key not found\n");
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        size_t m = radix_match(edge, k);
        if (m != edge->len) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                             shrewd_basic_ds_log,
                             "radix",
                             "search: key not found\n");
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        k += m;
        node = edge->child;
    }
    if (!node->has_value) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "radix", "search: key not found\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = node->value;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_radix_remove(shrewd_basic_radix_t *tree, const char *key, void **out) {
    if ((NULL == tree) || (NULL == key)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "radix",
                         "remove: tree or key is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = radix_remove_rec(tree->root, (const unsigned char *)key, out);
    if (SHREWD_BASIC_ERR_OK == status) {
        --tree->size;
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                         shrewd_basic_ds_log,
                         "radix",
                         "remove ok, size %zu\n",
                         tree->size);
    } else if (SHREWD_BASIC_ERR_KEY_NOTFOUND == status) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "radix", "remove: key not found\n");
    } else {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "remove: node free failed\n");
    }
    return status;
}

int shrewd_basic_radix_foreach(const shrewd_basic_radix_t *tree,
                               shrewd_basic_radix_visit_fn visit,
                               void *ctx) {
    if ((NULL == tree) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach: tree or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, tree->max_key_len + 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach: buffer allocation failed\n");
        return status;
    }
    char *buf = block;
    status = radix_walk(tree->root, buf, 0, visit, ctx);
    int free_status = shrewd_basic_platform_free(&block);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach: visit aborted (%s)\n",
                         shrewd_basic_error_name(status));
        return status;
    }
    if (SHREWD_BASIC_ERR_OK != free_status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach: buffer free failed\n");
        return free_status;
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_radix_foreach_prefix(const shrewd_basic_radix_t *tree,
                                      const char *prefix,
                                      shrewd_basic_radix_visit_fn visit,
                                      void *ctx) {
    if ((NULL == tree) || (NULL == prefix) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach_prefix: tree, prefix or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, tree->max_key_len + 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach_prefix: buffer allocation failed\n");
        return status;
    }
    char *buf = block;
    status = radix_walk_prefix(tree->root, (const unsigned char *)prefix, buf, 0, visit, ctx);
    int free_status = shrewd_basic_platform_free(&block);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach_prefix: visit aborted (%s)\n",
                         shrewd_basic_error_name(status));
        return status;
    }
    if (SHREWD_BASIC_ERR_OK != free_status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "radix",
                         "foreach_prefix: buffer free failed\n");
        return free_status;
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_radix_size(const shrewd_basic_radix_t *tree, size_t *out) {
    if ((NULL == tree) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "radix",
                         "size: tree or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = tree->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_radix_empty(const shrewd_basic_radix_t *tree) {
    return (NULL == tree) || (0 == tree->size);
}

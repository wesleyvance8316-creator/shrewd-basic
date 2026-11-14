#ifndef SHREWD_BASIC_DS_RADIX_H
#define SHREWD_BASIC_DS_RADIX_H

/**
 * @file radix.h
 * @brief Compressed trie (radix tree) mapping C-string keys to @c void* values.
 *
 * The compressed counterpart of trie.h: chains of single-child nodes are
 * merged into one edge labeled with a string, so a node only exists where
 * keys branch, end, or carry a value. Edges are kept sorted by their first
 * byte, so shrewd_basic_radix_foreach() walks keys in lexicographic (byte)
 * order - which is also codepoint order for UTF-8 keys. Removing a key
 * re-compresses the path (dead leaves are pruned, single-child chains are
 * merged again).
 *
 * Keys are unique: inserting a key that is already stored fails with
 * SHREWD_BASIC_ERR_KEY_EXISTS. The empty string is a valid key (it lives on
 * the root node). Values are never freed by the tree; ownership stays with
 * the caller. Every non-boolean interface returns a generic error code
 * (shrewd-basic/platform/error.h) and hands data back through out parameters;
 * an out parameter may be NULL to discard a removed value, but is required
 * where a value is only queried.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One edge: a label (owned copy, @c len bytes) plus the child it leads to. */
typedef struct shrewd_basic_radix_edge {
    char *label;
    size_t len;
    struct shrewd_basic_radix_node *child;
    struct shrewd_basic_radix_edge *next;
} shrewd_basic_radix_edge_t;

/** Tree node: optional value plus edges sorted by label first byte. */
typedef struct shrewd_basic_radix_node {
    void *value;
    bool has_value;
    struct shrewd_basic_radix_edge *edges;
} shrewd_basic_radix_node_t;

/** Radix tree: root node, key count and longest key length (foreach buffer). */
typedef struct shrewd_basic_radix {
    shrewd_basic_radix_node_t *root;
    size_t size;
    size_t max_key_len;
} shrewd_basic_radix_t;

/**
 * Visitor callback for foreach: returns SHREWD_BASIC_ERR_OK to continue,
 * any other code stops the walk and is returned by foreach. @p key is only
 * valid for the duration of the call.
 */
typedef int (*shrewd_basic_radix_visit_fn)(const char *key, void *value, void *ctx);

/** Create an empty tree and store it into @p *out. */
int shrewd_basic_radix_create(shrewd_basic_radix_t **out);

/** Free all nodes and edge labels (not the values). */
int shrewd_basic_radix_destroy(shrewd_basic_radix_t *tree);

/**
 * Insert @p value under @p key, splitting edges as needed.
 * Returns SHREWD_BASIC_ERR_KEY_EXISTS if the key is already stored.
 */
int shrewd_basic_radix_insert(shrewd_basic_radix_t *tree, const char *key,
                              void *value);

/**
 * Store the value matching @p key into @p *out (required).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no key matches.
 */
int shrewd_basic_radix_search(const shrewd_basic_radix_t *tree, const char *key,
                              void **out);

/**
 * Remove @p key and store its value into @p *out (NULL discards); the path
 * is re-compressed afterwards. Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when
 * no key matches.
 */
int shrewd_basic_radix_remove(shrewd_basic_radix_t *tree, const char *key,
                              void **out);

/** Visit all keys in lexicographic order until @p visit returns non-OK. */
int shrewd_basic_radix_foreach(const shrewd_basic_radix_t *tree,
                               shrewd_basic_radix_visit_fn visit, void *ctx);

/**
 * Visit every key starting with @p prefix, in lexicographic order; @p visit
 * receives the full key. A prefix matching no key simply visits nothing
 * (a prefix may also end inside an edge label). An empty prefix walks the
 * whole tree.
 */
int shrewd_basic_radix_foreach_prefix(const shrewd_basic_radix_t *tree,
                                      const char *prefix,
                                      shrewd_basic_radix_visit_fn visit,
                                      void *ctx);

/** Store the key count into @p *out. */
int shrewd_basic_radix_size(const shrewd_basic_radix_t *tree, size_t *out);

/** True when the tree holds no keys. */
bool shrewd_basic_radix_empty(const shrewd_basic_radix_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_RADIX_H */

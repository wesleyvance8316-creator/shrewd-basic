#ifndef SHREWD_BASIC_DS_BTREE_H
#define SHREWD_BASIC_DS_BTREE_H

/**
 * @file btree.h
 * @brief Unbalanced binary search tree of @c void* payloads.
 *
 * Ordering comes from a caller-supplied comparison function, so the tree
 * works as a generic set of pointers. Keys are unique: inserting a payload
 * that compares equal to an existing one fails with SHREWD_BASIC_ERR_KEY_EXISTS.
 * Payloads are never freed by the tree; ownership stays with the caller.
 * Every non-boolean interface returns a generic error code
 * (shrewd-basic/platform/error.h) and hands data back through out parameters.
 *
 * No balancing is performed: this is the plain counterpart of the red-black
 * tree in rbtree.h, sharing the same lookup-oriented API.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Comparison function for two payloads: returns <0 if @p a sorts before
 * @p b, 0 if they are equal keys, >0 otherwise. @p ctx is the opaque context
 * passed at tree creation.
 */
typedef int (*shrewd_basic_btree_cmp_fn)(const void *a, const void *b, void *ctx);

/** Tree node. Exposed for advanced traversal; respect the ordering. */
typedef struct shrewd_basic_btree_node {
    void *data;
    struct shrewd_basic_btree_node *left;
    struct shrewd_basic_btree_node *right;
} shrewd_basic_btree_node_t;

/** Binary search tree: root, comparison function, context and count. */
typedef struct shrewd_basic_btree {
    shrewd_basic_btree_node_t *root;
    shrewd_basic_btree_cmp_fn cmp;
    void *ctx;
    size_t size;
} shrewd_basic_btree_t;

/**
 * Visitor callback for shrewd_basic_btree_foreach(): returns
 * SHREWD_BASIC_ERR_OK to continue, any other code stops the walk and is
 * returned by foreach.
 */
typedef int (*shrewd_basic_btree_visit_fn)(void *data, void *ctx);

/**
 * Create an empty tree ordered by @p cmp (must not be NULL) and store it
 * into @p *out.
 */
int shrewd_basic_btree_create(shrewd_basic_btree_cmp_fn cmp, void *ctx, shrewd_basic_btree_t **out);

/** Free all nodes (not the payloads). */
int shrewd_basic_btree_destroy(shrewd_basic_btree_t *tree);

/**
 * Insert @p data. Returns SHREWD_BASIC_ERR_KEY_EXISTS if an equal key is already
 * stored.
 */
int shrewd_basic_btree_insert(shrewd_basic_btree_t *tree, void *data);

/**
 * Store the payload matching @p key into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no key matches.
 */
int shrewd_basic_btree_search(const shrewd_basic_btree_t *tree, const void *key, void **out);

/**
 * Remove the node matching @p key and store its payload into @p *out
 * (NULL discards). Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no key matches.
 */
int shrewd_basic_btree_remove(shrewd_basic_btree_t *tree, const void *key, void **out);

/**
 * Store the smallest payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the tree is empty.
 */
int shrewd_basic_btree_min(const shrewd_basic_btree_t *tree, void **out);

/**
 * Store the largest payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the tree is empty.
 */
int shrewd_basic_btree_max(const shrewd_basic_btree_t *tree, void **out);

/** Visit payloads in ascending key order until @p visit returns non-OK. */
int shrewd_basic_btree_foreach(const shrewd_basic_btree_t *tree,
                               shrewd_basic_btree_visit_fn visit,
                               void *ctx);

/** Store the payload count into @p *out. */
int shrewd_basic_btree_size(const shrewd_basic_btree_t *tree, size_t *out);

/** True when the tree holds no payloads. */
bool shrewd_basic_btree_empty(const shrewd_basic_btree_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_BTREE_H */

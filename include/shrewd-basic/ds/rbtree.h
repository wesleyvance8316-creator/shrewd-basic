#ifndef SHREWD_BASIC_DS_RBTREE_H
#define SHREWD_BASIC_DS_RBTREE_H

/**
 * @file rbtree.h
 * @brief Red-black tree (self-balancing BST) of @c void* payloads.
 *
 * Ordering comes from a caller-supplied comparison function, so the tree
 * works as a generic set of pointers. Keys are unique: inserting a payload
 * that compares equal to an existing one fails with SHREWD_BASIC_ERR_KEY_EXISTS.
 * Payloads are never freed by the tree; ownership stays with the caller.
 * Every non-boolean interface returns a generic error code
 * (shrewd-basic/platform/error.h) and hands data back through out parameters.
 *
 * Lookup API matches the unbalanced tree in btree.h; all operations are
 * O(log n) thanks to the balancing invariants.
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
typedef int (*shrewd_basic_rbtree_cmp_fn)(const void *a, const void *b, void *ctx);

/** Node color. */
typedef enum shrewd_basic_rbtree_color {
    SHREWD_BASIC_RBTREE_RED = 0,
    SHREWD_BASIC_RBTREE_BLACK = 1
} shrewd_basic_rbtree_color_t;

/**
 * Tree node. Exposed for advanced traversal; the tree also owns a private
 * sentinel node that stands for every missing child, so links are never NULL.
 */
typedef struct shrewd_basic_rbtree_node {
    void *data;
    struct shrewd_basic_rbtree_node *left;
    struct shrewd_basic_rbtree_node *right;
    struct shrewd_basic_rbtree_node *parent;
    shrewd_basic_rbtree_color_t color;
} shrewd_basic_rbtree_node_t;

/** Red-black tree: root, sentinel, comparison function, context and count. */
typedef struct shrewd_basic_rbtree {
    shrewd_basic_rbtree_node_t *root;
    shrewd_basic_rbtree_node_t *nil;
    shrewd_basic_rbtree_cmp_fn cmp;
    void *ctx;
    size_t size;
} shrewd_basic_rbtree_t;

/**
 * Visitor callback for shrewd_basic_rbtree_foreach(): returns
 * SHREWD_BASIC_ERR_OK to continue, any other code stops the walk and is
 * returned by foreach.
 */
typedef int (*shrewd_basic_rbtree_visit_fn)(void *data, void *ctx);

/**
 * Create an empty tree ordered by @p cmp (must not be NULL) and store it
 * into @p *out.
 */
int shrewd_basic_rbtree_create(shrewd_basic_rbtree_cmp_fn cmp, void *ctx,
                               shrewd_basic_rbtree_t **out);

/** Free all nodes (not the payloads). */
int shrewd_basic_rbtree_destroy(shrewd_basic_rbtree_t *tree);

/**
 * Insert @p data. Returns SHREWD_BASIC_ERR_KEY_EXISTS if an equal key is already
 * stored.
 */
int shrewd_basic_rbtree_insert(shrewd_basic_rbtree_t *tree, void *data);

/**
 * Store the payload matching @p key into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no key matches.
 */
int shrewd_basic_rbtree_search(const shrewd_basic_rbtree_t *tree, const void *key,
                               void **out);

/**
 * Remove the node matching @p key and store its payload into @p *out
 * (NULL discards). Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no key matches.
 */
int shrewd_basic_rbtree_remove(shrewd_basic_rbtree_t *tree, const void *key,
                               void **out);

/**
 * Store the smallest payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the tree is empty.
 */
int shrewd_basic_rbtree_min(const shrewd_basic_rbtree_t *tree, void **out);

/**
 * Store the largest payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the tree is empty.
 */
int shrewd_basic_rbtree_max(const shrewd_basic_rbtree_t *tree, void **out);

/** Visit payloads in ascending key order until @p visit returns non-OK. */
int shrewd_basic_rbtree_foreach(const shrewd_basic_rbtree_t *tree,
                                shrewd_basic_rbtree_visit_fn visit, void *ctx);

/** Store the payload count into @p *out. */
int shrewd_basic_rbtree_size(const shrewd_basic_rbtree_t *tree, size_t *out);

/** True when the tree holds no payloads. */
bool shrewd_basic_rbtree_empty(const shrewd_basic_rbtree_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_RBTREE_H */

#ifndef SHREWD_BASIC_DS_DLIST_H
#define SHREWD_BASIC_DS_DLIST_H

/**
 * @file dlist.h
 * @brief Doubly linked list of @c void* payloads.
 *
 * The list never frees the payloads it holds: ownership of each @c void *data
 * stays with the caller. In addition to O(1) insertion/removal at both ends,
 * nodes expose their links so middle removal stays O(1). Every non-boolean
 * interface returns a generic error code (shrewd-basic/platform/error.h) and hands
 * data back through out parameters; an out parameter may be NULL to discard a
 * removed payload, but is required where a value is only queried.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** List node. Exposed so callers can unlink nodes in the middle in O(1). */
typedef struct shrewd_basic_dlist_node {
    void *data;
    struct shrewd_basic_dlist_node *prev;
    struct shrewd_basic_dlist_node *next;
} shrewd_basic_dlist_node_t;

/** Doubly linked list: head/tail pointers plus element count. */
typedef struct shrewd_basic_dlist {
    shrewd_basic_dlist_node_t *head;
    shrewd_basic_dlist_node_t *tail;
    size_t size;
} shrewd_basic_dlist_t;

/**
 * Visitor callback for the foreach family: returns SHREWD_BASIC_ERR_OK to
 * continue, any other code stops the walk and is returned by foreach.
 */
typedef int (*shrewd_basic_dlist_visit_fn)(void *data, void *ctx);

/** Create an empty list and store it into @p *out. */
int shrewd_basic_dlist_create(shrewd_basic_dlist_t **out);

/** Free all nodes (not the payloads). */
int shrewd_basic_dlist_destroy(shrewd_basic_dlist_t *list);

/** Insert @p data at the front. */
int shrewd_basic_dlist_push_front(shrewd_basic_dlist_t *list, void *data);

/** Insert @p data at the back. */
int shrewd_basic_dlist_push_back(shrewd_basic_dlist_t *list, void *data);

/**
 * Remove the front node and store its payload into @p *out (NULL discards).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the list is empty.
 */
int shrewd_basic_dlist_pop_front(shrewd_basic_dlist_t *list, void **out);

/**
 * Remove the back node and store its payload into @p *out (NULL discards).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the list is empty.
 */
int shrewd_basic_dlist_pop_back(shrewd_basic_dlist_t *list, void **out);

/**
 * Store the front payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the list is empty.
 */
int shrewd_basic_dlist_front(const shrewd_basic_dlist_t *list, void **out);

/**
 * Store the back payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the list is empty.
 */
int shrewd_basic_dlist_back(const shrewd_basic_dlist_t *list, void **out);

/**
 * Insert @p data just before @p node, which must belong to @p list.
 */
int shrewd_basic_dlist_insert_before(shrewd_basic_dlist_t *list,
                                     shrewd_basic_dlist_node_t *node,
                                     void *data);

/**
 * Insert @p data just after @p node, which must belong to @p list.
 */
int shrewd_basic_dlist_insert_after(shrewd_basic_dlist_t *list,
                                    shrewd_basic_dlist_node_t *node,
                                    void *data);

/**
 * Unlink and free @p node (which must belong to @p list) and store its
 * payload into @p *out (NULL discards).
 */
int shrewd_basic_dlist_remove(shrewd_basic_dlist_t *list,
                              shrewd_basic_dlist_node_t *node,
                              void **out);

/** Store the node count into @p *out. */
int shrewd_basic_dlist_size(const shrewd_basic_dlist_t *list, size_t *out);

/** True when the list holds no nodes. */
bool shrewd_basic_dlist_empty(const shrewd_basic_dlist_t *list);

/** Visit payloads front to back until @p visit returns non-OK. */
int shrewd_basic_dlist_foreach(const shrewd_basic_dlist_t *list,
                               shrewd_basic_dlist_visit_fn visit,
                               void *ctx);

/** Visit payloads back to front until @p visit returns non-OK. */
int shrewd_basic_dlist_foreach_reverse(const shrewd_basic_dlist_t *list,
                                       shrewd_basic_dlist_visit_fn visit,
                                       void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_DLIST_H */

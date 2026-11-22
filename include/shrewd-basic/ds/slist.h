#ifndef SHREWD_BASIC_DS_SLIST_H
#define SHREWD_BASIC_DS_SLIST_H

/**
 * @file slist.h
 * @brief Singly linked list of @c void* payloads.
 *
 * The list never frees the payloads it holds: ownership of each @c void *data
 * stays with the caller. Every non-boolean interface returns a generic error
 * code (shrewd-basic/platform/error.h) and hands data back through out parameters;
 * an out parameter may be NULL to discard a popped payload, but is required
 * where a value is only queried.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** List node. Exposed so callers can walk the chain themselves. */
typedef struct shrewd_basic_slist_node {
    void *data;
    struct shrewd_basic_slist_node *next;
} shrewd_basic_slist_node_t;

/** Singly linked list: head/tail pointers plus element count. */
typedef struct shrewd_basic_slist {
    shrewd_basic_slist_node_t *head;
    shrewd_basic_slist_node_t *tail;
    size_t size;
} shrewd_basic_slist_t;

/**
 * Visitor callback for shrewd_basic_slist_foreach(): returns
 * SHREWD_BASIC_ERR_OK to continue, any other code stops the walk and is
 * returned by foreach.
 */
typedef int (*shrewd_basic_slist_visit_fn)(void *data, void *ctx);

/** Create an empty list and store it into @p *out. */
int shrewd_basic_slist_create(shrewd_basic_slist_t **out);

/** Free all nodes (not the payloads). */
int shrewd_basic_slist_destroy(shrewd_basic_slist_t *list);

/** Insert @p data at the front. */
int shrewd_basic_slist_push_front(shrewd_basic_slist_t *list, void *data);

/** Insert @p data at the back. */
int shrewd_basic_slist_push_back(shrewd_basic_slist_t *list, void *data);

/**
 * Remove the front node and store its payload into @p *out (NULL discards).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the list is empty.
 */
int shrewd_basic_slist_pop_front(shrewd_basic_slist_t *list, void **out);

/**
 * Store the front payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the list is empty.
 */
int shrewd_basic_slist_front(const shrewd_basic_slist_t *list, void **out);

/** Store the node count into @p *out. */
int shrewd_basic_slist_size(const shrewd_basic_slist_t *list, size_t *out);

/** True when the list holds no nodes. */
bool shrewd_basic_slist_empty(const shrewd_basic_slist_t *list);

/**
 * Visit payloads front to back until @p visit returns non-OK; that code is
 * then returned.
 */
int shrewd_basic_slist_foreach(const shrewd_basic_slist_t *list,
                               shrewd_basic_slist_visit_fn visit,
                               void *ctx);

/** Reverse the node order in place. */
int shrewd_basic_slist_reverse(shrewd_basic_slist_t *list);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_SLIST_H */

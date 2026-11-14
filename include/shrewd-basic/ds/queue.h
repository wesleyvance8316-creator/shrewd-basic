#ifndef SHREWD_BASIC_DS_QUEUE_H
#define SHREWD_BASIC_DS_QUEUE_H

/**
 * @file queue.h
 * @brief FIFO queue of @c void* payloads (linked nodes, O(1) at both ends).
 *
 * The queue never frees the payloads it holds: ownership of each @c void *data
 * stays with the caller. Every non-boolean interface returns a generic error
 * code (shrewd-basic/platform/error.h) and hands data back through out parameters;
 * an out parameter may be NULL to discard a dequeued payload, but is required
 * where a value is only queried.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Queue node; defined privately in queue.c. */
typedef struct shrewd_basic_queue_node shrewd_basic_queue_node_t;

/** FIFO queue: head (front), tail (back) and element count. */
typedef struct shrewd_basic_queue {
    shrewd_basic_queue_node_t *head;
    shrewd_basic_queue_node_t *tail;
    size_t size;
} shrewd_basic_queue_t;

/**
 * Visitor callback for shrewd_basic_queue_foreach(): returns
 * SHREWD_BASIC_ERR_OK to continue, any other code stops the walk and is
 * returned by foreach.
 */
typedef int (*shrewd_basic_queue_visit_fn)(void *data, void *ctx);

/** Create an empty queue and store it into @p *out. */
int shrewd_basic_queue_create(shrewd_basic_queue_t **out);

/** Free all nodes (not the payloads). */
int shrewd_basic_queue_destroy(shrewd_basic_queue_t *queue);

/** Append @p data at the back. */
int shrewd_basic_queue_enqueue(shrewd_basic_queue_t *queue, void *data);

/**
 * Remove the front node and store its payload into @p *out (NULL discards).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the queue is empty.
 */
int shrewd_basic_queue_dequeue(shrewd_basic_queue_t *queue, void **out);

/**
 * Store the front payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the queue is empty.
 */
int shrewd_basic_queue_front(const shrewd_basic_queue_t *queue, void **out);

/** Store the element count into @p *out. */
int shrewd_basic_queue_size(const shrewd_basic_queue_t *queue, size_t *out);

/** True when the queue holds no payloads. */
bool shrewd_basic_queue_empty(const shrewd_basic_queue_t *queue);

/** Visit payloads front to back until @p visit returns non-OK. */
int shrewd_basic_queue_foreach(const shrewd_basic_queue_t *queue,
                               shrewd_basic_queue_visit_fn visit, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_QUEUE_H */

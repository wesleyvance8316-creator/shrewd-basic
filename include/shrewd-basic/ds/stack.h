#ifndef SHREWD_BASIC_DS_STACK_H
#define SHREWD_BASIC_DS_STACK_H

/**
 * @file stack.h
 * @brief LIFO stack of @c void* payloads (growing array).
 *
 * The stack never frees the payloads it holds: ownership of each @c void *data
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

/** Stack: item array (bottom first), element count and capacity. */
typedef struct shrewd_basic_stack {
    void **items;
    size_t size;
    size_t capacity;
} shrewd_basic_stack_t;

/**
 * Visitor callback for shrewd_basic_stack_foreach(): returns
 * SHREWD_BASIC_ERR_OK to continue, any other code stops the walk and is
 * returned by foreach.
 */
typedef int (*shrewd_basic_stack_visit_fn)(void *data, void *ctx);

/** Create an empty stack and store it into @p *out. */
int shrewd_basic_stack_create(shrewd_basic_stack_t **out);

/** Free the item array (not the payloads). */
int shrewd_basic_stack_destroy(shrewd_basic_stack_t *stack);

/** Push @p data on top. */
int shrewd_basic_stack_push(shrewd_basic_stack_t *stack, void *data);

/**
 * Pop the top payload into @p *out (NULL discards).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the stack is empty.
 */
int shrewd_basic_stack_pop(shrewd_basic_stack_t *stack, void **out);

/**
 * Store the top payload into @p *out.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the stack is empty.
 */
int shrewd_basic_stack_top(const shrewd_basic_stack_t *stack, void **out);

/** Store the element count into @p *out. */
int shrewd_basic_stack_size(const shrewd_basic_stack_t *stack, size_t *out);

/** True when the stack holds no payloads. */
bool shrewd_basic_stack_empty(const shrewd_basic_stack_t *stack);

/** Visit payloads bottom to top until @p visit returns non-OK. */
int shrewd_basic_stack_foreach(const shrewd_basic_stack_t *stack,
                               shrewd_basic_stack_visit_fn visit,
                               void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_STACK_H */

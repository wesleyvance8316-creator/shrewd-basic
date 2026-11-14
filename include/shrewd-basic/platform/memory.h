#ifndef SHREWD_BASIC_PLATFORM_MEMORY_H
#define SHREWD_BASIC_PLATFORM_MEMORY_H

/**
 * @file memory.h
 * @brief Platform memory allocation wrappers.
 *
 * All basic components allocate through these wrappers, so a port can
 * redirect them to a pool allocator, a tracer, or any other scheme.
 * Returns the generic error codes from shrewd-basic/platform/error.h.
 *
 * The wrappers call the SHREWD_BASIC_MALLOC/CALLOC/REALLOC/FREE macros
 * below, never malloc and friends directly. Redefine the macros (compiler
 * flags, or before including this header) to select a different allocation
 * implementation.
 */

#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Allocation function macros; override to plug in a custom allocator. */
#ifndef SHREWD_BASIC_MALLOC
#define SHREWD_BASIC_MALLOC  malloc
#endif
#ifndef SHREWD_BASIC_CALLOC
#define SHREWD_BASIC_CALLOC  calloc
#endif
#ifndef SHREWD_BASIC_REALLOC
#define SHREWD_BASIC_REALLOC realloc
#endif
#ifndef SHREWD_BASIC_FREE
#define SHREWD_BASIC_FREE    free
#endif

/**
 * Allocate @p size bytes and store the block into @p *out (always the first
 * parameter). Returns SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_MEM_NOMEM
 * (allocation failed) or SHREWD_BASIC_ERR_ARG_INVALID (NULL out or zero
 * size).
 */
int shrewd_basic_platform_alloc(void **out, size_t size);

/**
 * Allocate @p count * @p size zero-initialized bytes and store the block
 * into @p *out. Returns SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_MEM_NOMEM or
 * SHREWD_BASIC_ERR_ARG_INVALID (NULL out or zero count/size).
 */
int shrewd_basic_platform_calloc(void **out, size_t count, size_t size);

/**
 * Resize the block at @p ptr to @p size bytes and store the (possibly
 * moved) block into @p *out. @p ptr may be NULL, which behaves like
 * shrewd_basic_platform_alloc(). The old block is released on success.
 * Returns SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_MEM_NOMEM or
 * SHREWD_BASIC_ERR_ARG_INVALID (NULL out or zero size).
 */
int shrewd_basic_platform_realloc(void **out, void *ptr, size_t size);

/**
 * Release the block referenced by @p *ptr and set @p *ptr to NULL, so a
 * caller can never keep a dangling pointer. Returns SHREWD_BASIC_ERR_OK,
 * also when @p ptr or @p *ptr is NULL (nothing to release).
 */
int shrewd_basic_platform_free(void **ptr);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_PLATFORM_MEMORY_H */

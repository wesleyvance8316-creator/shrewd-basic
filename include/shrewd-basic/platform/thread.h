#ifndef SHREWD_BASIC_PLATFORM_THREAD_H
#define SHREWD_BASIC_PLATFORM_THREAD_H

/**
 * @file thread.h
 * @brief Optional mutex hooks for the library's global side tables.
 *
 * The data the library hands back (value trees, descriptors) is not
 * synchronized - share value trees read-only across threads and mutate
 * them under the application's own discipline. What the library DOES
 * share internally is a handful of global side tables (the json decode
 * allocation registry, the reflect lookup cache, the log module
 * registry). By default those are used unsynchronized, matching a
 * single-threaded host; plug a mutex in to make concurrent use safe:
 *
 *   shrewd_basic_platform_set_mutex(my_lock, my_unlock, my_ctx);
 *
 * Hooks are plain function pointers - the library has no threading
 * dependency of its own. NULL hooks (the default) mean no locking.
 */

#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Lock/unlock callbacks; @p ctx is passed through unchanged. */
typedef void (*shrewd_basic_platform_mutex_fn)(void *ctx);

/**
 * Install the mutex used around the library's global side tables
 * (either callback may be NULL to mean "no synchronization").
 */
int shrewd_basic_platform_set_mutex(shrewd_basic_platform_mutex_fn lock,
                                    shrewd_basic_platform_mutex_fn unlock,
                                    void *ctx);

/** Take the library mutex (no-op without hooks). */
void shrewd_basic_platform_lock(void);

/** Release the library mutex (no-op without hooks). */
void shrewd_basic_platform_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_PLATFORM_THREAD_H */

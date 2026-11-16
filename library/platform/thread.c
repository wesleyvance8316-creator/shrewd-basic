#include "shrewd-basic/platform/thread.h"

/*
 * The optional mutex: three pointers and two calls, no threading
 * dependency. Without hooks every call is a NULL check - the
 * single-threaded case pays nothing measurable.
 */
static shrewd_basic_platform_mutex_fn g_lock;
static shrewd_basic_platform_mutex_fn g_unlock;
static void *g_mutex_ctx;

int shrewd_basic_platform_set_mutex(shrewd_basic_platform_mutex_fn lock,
                                    shrewd_basic_platform_mutex_fn unlock,
                                    void *ctx)
{
    g_lock = lock;
    g_unlock = unlock;
    g_mutex_ctx = ctx;
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_platform_lock(void)
{
    if (NULL != g_lock) {
        g_lock(g_mutex_ctx);
    }
}

void shrewd_basic_platform_unlock(void)
{
    if (NULL != g_unlock) {
        g_unlock(g_mutex_ctx);
    }
}

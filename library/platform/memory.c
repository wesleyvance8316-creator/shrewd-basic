#include "shrewd-basic/platform/memory.h"

#include <stdlib.h>

int shrewd_basic_platform_alloc(void **out, size_t size) {
    if ((NULL == out) || (0 == size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *ptr = SHREWD_BASIC_MALLOC(size);
    if (NULL == ptr) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    *out = ptr;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_platform_calloc(void **out, size_t count, size_t size) {
    if ((NULL == out) || (0 == count) || (0 == size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *ptr = SHREWD_BASIC_CALLOC(count, size);
    if (NULL == ptr) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    *out = ptr;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_platform_realloc(void **out, void *ptr, size_t size) {
    if ((NULL == out) || (0 == size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *resized = SHREWD_BASIC_REALLOC(ptr, size);
    if (NULL == resized) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    *out = resized;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_platform_free(void **ptr) {
    if ((NULL == ptr) || (NULL == *ptr)) {
        return SHREWD_BASIC_ERR_OK;
    }
    SHREWD_BASIC_FREE(*ptr);
    *ptr = NULL;
    return SHREWD_BASIC_ERR_OK;
}

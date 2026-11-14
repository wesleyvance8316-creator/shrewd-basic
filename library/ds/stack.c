#include "shrewd-basic/ds/stack.h"

#include "shrewd-basic/platform/memory.h"

int shrewd_basic_stack_create(shrewd_basic_stack_t **out)
{
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_stack_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_destroy(shrewd_basic_stack_t *stack)
{
    if (NULL == stack) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = shrewd_basic_platform_free((void **)&stack->items);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_platform_free((void **)&stack);
}

int shrewd_basic_stack_push(shrewd_basic_stack_t *stack, void *data)
{
    if (NULL == stack) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (stack->size == stack->capacity) {
        size_t capacity = (0 != stack->capacity) ? stack->capacity * 2 : 8;
        void *block = NULL;
        int status = shrewd_basic_platform_realloc(&block, stack->items,
                                                  capacity * sizeof(*stack->items));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        stack->items = block;
        stack->capacity = capacity;
    }
    stack->items[stack->size] = data;
    ++stack->size;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_pop(shrewd_basic_stack_t *stack, void **out)
{
    if (NULL == stack) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == stack->size) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    --stack->size;
    if (NULL != out) {
        *out = stack->items[stack->size];
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_top(const shrewd_basic_stack_t *stack, void **out)
{
    if ((NULL == stack) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == stack->size) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = stack->items[stack->size - 1];
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_size(const shrewd_basic_stack_t *stack, size_t *out)
{
    if ((NULL == stack) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = stack->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_stack_empty(const shrewd_basic_stack_t *stack)
{
    return (NULL == stack) || (0 == stack->size);
}

int shrewd_basic_stack_foreach(const shrewd_basic_stack_t *stack,
                               shrewd_basic_stack_visit_fn visit, void *ctx)
{
    if ((NULL == stack) || (NULL == visit)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < stack->size; ++i) {
        int status = visit(stack->items[i], ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

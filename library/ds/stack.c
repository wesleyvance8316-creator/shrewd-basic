#include "shrewd-basic/ds/stack.h"

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared ds log module, defined in library/ds/ds.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log);

int shrewd_basic_stack_create(shrewd_basic_stack_t **out) {
    if (NULL == out) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "stack", "create: out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_stack_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "stack",
                         "create: allocation failed\n");
        return status;
    }
    *out = block;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "stack", "create ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_destroy(shrewd_basic_stack_t *stack) {
    if (NULL == stack) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "stack", "destroy: stack is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = shrewd_basic_platform_free((void **)&stack->items);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "stack",
                         "destroy: items free failed\n");
        return status;
    }
    status = shrewd_basic_platform_free((void **)&stack);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "stack",
                         "destroy: stack free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "stack", "destroy ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_push(shrewd_basic_stack_t *stack, void *data) {
    if (NULL == stack) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "stack", "push: stack is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (stack->size == stack->capacity) {
        size_t capacity = (0 != stack->capacity) ? stack->capacity * 2 : 8;
        void *block = NULL;
        int status
            = shrewd_basic_platform_realloc(&block, stack->items, capacity * sizeof(*stack->items));
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                             shrewd_basic_ds_log,
                             "stack",
                             "push: growth to %zu failed\n",
                             capacity);
            return status;
        }
        stack->items = block;
        stack->capacity = capacity;
    }
    stack->items[stack->size] = data;
    ++stack->size;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_ds_log,
                     "stack",
                     "push ok, size %zu\n",
                     stack->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_pop(shrewd_basic_stack_t *stack, void **out) {
    if (NULL == stack) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "stack", "pop: stack is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == stack->size) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "stack", "pop: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    --stack->size;
    if (NULL != out) {
        *out = stack->items[stack->size];
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_ds_log,
                     "stack",
                     "pop ok, size %zu\n",
                     stack->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_top(const shrewd_basic_stack_t *stack, void **out) {
    if ((NULL == stack) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "stack",
                         "top: stack or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == stack->size) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "stack", "top: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = stack->items[stack->size - 1];
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_stack_size(const shrewd_basic_stack_t *stack, size_t *out) {
    if ((NULL == stack) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "stack",
                         "size: stack or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = stack->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_stack_empty(const shrewd_basic_stack_t *stack) {
    return (NULL == stack) || (0 == stack->size);
}

int shrewd_basic_stack_foreach(const shrewd_basic_stack_t *stack,
                               shrewd_basic_stack_visit_fn visit,
                               void *ctx) {
    if ((NULL == stack) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "stack",
                         "foreach: stack or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < stack->size; ++i) {
        int status = visit(stack->items[i], ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                             shrewd_basic_ds_log,
                             "stack",
                             "foreach: visit aborted (%s)\n",
                             shrewd_basic_error_name(status));
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

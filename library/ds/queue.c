#include "shrewd-basic/ds/queue.h"

#include "shrewd-basic/platform/memory.h"

struct shrewd_basic_queue_node {
    void *data;
    struct shrewd_basic_queue_node *next;
};

int shrewd_basic_queue_create(shrewd_basic_queue_t **out)
{
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_queue_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_destroy(shrewd_basic_queue_t *queue)
{
    if (NULL == queue) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_queue_node_t *node = queue->head;
    while (NULL != node) {
        shrewd_basic_queue_node_t *next = node->next;
        int status = shrewd_basic_platform_free((void **)&node);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        node = next;
    }
    return shrewd_basic_platform_free((void **)&queue);
}

int shrewd_basic_queue_enqueue(shrewd_basic_queue_t *queue, void *data)
{
    if (NULL == queue) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_queue_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    shrewd_basic_queue_node_t *node = block;
    node->data = data;
    node->next = NULL;
    if (NULL != queue->tail) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    ++queue->size;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_dequeue(shrewd_basic_queue_t *queue, void **out)
{
    if (NULL == queue) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == queue->head) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    shrewd_basic_queue_node_t *node = queue->head;
    if (NULL != out) {
        *out = node->data;
    }
    queue->head = node->next;
    if (NULL == queue->head) {
        queue->tail = NULL;
    }
    --queue->size;
    return shrewd_basic_platform_free((void **)&node);
}

int shrewd_basic_queue_front(const shrewd_basic_queue_t *queue, void **out)
{
    if ((NULL == queue) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == queue->head) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = queue->head->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_size(const shrewd_basic_queue_t *queue, size_t *out)
{
    if ((NULL == queue) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = queue->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_queue_empty(const shrewd_basic_queue_t *queue)
{
    return (NULL == queue) || (0 == queue->size);
}

int shrewd_basic_queue_foreach(const shrewd_basic_queue_t *queue,
                               shrewd_basic_queue_visit_fn visit, void *ctx)
{
    if ((NULL == queue) || (NULL == visit)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (const shrewd_basic_queue_node_t *node = queue->head; NULL != node; node = node->next) {
        int status = visit(node->data, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

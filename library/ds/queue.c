#include "shrewd-basic/ds/queue.h"

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared ds log module, defined in library/ds/ds.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log);

struct shrewd_basic_queue_node {
    void *data;
    struct shrewd_basic_queue_node *next;
};

int shrewd_basic_queue_create(shrewd_basic_queue_t **out)
{
    if (NULL == out) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "queue", "create: out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_queue_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "queue",
                         "create: allocation failed\n");
        return status;
    }
    *out = block;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "queue", "create ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_destroy(shrewd_basic_queue_t *queue)
{
    if (NULL == queue) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "queue", "destroy: queue is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_queue_node_t *node = queue->head;
    while (NULL != node) {
        shrewd_basic_queue_node_t *next = node->next;
        int status = shrewd_basic_platform_free((void **)&node);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                             shrewd_basic_ds_log,
                             "queue",
                             "destroy: node free failed\n");
            return status;
        }
        node = next;
    }
    int status = shrewd_basic_platform_free((void **)&queue);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "queue",
                         "destroy: queue free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "queue", "destroy ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_enqueue(shrewd_basic_queue_t *queue, void *data)
{
    if (NULL == queue) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "queue", "enqueue: queue is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_queue_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "queue",
                         "enqueue: allocation failed\n");
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
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_ds_log,
                     "queue",
                     "enqueue ok, size %zu\n",
                     queue->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_dequeue(shrewd_basic_queue_t *queue, void **out)
{
    if (NULL == queue) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "queue", "dequeue: queue is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == queue->head) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "queue", "dequeue: empty\n");
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
    int status = shrewd_basic_platform_free((void **)&node);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR,
                         shrewd_basic_ds_log,
                         "queue",
                         "dequeue: node free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_ds_log,
                     "queue",
                     "dequeue ok, size %zu\n",
                     queue->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_front(const shrewd_basic_queue_t *queue, void **out)
{
    if ((NULL == queue) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "queue",
                         "front: queue or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == queue->head) {
        shrewd_basic_log(
            SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "queue", "front: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = queue->head->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_queue_size(const shrewd_basic_queue_t *queue, size_t *out)
{
    if ((NULL == queue) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "queue",
                         "size: queue or out is NULL\n");
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
                               shrewd_basic_queue_visit_fn visit,
                               void *ctx)
{
    if ((NULL == queue) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_ds_log,
                         "queue",
                         "foreach: queue or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (const shrewd_basic_queue_node_t *node = queue->head; NULL != node; node = node->next) {
        int status = visit(node->data, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                             shrewd_basic_ds_log,
                             "queue",
                             "foreach: visit aborted (%s)\n",
                             shrewd_basic_error_name(status));
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

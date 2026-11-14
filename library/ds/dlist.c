#include "shrewd-basic/ds/dlist.h"

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared ds log module, defined in library/ds/ds.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log);

int shrewd_basic_dlist_create(shrewd_basic_dlist_t **out)
{
    if (NULL == out) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "create: out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_dlist_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "create: allocation failed\n");
        return status;
    }
    *out = block;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "dlist",
                     "create ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_destroy(shrewd_basic_dlist_t *list)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "destroy: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_dlist_node_t *node = list->head;
    while (NULL != node) {
        shrewd_basic_dlist_node_t *next = node->next;
        int status = shrewd_basic_platform_free((void **)&node);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log,
                             "dlist", "destroy: node free failed\n");
            return status;
        }
        node = next;
    }
    int status = shrewd_basic_platform_free((void **)&list);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "destroy: list free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "dlist",
                     "destroy ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_push_front(shrewd_basic_dlist_t *list, void *data)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "push_front: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_dlist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "push_front: allocation failed\n");
        return status;
    }
    shrewd_basic_dlist_node_t *node = block;
    node->data = data;
    node->prev = NULL;
    node->next = list->head;
    if (NULL != list->head) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }
    list->head = node;
    ++list->size;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                     "push_front ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_push_back(shrewd_basic_dlist_t *list, void *data)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "push_back: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_dlist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "push_back: allocation failed\n");
        return status;
    }
    shrewd_basic_dlist_node_t *node = block;
    node->data = data;
    node->prev = list->tail;
    node->next = NULL;
    if (NULL != list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    ++list->size;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                     "push_back ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_pop_front(shrewd_basic_dlist_t *list, void **out)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "pop_front: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->head) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                         "pop_front: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    shrewd_basic_dlist_node_t *node = list->head;
    if (NULL != out) {
        *out = node->data;
    }
    list->head = node->next;
    if (NULL != list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL;
    }
    --list->size;
    int status = shrewd_basic_platform_free((void **)&node);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "pop_front: node free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                     "pop_front ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_pop_back(shrewd_basic_dlist_t *list, void **out)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "pop_back: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->tail) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                         "pop_back: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    shrewd_basic_dlist_node_t *node = list->tail;
    if (NULL != out) {
        *out = node->data;
    }
    list->tail = node->prev;
    if (NULL != list->tail) {
        list->tail->next = NULL;
    } else {
        list->head = NULL;
    }
    --list->size;
    int status = shrewd_basic_platform_free((void **)&node);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "pop_back: node free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                     "pop_back ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_front(const shrewd_basic_dlist_t *list, void **out)
{
    if ((NULL == list) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "front: list or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->head) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                         "front: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = list->head->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_back(const shrewd_basic_dlist_t *list, void **out)
{
    if ((NULL == list) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "back: list or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->tail) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                         "back: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = list->tail->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_insert_before(shrewd_basic_dlist_t *list,
                                     shrewd_basic_dlist_node_t *node, void *data)
{
    if ((NULL == list) || (NULL == node)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "insert_before: list or node is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_dlist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "insert_before: allocation failed\n");
        return status;
    }
    shrewd_basic_dlist_node_t *fresh = block;
    fresh->data = data;
    fresh->prev = node->prev;
    fresh->next = node;
    if (NULL != node->prev) {
        node->prev->next = fresh;
    } else {
        list->head = fresh;
    }
    node->prev = fresh;
    ++list->size;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                     "insert_before ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_insert_after(shrewd_basic_dlist_t *list,
                                    shrewd_basic_dlist_node_t *node, void *data)
{
    if ((NULL == list) || (NULL == node)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "insert_after: list or node is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_dlist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "insert_after: allocation failed\n");
        return status;
    }
    shrewd_basic_dlist_node_t *fresh = block;
    fresh->data = data;
    fresh->prev = node;
    fresh->next = node->next;
    if (NULL != node->next) {
        node->next->prev = fresh;
    } else {
        list->tail = fresh;
    }
    node->next = fresh;
    ++list->size;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                     "insert_after ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_remove(shrewd_basic_dlist_t *list,
                              shrewd_basic_dlist_node_t *node, void **out)
{
    if ((NULL == list) || (NULL == node)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "remove: list or node is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL != out) {
        *out = node->data;
    }
    if (NULL != node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    if (NULL != node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }
    --list->size;
    int status = shrewd_basic_platform_free((void **)&node);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "dlist",
                         "remove: node free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "dlist",
                     "remove ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_size(const shrewd_basic_dlist_t *list, size_t *out)
{
    if ((NULL == list) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "size: list or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = list->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_dlist_empty(const shrewd_basic_dlist_t *list)
{
    return (NULL == list) || (0 == list->size);
}

int shrewd_basic_dlist_foreach(const shrewd_basic_dlist_t *list,
                               shrewd_basic_dlist_visit_fn visit, void *ctx)
{
    if ((NULL == list) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "foreach: list or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (const shrewd_basic_dlist_node_t *node = list->head; NULL != node; node = node->next) {
        int status = visit(node->data, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log,
                             "dlist", "foreach: visit aborted (%s)\n",
                             shrewd_basic_error_name(status));
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dlist_foreach_reverse(const shrewd_basic_dlist_t *list,
                                       shrewd_basic_dlist_visit_fn visit,
                                       void *ctx)
{
    if ((NULL == list) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "dlist",
                         "foreach_reverse: list or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (const shrewd_basic_dlist_node_t *node = list->tail; NULL != node; node = node->prev) {
        int status = visit(node->data, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log,
                             "dlist", "foreach_reverse: visit aborted (%s)\n",
                             shrewd_basic_error_name(status));
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

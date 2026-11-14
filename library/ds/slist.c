#include "shrewd-basic/ds/slist.h"

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared ds log module, defined in library/ds/ds.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log);

int shrewd_basic_slist_create(shrewd_basic_slist_t **out)
{
    if (NULL == out) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "create: out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_slist_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "slist",
                         "create: allocation failed\n");
        return status;
    }
    *out = block;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "slist",
                     "create ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_destroy(shrewd_basic_slist_t *list)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "destroy: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_slist_node_t *node = list->head;
    while (NULL != node) {
        shrewd_basic_slist_node_t *next = node->next;
        int status = shrewd_basic_platform_free((void **)&node);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log,
                             "slist", "destroy: node free failed\n");
            return status;
        }
        node = next;
    }
    int status = shrewd_basic_platform_free((void **)&list);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "slist",
                         "destroy: list free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "slist",
                     "destroy ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_push_front(shrewd_basic_slist_t *list, void *data)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "push_front: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_slist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "slist",
                         "push_front: allocation failed\n");
        return status;
    }
    shrewd_basic_slist_node_t *node = block;
    node->data = data;
    node->next = list->head;
    list->head = node;
    if (NULL == list->tail) {
        list->tail = node;
    }
    ++list->size;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "slist",
                     "push_front ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_push_back(shrewd_basic_slist_t *list, void *data)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "push_back: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_slist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "slist",
                         "push_back: allocation failed\n");
        return status;
    }
    shrewd_basic_slist_node_t *node = block;
    node->data = data;
    node->next = NULL;
    if (NULL != list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    ++list->size;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "slist",
                     "push_back ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_pop_front(shrewd_basic_slist_t *list, void **out)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "pop_front: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->head) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "slist",
                         "pop_front: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    shrewd_basic_slist_node_t *node = list->head;
    if (NULL != out) {
        *out = node->data;
    }
    list->head = node->next;
    if (NULL == list->head) {
        list->tail = NULL;
    }
    --list->size;
    int status = shrewd_basic_platform_free((void **)&node);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "slist",
                         "pop_front: node free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "slist",
                     "pop_front ok, size %zu\n", list->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_front(const shrewd_basic_slist_t *list, void **out)
{
    if ((NULL == list) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "front: list or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->head) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "slist",
                         "front: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = list->head->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_size(const shrewd_basic_slist_t *list, size_t *out)
{
    if ((NULL == list) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "size: list or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = list->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_slist_empty(const shrewd_basic_slist_t *list)
{
    return (NULL == list) || (0 == list->size);
}

int shrewd_basic_slist_foreach(const shrewd_basic_slist_t *list,
                               shrewd_basic_slist_visit_fn visit, void *ctx)
{
    if ((NULL == list) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "foreach: list or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (const shrewd_basic_slist_node_t *node = list->head; NULL != node; node = node->next) {
        int status = visit(node->data, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log,
                             "slist", "foreach: visit aborted (%s)\n",
                             shrewd_basic_error_name(status));
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_reverse(shrewd_basic_slist_t *list)
{
    if (NULL == list) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "slist",
                         "reverse: list is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_slist_node_t *prev = NULL;
    shrewd_basic_slist_node_t *node = list->head;
    list->tail = list->head;
    while (NULL != node) {
        shrewd_basic_slist_node_t *next = node->next;
        node->next = prev;
        prev = node;
        node = next;
    }
    list->head = prev;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "slist",
                     "reverse ok\n");
    return SHREWD_BASIC_ERR_OK;
}

#include "shrewd-basic/ds/slist.h"

#include "shrewd-basic/platform/memory.h"

int shrewd_basic_slist_create(shrewd_basic_slist_t **out)
{
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_slist_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_destroy(shrewd_basic_slist_t *list)
{
    if (NULL == list) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_slist_node_t *node = list->head;
    while (NULL != node) {
        shrewd_basic_slist_node_t *next = node->next;
        int status = shrewd_basic_platform_free((void **)&node);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        node = next;
    }
    return shrewd_basic_platform_free((void **)&list);
}

int shrewd_basic_slist_push_front(shrewd_basic_slist_t *list, void *data)
{
    if (NULL == list) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_slist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
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
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_push_back(shrewd_basic_slist_t *list, void *data)
{
    if (NULL == list) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_slist_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
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
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_pop_front(shrewd_basic_slist_t *list, void **out)
{
    if (NULL == list) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->head) {
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
    return shrewd_basic_platform_free((void **)&node);
}

int shrewd_basic_slist_front(const shrewd_basic_slist_t *list, void **out)
{
    if ((NULL == list) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == list->head) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = list->head->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_size(const shrewd_basic_slist_t *list, size_t *out)
{
    if ((NULL == list) || (NULL == out)) {
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
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (const shrewd_basic_slist_node_t *node = list->head; NULL != node; node = node->next) {
        int status = visit(node->data, ctx);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_slist_reverse(shrewd_basic_slist_t *list)
{
    if (NULL == list) {
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
    return SHREWD_BASIC_ERR_OK;
}

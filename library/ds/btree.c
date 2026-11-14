#include "shrewd-basic/ds/btree.h"

#include "shrewd-basic/platform/memory.h"

static shrewd_basic_btree_node_t *btree_min_node(shrewd_basic_btree_node_t *node)
{
    while (NULL != node->left) {
        node = node->left;
    }
    return node;
}

static shrewd_basic_btree_node_t *btree_max_node(shrewd_basic_btree_node_t *node)
{
    while (NULL != node->right) {
        node = node->right;
    }
    return node;
}

static int btree_free_node(shrewd_basic_btree_node_t *node)
{
    if (NULL == node) {
        return SHREWD_BASIC_ERR_OK;
    }
    int status = btree_free_node(node->left);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = btree_free_node(node->right);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_platform_free((void **)&node);
}

static int btree_foreach_node(const shrewd_basic_btree_node_t *node,
                              shrewd_basic_btree_visit_fn visit, void *ctx)
{
    if (NULL == node) {
        return SHREWD_BASIC_ERR_OK;
    }
    int status = btree_foreach_node(node->left, visit, ctx);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = visit(node->data, ctx);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return btree_foreach_node(node->right, visit, ctx);
}

/*
 * Recursive removal. *status is SHREWD_BASIC_ERR_OK when the key was removed,
 * SHREWD_BASIC_ERR_KEY_NOTFOUND when the (sub)tree is exhausted. The two-children
 * case copies the in-order successor's payload in place and drops the
 * successor node instead.
 */
static shrewd_basic_btree_node_t *btree_remove_node(shrewd_basic_btree_t *tree,
                                                    shrewd_basic_btree_node_t *node,
                                                    const void *key, void **out,
                                                    int *status)
{
    if (NULL == node) {
        *status = SHREWD_BASIC_ERR_KEY_NOTFOUND;
        return NULL;
    }
    int c = tree->cmp(key, node->data, tree->ctx);
    if (0 > c) {
        node->left = btree_remove_node(tree, node->left, key, out, status);
    } else if (0 < c) {
        node->right = btree_remove_node(tree, node->right, key, out, status);
    } else {
        *status = SHREWD_BASIC_ERR_OK;
        if (NULL != out) {
            *out = node->data;
        }
        if (NULL == node->left) {
            shrewd_basic_btree_node_t *right = node->right;
            *status = shrewd_basic_platform_free((void **)&node);
            return right;
        }
        if (NULL == node->right) {
            shrewd_basic_btree_node_t *left = node->left;
            *status = shrewd_basic_platform_free((void **)&node);
            return left;
        }
        shrewd_basic_btree_node_t *successor = btree_min_node(node->right);
        node->data = successor->data;
        node->right = btree_remove_node(tree, node->right, successor->data,
                                        NULL, status);
    }
    return node;
}

int shrewd_basic_btree_create(shrewd_basic_btree_cmp_fn cmp, void *ctx,
                              shrewd_basic_btree_t **out)
{
    if ((NULL == cmp) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_btree_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    shrewd_basic_btree_t *tree = block;
    tree->cmp = cmp;
    tree->ctx = ctx;
    *out = tree;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_btree_destroy(shrewd_basic_btree_t *tree)
{
    if (NULL == tree) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = btree_free_node(tree->root);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_platform_free((void **)&tree);
}

int shrewd_basic_btree_insert(shrewd_basic_btree_t *tree, void *data)
{
    if ((NULL == tree) || (NULL == tree->cmp)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_btree_node_t *parent = NULL;
    shrewd_basic_btree_node_t *node = tree->root;
    int c = 0;
    while (NULL != node) {
        parent = node;
        c = tree->cmp(data, node->data, tree->ctx);
        if (0 == c) {
            return SHREWD_BASIC_ERR_KEY_EXISTS;
        }
        node = (0 > c) ? node->left : node->right;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_btree_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    shrewd_basic_btree_node_t *fresh = block;
    fresh->data = data;
    fresh->left = NULL;
    fresh->right = NULL;
    if (NULL == parent) {
        tree->root = fresh;
    } else if (0 > c) {
        parent->left = fresh;
    } else {
        parent->right = fresh;
    }
    ++tree->size;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_btree_search(const shrewd_basic_btree_t *tree, const void *key,
                              void **out)
{
    if ((NULL == tree) || (NULL == tree->cmp) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_btree_node_t *node = tree->root;
    while (NULL != node) {
        int c = tree->cmp(key, node->data, tree->ctx);
        if (0 == c) {
            *out = node->data;
            return SHREWD_BASIC_ERR_OK;
        }
        node = (0 > c) ? node->left : node->right;
    }
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
}

int shrewd_basic_btree_remove(shrewd_basic_btree_t *tree, const void *key,
                              void **out)
{
    if ((NULL == tree) || (NULL == tree->cmp)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = SHREWD_BASIC_ERR_KEY_NOTFOUND;
    tree->root = btree_remove_node(tree, tree->root, key, out, &status);
    if (SHREWD_BASIC_ERR_OK == status) {
        --tree->size;
    }
    return status;
}

int shrewd_basic_btree_min(const shrewd_basic_btree_t *tree, void **out)
{
    if ((NULL == tree) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == tree->root) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = btree_min_node(tree->root)->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_btree_max(const shrewd_basic_btree_t *tree, void **out)
{
    if ((NULL == tree) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == tree->root) {
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = btree_max_node(tree->root)->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_btree_foreach(const shrewd_basic_btree_t *tree,
                               shrewd_basic_btree_visit_fn visit, void *ctx)
{
    if ((NULL == tree) || (NULL == visit)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    return btree_foreach_node(tree->root, visit, ctx);
}

int shrewd_basic_btree_size(const shrewd_basic_btree_t *tree, size_t *out)
{
    if ((NULL == tree) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = tree->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_btree_empty(const shrewd_basic_btree_t *tree)
{
    return (NULL == tree) || (0 == tree->size);
}

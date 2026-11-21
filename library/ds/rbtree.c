#include "shrewd-basic/ds/rbtree.h"

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared ds log module, defined in library/ds/ds.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log);

/*
 * Balancing follows the classic CLRS formulation with one sentinel node per
 * tree ("nil"): every missing child link points at it and it is black, so the
 * fixup loops never special-case NULL. The sentinel's parent is mutated while
 * deleting (it tracks the parent of the "hole" left by the removed node).
 */

static void rotate_left(shrewd_basic_rbtree_t *tree, shrewd_basic_rbtree_node_t *x)
{
    shrewd_basic_rbtree_node_t *y = x->right;
    x->right = y->left;
    if (tree->nil != y->left) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (tree->nil == x->parent) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

static void rotate_right(shrewd_basic_rbtree_t *tree, shrewd_basic_rbtree_node_t *x)
{
    shrewd_basic_rbtree_node_t *y = x->left;
    x->left = y->right;
    if (tree->nil != y->right) {
        y->right->parent = x;
    }
    y->parent = x->parent;
    if (tree->nil == x->parent) {
        tree->root = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }
    y->right = x;
    x->parent = y;
}

static shrewd_basic_rbtree_node_t *rbtree_min_node(shrewd_basic_rbtree_node_t *node,
                                                   shrewd_basic_rbtree_node_t *nil)
{
    while (nil != node->left) {
        node = node->left;
    }
    return node;
}

static shrewd_basic_rbtree_node_t *rbtree_max_node(shrewd_basic_rbtree_node_t *node,
                                                   shrewd_basic_rbtree_node_t *nil)
{
    while (nil != node->right) {
        node = node->right;
    }
    return node;
}

static int rbtree_free_node(shrewd_basic_rbtree_t *tree,
                            shrewd_basic_rbtree_node_t *node)
{
    if (tree->nil == node) {
        return SHREWD_BASIC_ERR_OK;
    }
    int status = rbtree_free_node(tree, node->left);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = rbtree_free_node(tree, node->right);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_platform_free((void **)&node);
}

static int rbtree_foreach_node(const shrewd_basic_rbtree_t *tree,
                               const shrewd_basic_rbtree_node_t *node,
                               shrewd_basic_rbtree_visit_fn visit, void *ctx)
{
    if (tree->nil == node) {
        return SHREWD_BASIC_ERR_OK;
    }
    int status = rbtree_foreach_node(tree, node->left, visit, ctx);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = visit(node->data, ctx);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return rbtree_foreach_node(tree, node->right, visit, ctx);
}

static void rbtree_insert_fixup(shrewd_basic_rbtree_t *tree,
                                shrewd_basic_rbtree_node_t *z)
{
    while (SHREWD_BASIC_RBTREE_RED == z->parent->color) {
        if (z->parent == z->parent->parent->left) {
            shrewd_basic_rbtree_node_t *y = z->parent->parent->right;
            if (SHREWD_BASIC_RBTREE_RED == y->color) {
                z->parent->color = SHREWD_BASIC_RBTREE_BLACK;
                y->color = SHREWD_BASIC_RBTREE_BLACK;
                z->parent->parent->color = SHREWD_BASIC_RBTREE_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rotate_left(tree, z);
                }
                z->parent->color = SHREWD_BASIC_RBTREE_BLACK;
                z->parent->parent->color = SHREWD_BASIC_RBTREE_RED;
                rotate_right(tree, z->parent->parent);
            }
        } else {
            shrewd_basic_rbtree_node_t *y = z->parent->parent->left;
            if (SHREWD_BASIC_RBTREE_RED == y->color) {
                z->parent->color = SHREWD_BASIC_RBTREE_BLACK;
                y->color = SHREWD_BASIC_RBTREE_BLACK;
                z->parent->parent->color = SHREWD_BASIC_RBTREE_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(tree, z);
                }
                z->parent->color = SHREWD_BASIC_RBTREE_BLACK;
                z->parent->parent->color = SHREWD_BASIC_RBTREE_RED;
                rotate_left(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = SHREWD_BASIC_RBTREE_BLACK;
}

static void rbtree_transplant(shrewd_basic_rbtree_t *tree,
                              shrewd_basic_rbtree_node_t *u,
                              shrewd_basic_rbtree_node_t *v)
{
    if (tree->nil == u->parent) {
        tree->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    v->parent = u->parent;
}

static void rbtree_delete_fixup(shrewd_basic_rbtree_t *tree,
                                shrewd_basic_rbtree_node_t *x)
{
    while ((x != tree->root) && (SHREWD_BASIC_RBTREE_BLACK == x->color)) {
        if (x == x->parent->left) {
            shrewd_basic_rbtree_node_t *w = x->parent->right;
            if (SHREWD_BASIC_RBTREE_RED == w->color) {
                w->color = SHREWD_BASIC_RBTREE_BLACK;
                x->parent->color = SHREWD_BASIC_RBTREE_RED;
                rotate_left(tree, x->parent);
                w = x->parent->right;
            }
            if ((SHREWD_BASIC_RBTREE_BLACK == w->left->color)
                    && (SHREWD_BASIC_RBTREE_BLACK == w->right->color)) {
                w->color = SHREWD_BASIC_RBTREE_RED;
                x = x->parent;
            } else {
                if (SHREWD_BASIC_RBTREE_BLACK == w->right->color) {
                    w->left->color = SHREWD_BASIC_RBTREE_BLACK;
                    w->color = SHREWD_BASIC_RBTREE_RED;
                    rotate_right(tree, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = SHREWD_BASIC_RBTREE_BLACK;
                w->right->color = SHREWD_BASIC_RBTREE_BLACK;
                rotate_left(tree, x->parent);
                x = tree->root;
            }
        } else {
            shrewd_basic_rbtree_node_t *w = x->parent->left;
            if (SHREWD_BASIC_RBTREE_RED == w->color) {
                w->color = SHREWD_BASIC_RBTREE_BLACK;
                x->parent->color = SHREWD_BASIC_RBTREE_RED;
                rotate_right(tree, x->parent);
                w = x->parent->left;
            }
            if ((SHREWD_BASIC_RBTREE_BLACK == w->right->color)
                    && (SHREWD_BASIC_RBTREE_BLACK == w->left->color)) {
                w->color = SHREWD_BASIC_RBTREE_RED;
                x = x->parent;
            } else {
                if (SHREWD_BASIC_RBTREE_BLACK == w->left->color) {
                    w->right->color = SHREWD_BASIC_RBTREE_BLACK;
                    w->color = SHREWD_BASIC_RBTREE_RED;
                    rotate_left(tree, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = SHREWD_BASIC_RBTREE_BLACK;
                w->left->color = SHREWD_BASIC_RBTREE_BLACK;
                rotate_right(tree, x->parent);
                x = tree->root;
            }
        }
    }
    x->color = SHREWD_BASIC_RBTREE_BLACK;
}

int shrewd_basic_rbtree_create(shrewd_basic_rbtree_cmp_fn cmp, void *ctx,
                               shrewd_basic_rbtree_t **out)
{
    if ((NULL == cmp) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "create: cmp or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_rbtree_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "rbtree",
                         "create: allocation failed\n");
        return status;
    }
    shrewd_basic_rbtree_t *tree = block;
    status = shrewd_basic_platform_calloc(&block, 1, sizeof(shrewd_basic_rbtree_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "rbtree",
                         "create: sentinel allocation failed\n");
        (void)shrewd_basic_platform_free((void **)&tree);
        return status;
    }
    tree->nil = block;
    tree->nil->data = NULL;
    tree->nil->left = tree->nil;
    tree->nil->right = tree->nil;
    tree->nil->parent = tree->nil;
    tree->nil->color = SHREWD_BASIC_RBTREE_BLACK;
    tree->root = tree->nil;
    tree->cmp = cmp;
    tree->ctx = ctx;
    *out = tree;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "rbtree",
                     "create ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_rbtree_destroy(shrewd_basic_rbtree_t *tree)
{
    if (NULL == tree) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "destroy: tree is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = rbtree_free_node(tree, tree->root);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "rbtree",
                         "destroy: node free failed\n");
        return status;
    }
    status = shrewd_basic_platform_free((void **)&tree->nil);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "rbtree",
                         "destroy: sentinel free failed\n");
        return status;
    }
    status = shrewd_basic_platform_free((void **)&tree);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "rbtree",
                         "destroy: tree free failed\n");
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, shrewd_basic_ds_log, "rbtree",
                     "destroy ok\n");
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_rbtree_insert(shrewd_basic_rbtree_t *tree, void *data)
{
    if ((NULL == tree) || (NULL == tree->cmp)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "insert: tree or cmp is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_rbtree_node_t *parent = tree->nil;
    shrewd_basic_rbtree_node_t *node = tree->root;
    int c = 0;
    while (tree->nil != node) {
        parent = node;
        c = tree->cmp(data, node->data, tree->ctx);
        if (0 == c) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log,
                             "rbtree", "insert: key exists\n");
            return SHREWD_BASIC_ERR_KEY_EXISTS;
        }
        node = (0 > c) ? node->left : node->right;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_rbtree_node_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "rbtree",
                         "insert: allocation failed\n");
        return status;
    }
    shrewd_basic_rbtree_node_t *fresh = block;
    fresh->data = data;
    fresh->left = tree->nil;
    fresh->right = tree->nil;
    fresh->color = SHREWD_BASIC_RBTREE_RED;
    fresh->parent = parent;
    if (tree->nil == parent) {
        tree->root = fresh;
    } else if (0 > c) {
        parent->left = fresh;
    } else {
        parent->right = fresh;
    }
    ++tree->size;
    rbtree_insert_fixup(tree, fresh);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "rbtree",
                     "insert ok, size %zu\n", tree->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_rbtree_search(const shrewd_basic_rbtree_t *tree, const void *key,
                               void **out)
{
    if ((NULL == tree) || (NULL == tree->cmp) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "search: tree, cmp or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_rbtree_node_t *node = tree->root;
    while (tree->nil != node) {
        int c = tree->cmp(key, node->data, tree->ctx);
        if (0 == c) {
            *out = node->data;
            return SHREWD_BASIC_ERR_OK;
        }
        node = (0 > c) ? node->left : node->right;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "rbtree",
                     "search: key not found\n");
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
}

int shrewd_basic_rbtree_remove(shrewd_basic_rbtree_t *tree, const void *key,
                               void **out)
{
    if ((NULL == tree) || (NULL == tree->cmp)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "remove: tree or cmp is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_rbtree_node_t *z = tree->root;
    while (tree->nil != z) {
        int c = tree->cmp(key, z->data, tree->ctx);
        if (0 == c) {
            break;
        }
        z = (0 > c) ? z->left : z->right;
    }
    if (tree->nil == z) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "rbtree",
                         "remove: key not found\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    if (NULL != out) {
        *out = z->data;
    }

    shrewd_basic_rbtree_node_t *y = z;
    shrewd_basic_rbtree_color_t y_color = y->color;
    shrewd_basic_rbtree_node_t *x = NULL;
    if (tree->nil == z->left) {
        x = z->right;
        rbtree_transplant(tree, z, z->right);
    } else if (tree->nil == z->right) {
        x = z->left;
        rbtree_transplant(tree, z, z->left);
    } else {
        y = rbtree_min_node(z->right, tree->nil);
        y_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
        } else {
            rbtree_transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        rbtree_transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }
    int status = shrewd_basic_platform_free((void **)&z);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_ds_log, "rbtree",
                         "remove: node free failed\n");
        return status;
    }
    --tree->size;
    if (SHREWD_BASIC_RBTREE_BLACK == y_color) {
        rbtree_delete_fixup(tree, x);
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "rbtree",
                     "remove ok, size %zu\n", tree->size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_rbtree_min(const shrewd_basic_rbtree_t *tree, void **out)
{
    if ((NULL == tree) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "min: tree or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (tree->nil == tree->root) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "rbtree",
                         "min: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = rbtree_min_node(tree->root, tree->nil)->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_rbtree_max(const shrewd_basic_rbtree_t *tree, void **out)
{
    if ((NULL == tree) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "max: tree or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (tree->nil == tree->root) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_ds_log, "rbtree",
                         "max: empty\n");
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = rbtree_max_node(tree->root, tree->nil)->data;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_rbtree_foreach(const shrewd_basic_rbtree_t *tree,
                                shrewd_basic_rbtree_visit_fn visit, void *ctx)
{
    if ((NULL == tree) || (NULL == visit)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "foreach: tree or visit is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    return rbtree_foreach_node(tree, tree->root, visit, ctx);
}

int shrewd_basic_rbtree_size(const shrewd_basic_rbtree_t *tree, size_t *out)
{
    if ((NULL == tree) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_ds_log, "rbtree",
                         "size: tree or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = tree->size;
    return SHREWD_BASIC_ERR_OK;
}

bool shrewd_basic_rbtree_empty(const shrewd_basic_rbtree_t *tree)
{
    return (NULL == tree) || (0 == tree->size);
}

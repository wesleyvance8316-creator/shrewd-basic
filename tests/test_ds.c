#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "shrewd-basic/ds/btree.h"
#include "shrewd-basic/ds/dlist.h"
#include "shrewd-basic/ds/queue.h"
#include "shrewd-basic/ds/rbtree.h"
#include "shrewd-basic/ds/slist.h"
#include "shrewd-basic/ds/stack.h"

static int g_failures = 0;

static void expect_eq_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, actual, expected);
        ++g_failures;
    }
}

static void expect_true(const char *name, int condition)
{
    if (0 == condition) {
        fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

/* Integer payloads smuggled through void*. */
static void *iv(int value)
{
    return (void *)(intptr_t)value;
}

static int pv(const void *payload)
{
    return (int)(intptr_t)payload;
}

static int cmp_int(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    int x = pv(a);
    int y = pv(b);
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* foreach collector. */
struct collect_ctx {
    void *items[128];
    size_t n;
};

static int collect(void *data, void *ctx)
{
    struct collect_ctx *c = ctx;
    if (c->n < sizeof(c->items) / sizeof(c->items[0])) {
        c->items[c->n] = data;
        ++c->n;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Stops the walk with a non-OK code once payload 2 is reached. */
static int collect_stop_at_2(void *data, void *ctx)
{
    int status = collect(data, ctx);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return (2 == pv(data)) ? SHREWD_BASIC_ERR_KEY_EXISTS : SHREWD_BASIC_ERR_OK;
}

static int collected_sorted(const struct collect_ctx *c)
{
    for (size_t i = 1; i < c->n; ++i) {
        if (pv(c->items[i - 1]) >= pv(c->items[i])) {
            return 0;
        }
    }
    return 1;
}

static void test_slist(void)
{
    shrewd_basic_slist_t *list = NULL;
    expect_eq_int("slist create", shrewd_basic_slist_create(&list), SHREWD_BASIC_ERR_OK);
    expect_true("slist create out", NULL != list);
    expect_eq_int("slist create rejects NULL out", shrewd_basic_slist_create(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_true("slist empty at start", shrewd_basic_slist_empty(list));

    expect_eq_int("slist push_back 1", shrewd_basic_slist_push_back(list, iv(1)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist push_back 2", shrewd_basic_slist_push_back(list, iv(2)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist push_back 3", shrewd_basic_slist_push_back(list, iv(3)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist push_front 0", shrewd_basic_slist_push_front(list, iv(0)),
                  SHREWD_BASIC_ERR_OK);

    size_t size = 0;
    expect_eq_int("slist size status", shrewd_basic_slist_size(list, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("slist size", 4 == size);

    void *out = NULL;
    expect_eq_int("slist front status", shrewd_basic_slist_front(list, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist front", pv(out), 0);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int("slist foreach", shrewd_basic_slist_foreach(list, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("slist order 0 1 2 3",
                (4 == c.n) && (0 == pv(c.items[0])) && (1 == pv(c.items[1]))
                && (2 == pv(c.items[2])) && (3 == pv(c.items[3])));

    expect_eq_int("slist reverse", shrewd_basic_slist_reverse(list), SHREWD_BASIC_ERR_OK);
    c.n = 0;
    expect_eq_int("slist foreach reversed", shrewd_basic_slist_foreach(list, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("slist reversed 3 2 1 0",
                (4 == c.n) && (3 == pv(c.items[0])) && (0 == pv(c.items[3])));
    expect_eq_int("slist reverse back", shrewd_basic_slist_reverse(list), SHREWD_BASIC_ERR_OK);

    expect_eq_int("slist pop_front", shrewd_basic_slist_pop_front(list, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist popped 0", pv(out), 0);
    expect_eq_int("slist pop_front discard", shrewd_basic_slist_pop_front(list, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist size status after pops", shrewd_basic_slist_size(list, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("slist size after pops", 2 == size);

    /* foreach stops and propagates the visitor's code. */
    struct collect_ctx stopped = {{0}, 0};
    expect_eq_int("slist foreach stop",
                  shrewd_basic_slist_foreach(list, collect_stop_at_2, &stopped),
                  SHREWD_BASIC_ERR_KEY_EXISTS);
    expect_true("slist foreach stopped early",
                (1 == stopped.n) && (2 == pv(stopped.items[0])));

    while (0 == shrewd_basic_slist_empty(list)) {
        expect_eq_int("slist drain", shrewd_basic_slist_pop_front(list, NULL),
                      SHREWD_BASIC_ERR_OK);
    }
    out = NULL;
    expect_eq_int("slist pop on empty", shrewd_basic_slist_pop_front(list, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("slist front on empty", shrewd_basic_slist_front(list, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("slist destroy", shrewd_basic_slist_destroy(list), SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist destroy NULL", shrewd_basic_slist_destroy(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_dlist(void)
{
    shrewd_basic_dlist_t *list = NULL;
    expect_eq_int("dlist create", shrewd_basic_dlist_create(&list), SHREWD_BASIC_ERR_OK);
    expect_true("dlist create out", NULL != list);

    expect_eq_int("dlist push_back 1", shrewd_basic_dlist_push_back(list, iv(1)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist push_back 2", shrewd_basic_dlist_push_back(list, iv(2)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist push_back 3", shrewd_basic_dlist_push_back(list, iv(3)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist push_front 0", shrewd_basic_dlist_push_front(list, iv(0)),
                  SHREWD_BASIC_ERR_OK);

    size_t size = 0;
    expect_eq_int("dlist size status", shrewd_basic_dlist_size(list, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dlist size", 4 == size);

    void *out = NULL;
    expect_eq_int("dlist front status", shrewd_basic_dlist_front(list, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist front", pv(out), 0);
    expect_eq_int("dlist back status", shrewd_basic_dlist_back(list, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist back", pv(out), 3);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int("dlist foreach_reverse",
                  shrewd_basic_dlist_foreach_reverse(list, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dlist reverse order 3 2 1 0",
                (4 == c.n) && (3 == pv(c.items[0])) && (2 == pv(c.items[1]))
                && (1 == pv(c.items[2])) && (0 == pv(c.items[3])));

    expect_eq_int("dlist insert_after",
                  shrewd_basic_dlist_insert_after(list, list->head, iv(9)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist insert_before",
                  shrewd_basic_dlist_insert_before(list, list->tail, iv(8)),
                  SHREWD_BASIC_ERR_OK);

    shrewd_basic_dlist_node_t *victim = list->head->next->next;
    expect_eq_int("dlist remove", shrewd_basic_dlist_remove(list, victim, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist removed payload", pv(out), 1);
    expect_eq_int("dlist size status after remove", shrewd_basic_dlist_size(list, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dlist size after remove", 5 == size);

    c.n = 0;
    expect_eq_int("dlist foreach", shrewd_basic_dlist_foreach(list, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dlist order 0 9 2 8 3",
                (5 == c.n) && (0 == pv(c.items[0])) && (9 == pv(c.items[1]))
                && (2 == pv(c.items[2])) && (8 == pv(c.items[3]))
                && (3 == pv(c.items[4])));

    expect_eq_int("dlist pop_front", shrewd_basic_dlist_pop_front(list, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist popped front", pv(out), 0);
    expect_eq_int("dlist pop_back", shrewd_basic_dlist_pop_back(list, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist popped back", pv(out), 3);

    while (0 == shrewd_basic_dlist_empty(list)) {
        expect_eq_int("dlist drain", shrewd_basic_dlist_pop_back(list, NULL),
                      SHREWD_BASIC_ERR_OK);
    }
    expect_eq_int("dlist pop on empty", shrewd_basic_dlist_pop_back(list, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("dlist destroy", shrewd_basic_dlist_destroy(list), SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist destroy NULL", shrewd_basic_dlist_destroy(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_queue(void)
{
    shrewd_basic_queue_t *queue = NULL;
    expect_eq_int("queue create", shrewd_basic_queue_create(&queue), SHREWD_BASIC_ERR_OK);
    expect_true("queue create out", NULL != queue);

    expect_eq_int("queue enqueue 1", shrewd_basic_queue_enqueue(queue, iv(1)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue enqueue 2", shrewd_basic_queue_enqueue(queue, iv(2)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue enqueue 3", shrewd_basic_queue_enqueue(queue, iv(3)),
                  SHREWD_BASIC_ERR_OK);

    size_t size = 0;
    expect_eq_int("queue size status", shrewd_basic_queue_size(queue, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("queue size", 3 == size);

    void *out = NULL;
    expect_eq_int("queue front status", shrewd_basic_queue_front(queue, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue front", pv(out), 1);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int("queue foreach", shrewd_basic_queue_foreach(queue, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("queue order 1 2 3",
                (3 == c.n) && (1 == pv(c.items[0])) && (3 == pv(c.items[2])));

    expect_eq_int("queue dequeue 1", shrewd_basic_queue_dequeue(queue, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue dequeued 1", pv(out), 1);
    expect_eq_int("queue dequeue 2", shrewd_basic_queue_dequeue(queue, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue dequeued 2", pv(out), 2);
    expect_eq_int("queue dequeue 3", shrewd_basic_queue_dequeue(queue, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue dequeued 3", pv(out), 3);
    expect_true("queue empty", shrewd_basic_queue_empty(queue));
    expect_eq_int("queue dequeue on empty", shrewd_basic_queue_dequeue(queue, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("queue destroy", shrewd_basic_queue_destroy(queue), SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue destroy NULL", shrewd_basic_queue_destroy(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_stack(void)
{
    shrewd_basic_stack_t *stack = NULL;
    expect_eq_int("stack create", shrewd_basic_stack_create(&stack), SHREWD_BASIC_ERR_OK);
    expect_true("stack create out", NULL != stack);

    expect_eq_int("stack push 1", shrewd_basic_stack_push(stack, iv(1)), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack push 2", shrewd_basic_stack_push(stack, iv(2)), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack push 3", shrewd_basic_stack_push(stack, iv(3)), SHREWD_BASIC_ERR_OK);

    size_t size = 0;
    expect_eq_int("stack size status", shrewd_basic_stack_size(stack, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("stack size", 3 == size);

    void *out = NULL;
    expect_eq_int("stack top status", shrewd_basic_stack_top(stack, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack top", pv(out), 3);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int("stack foreach", shrewd_basic_stack_foreach(stack, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("stack bottom to top 1 2 3",
                (3 == c.n) && (1 == pv(c.items[0])) && (3 == pv(c.items[2])));

    expect_eq_int("stack pop", shrewd_basic_stack_pop(stack, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack popped 3", pv(out), 3);
    expect_eq_int("stack pop again", shrewd_basic_stack_pop(stack, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack popped 2", pv(out), 2);
    expect_eq_int("stack top now status", shrewd_basic_stack_top(stack, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack top now", pv(out), 1);
    expect_eq_int("stack pop last", shrewd_basic_stack_pop(stack, NULL), SHREWD_BASIC_ERR_OK);
    expect_true("stack empty", shrewd_basic_stack_empty(stack));
    expect_eq_int("stack pop on empty", shrewd_basic_stack_pop(stack, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("stack top on empty", shrewd_basic_stack_top(stack, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("stack destroy", shrewd_basic_stack_destroy(stack), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack destroy NULL", shrewd_basic_stack_destroy(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_btree(void)
{
    shrewd_basic_btree_t *tree = NULL;
    expect_eq_int("btree rejects NULL cmp",
                  shrewd_basic_btree_create(NULL, NULL, &tree), SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("btree create", shrewd_basic_btree_create(cmp_int, NULL, &tree),
                  SHREWD_BASIC_ERR_OK);
    expect_true("btree create out", NULL != tree);

    const int values[] = {5, 3, 8, 1, 4, 7, 9};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        expect_eq_int("btree insert", shrewd_basic_btree_insert(tree, iv(values[i])),
                      SHREWD_BASIC_ERR_OK);
    }
    size_t size = 0;
    expect_eq_int("btree size status", shrewd_basic_btree_size(tree, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("btree size", 7 == size);
    expect_eq_int("btree duplicate insert", shrewd_basic_btree_insert(tree, iv(4)),
                  SHREWD_BASIC_ERR_KEY_EXISTS);

    void *out = NULL;
    expect_eq_int("btree search hit", shrewd_basic_btree_search(tree, iv(4), &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree search payload", pv(out), 4);
    expect_eq_int("btree search miss", shrewd_basic_btree_search(tree, iv(6), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("btree min status", shrewd_basic_btree_min(tree, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree min", pv(out), 1);
    expect_eq_int("btree max status", shrewd_basic_btree_max(tree, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree max", pv(out), 9);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int("btree foreach", shrewd_basic_btree_foreach(tree, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("btree in-order sorted",
                (7 == c.n) && collected_sorted(&c)
                && (1 == pv(c.items[0])) && (9 == pv(c.items[6])));

    /* 3 has two children; 1 is a leaf; 8 has two children. */
    expect_eq_int("btree remove two children", shrewd_basic_btree_remove(tree, iv(3), &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree removed payload", pv(out), 3);
    expect_eq_int("btree remove leaf", shrewd_basic_btree_remove(tree, iv(1), NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree remove subtree root", shrewd_basic_btree_remove(tree, iv(8), NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree remove missing", shrewd_basic_btree_remove(tree, iv(42), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("btree size status after removes", shrewd_basic_btree_size(tree, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("btree size after removes", 4 == size);
    expect_eq_int("btree search removed", shrewd_basic_btree_search(tree, iv(3), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("btree min after removes", shrewd_basic_btree_min(tree, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree min after removes payload", pv(out), 4);
    expect_eq_int("btree max after removes", shrewd_basic_btree_max(tree, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree max after removes payload", pv(out), 9);

    c.n = 0;
    expect_eq_int("btree foreach after removes",
                  shrewd_basic_btree_foreach(tree, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("btree remaining 4 5 7 9",
                (4 == c.n) && (4 == pv(c.items[0])) && (5 == pv(c.items[1]))
                && (7 == pv(c.items[2])) && (9 == pv(c.items[3])));

    expect_eq_int("btree destroy", shrewd_basic_btree_destroy(tree), SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree destroy NULL", shrewd_basic_btree_destroy(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

/* Returns -1 on an invariant violation, else the black height of the subtree. */
static int rb_black_height(const shrewd_basic_rbtree_t *tree,
                           const shrewd_basic_rbtree_node_t *node)
{
    if (node == tree->nil) {
        return 0;
    }
    if ((SHREWD_BASIC_RBTREE_RED == node->color)
            && ((SHREWD_BASIC_RBTREE_RED == node->left->color)
                || (SHREWD_BASIC_RBTREE_RED == node->right->color))) {
        return -1;
    }
    int left_height = rb_black_height(tree, node->left);
    if (0 > left_height) {
        return -1;
    }
    int right_height = rb_black_height(tree, node->right);
    if (0 > right_height) {
        return -1;
    }
    if (left_height != right_height) {
        return -1;
    }
    return left_height + ((SHREWD_BASIC_RBTREE_BLACK == node->color) ? 1 : 0);
}

static int rb_invariants_ok(const shrewd_basic_rbtree_t *tree)
{
    if (tree->root == tree->nil) {
        return 1;
    }
    return ((SHREWD_BASIC_RBTREE_BLACK == tree->root->color)
            && (0 <= rb_black_height(tree, tree->root)));
}

static void test_rbtree(void)
{
    shrewd_basic_rbtree_t *tree = NULL;
    expect_eq_int("rbtree create", shrewd_basic_rbtree_create(cmp_int, NULL, &tree),
                  SHREWD_BASIC_ERR_OK);
    expect_true("rbtree create out", NULL != tree);

    /* (i * 37) % 64 permutes 0..63: a fixed "shuffled" insertion order. */
    for (int i = 0; 64 > i; ++i) {
        expect_eq_int("rbtree insert",
                      shrewd_basic_rbtree_insert(tree, iv((i * 37) % 64)),
                      SHREWD_BASIC_ERR_OK);
    }
    size_t size = 0;
    expect_eq_int("rbtree size status", shrewd_basic_rbtree_size(tree, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("rbtree size", 64 == size);
    expect_eq_int("rbtree duplicate insert", shrewd_basic_rbtree_insert(tree, iv(5)),
                  SHREWD_BASIC_ERR_KEY_EXISTS);
    expect_true("rbtree invariants after inserts", rb_invariants_ok(tree));

    int found_all = 1;
    for (int v = 0; 64 > v; ++v) {
        void *out = NULL;
        if ((SHREWD_BASIC_ERR_OK != shrewd_basic_rbtree_search(tree, iv(v), &out))
                || (v != pv(out))) {
            found_all = 0;
        }
    }
    expect_true("rbtree search all keys", found_all);

    void *out = NULL;
    expect_eq_int("rbtree search miss", shrewd_basic_rbtree_search(tree, iv(100), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("rbtree min status", shrewd_basic_rbtree_min(tree, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("rbtree min", pv(out), 0);
    expect_eq_int("rbtree max status", shrewd_basic_rbtree_max(tree, &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("rbtree max", pv(out), 63);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int("rbtree foreach", shrewd_basic_rbtree_foreach(tree, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("rbtree in-order sorted 0..63",
                (64 == c.n) && collected_sorted(&c)
                && (0 == pv(c.items[0])) && (63 == pv(c.items[63])));

    /* Remove every third key; the invariants must hold after each removal. */
    int removed = 0;
    int consistent = 1;
    for (int v = 0; 64 > v; v += 3) {
        void *payload = NULL;
        if ((SHREWD_BASIC_ERR_OK != shrewd_basic_rbtree_remove(tree, iv(v), &payload))
                || (v != pv(payload))) {
            consistent = 0;
        }
        ++removed;
        if (0 == rb_invariants_ok(tree)) {
            consistent = 0;
        }
    }
    expect_true("rbtree removes succeed", consistent);
    expect_eq_int("rbtree size status after removes", shrewd_basic_rbtree_size(tree, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("rbtree size after removes", size == (size_t)(64 - removed));

    int remaining_ok = 1;
    for (int v = 0; 64 > v; ++v) {
        void *hit = NULL;
        int status = shrewd_basic_rbtree_search(tree, iv(v), &hit);
        if ((0 == (v % 3)) != (SHREWD_BASIC_ERR_KEY_NOTFOUND == status)) {
            remaining_ok = 0;
        }
    }
    expect_true("rbtree remaining keys", remaining_ok);

    c.n = 0;
    expect_eq_int("rbtree foreach after removes",
                  shrewd_basic_rbtree_foreach(tree, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("rbtree still sorted",
                (c.n == (size_t)(64 - removed)) && collected_sorted(&c));
    expect_true("rbtree invariants after removes", rb_invariants_ok(tree));

    expect_eq_int("rbtree destroy", shrewd_basic_rbtree_destroy(tree), SHREWD_BASIC_ERR_OK);
    expect_eq_int("rbtree destroy NULL", shrewd_basic_rbtree_destroy(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

int main(void)
{
    test_slist();
    test_dlist();
    test_queue();
    test_stack();
    test_btree();
    test_rbtree();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

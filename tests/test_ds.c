#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/ds/btree.h"
#include "shrewd-basic/ds/dlist.h"
#include "shrewd-basic/ds/queue.h"
#include "shrewd-basic/ds/radix.h"
#include "shrewd-basic/ds/rbtree.h"
#include "shrewd-basic/ds/slist.h"
#include "shrewd-basic/ds/stack.h"
#include "shrewd-basic/ds/trie.h"

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
    expect_eq_int("slist create rejects NULL out",
                  shrewd_basic_slist_create(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_true("slist empty at start", shrewd_basic_slist_empty(list));

    expect_eq_int(
        "slist push_back 1", shrewd_basic_slist_push_back(list, iv(1)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "slist push_back 2", shrewd_basic_slist_push_back(list, iv(2)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "slist push_back 3", shrewd_basic_slist_push_back(list, iv(3)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "slist push_front 0", shrewd_basic_slist_push_front(list, iv(0)), SHREWD_BASIC_ERR_OK);

    size_t size = 0;
    expect_eq_int("slist size status", shrewd_basic_slist_size(list, &size), SHREWD_BASIC_ERR_OK);
    expect_true("slist size", 4 == size);

    void *out = NULL;
    expect_eq_int("slist front status", shrewd_basic_slist_front(list, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist front", pv(out), 0);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int(
        "slist foreach", shrewd_basic_slist_foreach(list, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("slist order 0 1 2 3",
                (4 == c.n) && (0 == pv(c.items[0])) && (1 == pv(c.items[1]))
                    && (2 == pv(c.items[2])) && (3 == pv(c.items[3])));

    expect_eq_int("slist reverse", shrewd_basic_slist_reverse(list), SHREWD_BASIC_ERR_OK);
    c.n = 0;
    expect_eq_int("slist foreach reversed",
                  shrewd_basic_slist_foreach(list, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("slist reversed 3 2 1 0",
                (4 == c.n) && (3 == pv(c.items[0])) && (0 == pv(c.items[3])));
    expect_eq_int("slist reverse back", shrewd_basic_slist_reverse(list), SHREWD_BASIC_ERR_OK);

    expect_eq_int("slist pop_front", shrewd_basic_slist_pop_front(list, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("slist popped 0", pv(out), 0);
    expect_eq_int(
        "slist pop_front discard", shrewd_basic_slist_pop_front(list, NULL), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "slist size status after pops", shrewd_basic_slist_size(list, &size), SHREWD_BASIC_ERR_OK);
    expect_true("slist size after pops", 2 == size);

    /* foreach stops and propagates the visitor's code. */
    struct collect_ctx stopped = {{0}, 0};
    expect_eq_int("slist foreach stop",
                  shrewd_basic_slist_foreach(list, collect_stop_at_2, &stopped),
                  SHREWD_BASIC_ERR_KEY_EXISTS);
    expect_true("slist foreach stopped early", (1 == stopped.n) && (2 == pv(stopped.items[0])));

    while (0 == shrewd_basic_slist_empty(list)) {
        expect_eq_int("slist drain", shrewd_basic_slist_pop_front(list, NULL), SHREWD_BASIC_ERR_OK);
    }
    out = NULL;
    expect_eq_int("slist pop on empty",
                  shrewd_basic_slist_pop_front(list, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("slist front on empty",
                  shrewd_basic_slist_front(list, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("slist destroy", shrewd_basic_slist_destroy(list), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "slist destroy NULL", shrewd_basic_slist_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_dlist(void)
{
    shrewd_basic_dlist_t *list = NULL;
    expect_eq_int("dlist create", shrewd_basic_dlist_create(&list), SHREWD_BASIC_ERR_OK);
    expect_true("dlist create out", NULL != list);

    expect_eq_int(
        "dlist push_back 1", shrewd_basic_dlist_push_back(list, iv(1)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "dlist push_back 2", shrewd_basic_dlist_push_back(list, iv(2)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "dlist push_back 3", shrewd_basic_dlist_push_back(list, iv(3)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "dlist push_front 0", shrewd_basic_dlist_push_front(list, iv(0)), SHREWD_BASIC_ERR_OK);

    size_t size = 0;
    expect_eq_int("dlist size status", shrewd_basic_dlist_size(list, &size), SHREWD_BASIC_ERR_OK);
    expect_true("dlist size", 4 == size);

    void *out = NULL;
    expect_eq_int("dlist front status", shrewd_basic_dlist_front(list, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist front", pv(out), 0);
    expect_eq_int("dlist back status", shrewd_basic_dlist_back(list, &out), SHREWD_BASIC_ERR_OK);
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
    expect_eq_int(
        "dlist remove", shrewd_basic_dlist_remove(list, victim, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist removed payload", pv(out), 1);
    expect_eq_int("dlist size status after remove",
                  shrewd_basic_dlist_size(list, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dlist size after remove", 5 == size);

    c.n = 0;
    expect_eq_int(
        "dlist foreach", shrewd_basic_dlist_foreach(list, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("dlist order 0 9 2 8 3",
                (5 == c.n) && (0 == pv(c.items[0])) && (9 == pv(c.items[1]))
                    && (2 == pv(c.items[2])) && (8 == pv(c.items[3])) && (3 == pv(c.items[4])));

    expect_eq_int("dlist pop_front", shrewd_basic_dlist_pop_front(list, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist popped front", pv(out), 0);
    expect_eq_int("dlist pop_back", shrewd_basic_dlist_pop_back(list, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("dlist popped back", pv(out), 3);

    while (0 == shrewd_basic_dlist_empty(list)) {
        expect_eq_int("dlist drain", shrewd_basic_dlist_pop_back(list, NULL), SHREWD_BASIC_ERR_OK);
    }
    expect_eq_int("dlist pop on empty",
                  shrewd_basic_dlist_pop_back(list, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("dlist destroy", shrewd_basic_dlist_destroy(list), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "dlist destroy NULL", shrewd_basic_dlist_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_queue(void)
{
    shrewd_basic_queue_t *queue = NULL;
    expect_eq_int("queue create", shrewd_basic_queue_create(&queue), SHREWD_BASIC_ERR_OK);
    expect_true("queue create out", NULL != queue);

    expect_eq_int("queue enqueue 1", shrewd_basic_queue_enqueue(queue, iv(1)), SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue enqueue 2", shrewd_basic_queue_enqueue(queue, iv(2)), SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue enqueue 3", shrewd_basic_queue_enqueue(queue, iv(3)), SHREWD_BASIC_ERR_OK);

    size_t size = 0;
    expect_eq_int("queue size status", shrewd_basic_queue_size(queue, &size), SHREWD_BASIC_ERR_OK);
    expect_true("queue size", 3 == size);

    void *out = NULL;
    expect_eq_int("queue front status", shrewd_basic_queue_front(queue, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue front", pv(out), 1);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int(
        "queue foreach", shrewd_basic_queue_foreach(queue, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("queue order 1 2 3", (3 == c.n) && (1 == pv(c.items[0])) && (3 == pv(c.items[2])));

    expect_eq_int("queue dequeue 1", shrewd_basic_queue_dequeue(queue, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue dequeued 1", pv(out), 1);
    expect_eq_int("queue dequeue 2", shrewd_basic_queue_dequeue(queue, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue dequeued 2", pv(out), 2);
    expect_eq_int("queue dequeue 3", shrewd_basic_queue_dequeue(queue, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("queue dequeued 3", pv(out), 3);
    expect_true("queue empty", shrewd_basic_queue_empty(queue));
    expect_eq_int("queue dequeue on empty",
                  shrewd_basic_queue_dequeue(queue, &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("queue destroy", shrewd_basic_queue_destroy(queue), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "queue destroy NULL", shrewd_basic_queue_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
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
    expect_eq_int("stack size status", shrewd_basic_stack_size(stack, &size), SHREWD_BASIC_ERR_OK);
    expect_true("stack size", 3 == size);

    void *out = NULL;
    expect_eq_int("stack top status", shrewd_basic_stack_top(stack, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack top", pv(out), 3);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int(
        "stack foreach", shrewd_basic_stack_foreach(stack, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("stack bottom to top 1 2 3",
                (3 == c.n) && (1 == pv(c.items[0])) && (3 == pv(c.items[2])));

    expect_eq_int("stack pop", shrewd_basic_stack_pop(stack, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack popped 3", pv(out), 3);
    expect_eq_int("stack pop again", shrewd_basic_stack_pop(stack, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack popped 2", pv(out), 2);
    expect_eq_int("stack top now status", shrewd_basic_stack_top(stack, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("stack top now", pv(out), 1);
    expect_eq_int("stack pop last", shrewd_basic_stack_pop(stack, NULL), SHREWD_BASIC_ERR_OK);
    expect_true("stack empty", shrewd_basic_stack_empty(stack));
    expect_eq_int(
        "stack pop on empty", shrewd_basic_stack_pop(stack, &out), SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int(
        "stack top on empty", shrewd_basic_stack_top(stack, &out), SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("stack destroy", shrewd_basic_stack_destroy(stack), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "stack destroy NULL", shrewd_basic_stack_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_btree(void)
{
    shrewd_basic_btree_t *tree = NULL;
    expect_eq_int("btree rejects NULL cmp",
                  shrewd_basic_btree_create(NULL, NULL, &tree),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int(
        "btree create", shrewd_basic_btree_create(cmp_int, NULL, &tree), SHREWD_BASIC_ERR_OK);
    expect_true("btree create out", NULL != tree);

    const int values[] = {5, 3, 8, 1, 4, 7, 9};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        expect_eq_int(
            "btree insert", shrewd_basic_btree_insert(tree, iv(values[i])), SHREWD_BASIC_ERR_OK);
    }
    size_t size = 0;
    expect_eq_int("btree size status", shrewd_basic_btree_size(tree, &size), SHREWD_BASIC_ERR_OK);
    expect_true("btree size", 7 == size);
    expect_eq_int("btree duplicate insert",
                  shrewd_basic_btree_insert(tree, iv(4)),
                  SHREWD_BASIC_ERR_KEY_EXISTS);

    void *out = NULL;
    expect_eq_int(
        "btree search hit", shrewd_basic_btree_search(tree, iv(4), &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree search payload", pv(out), 4);
    expect_eq_int("btree search miss",
                  shrewd_basic_btree_search(tree, iv(6), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("btree min status", shrewd_basic_btree_min(tree, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree min", pv(out), 1);
    expect_eq_int("btree max status", shrewd_basic_btree_max(tree, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree max", pv(out), 9);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int(
        "btree foreach", shrewd_basic_btree_foreach(tree, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("btree in-order sorted",
                (7 == c.n) && collected_sorted(&c) && (1 == pv(c.items[0]))
                    && (9 == pv(c.items[6])));

    /* 3 has two children; 1 is a leaf; 8 has two children. */
    expect_eq_int("btree remove two children",
                  shrewd_basic_btree_remove(tree, iv(3), &out),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree removed payload", pv(out), 3);
    expect_eq_int(
        "btree remove leaf", shrewd_basic_btree_remove(tree, iv(1), NULL), SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree remove subtree root",
                  shrewd_basic_btree_remove(tree, iv(8), NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree remove missing",
                  shrewd_basic_btree_remove(tree, iv(42), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    expect_eq_int("btree size status after removes",
                  shrewd_basic_btree_size(tree, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("btree size after removes", 4 == size);
    expect_eq_int("btree search removed",
                  shrewd_basic_btree_search(tree, iv(3), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int(
        "btree min after removes", shrewd_basic_btree_min(tree, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree min after removes payload", pv(out), 4);
    expect_eq_int(
        "btree max after removes", shrewd_basic_btree_max(tree, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("btree max after removes payload", pv(out), 9);

    c.n = 0;
    expect_eq_int("btree foreach after removes",
                  shrewd_basic_btree_foreach(tree, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("btree remaining 4 5 7 9",
                (4 == c.n) && (4 == pv(c.items[0])) && (5 == pv(c.items[1]))
                    && (7 == pv(c.items[2])) && (9 == pv(c.items[3])));

    expect_eq_int("btree destroy", shrewd_basic_btree_destroy(tree), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "btree destroy NULL", shrewd_basic_btree_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
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
    expect_eq_int(
        "rbtree create", shrewd_basic_rbtree_create(cmp_int, NULL, &tree), SHREWD_BASIC_ERR_OK);
    expect_true("rbtree create out", NULL != tree);

    /* (i * 37) % 64 permutes 0..63: a fixed "shuffled" insertion order. */
    for (int i = 0; 64 > i; ++i) {
        expect_eq_int("rbtree insert",
                      shrewd_basic_rbtree_insert(tree, iv((i * 37) % 64)),
                      SHREWD_BASIC_ERR_OK);
    }
    size_t size = 0;
    expect_eq_int("rbtree size status", shrewd_basic_rbtree_size(tree, &size), SHREWD_BASIC_ERR_OK);
    expect_true("rbtree size", 64 == size);
    expect_eq_int("rbtree duplicate insert",
                  shrewd_basic_rbtree_insert(tree, iv(5)),
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
    expect_eq_int("rbtree search miss",
                  shrewd_basic_rbtree_search(tree, iv(100), &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("rbtree min status", shrewd_basic_rbtree_min(tree, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("rbtree min", pv(out), 0);
    expect_eq_int("rbtree max status", shrewd_basic_rbtree_max(tree, &out), SHREWD_BASIC_ERR_OK);
    expect_eq_int("rbtree max", pv(out), 63);

    struct collect_ctx c = {{0}, 0};
    expect_eq_int(
        "rbtree foreach", shrewd_basic_rbtree_foreach(tree, collect, &c), SHREWD_BASIC_ERR_OK);
    expect_true("rbtree in-order sorted 0..63",
                (64 == c.n) && collected_sorted(&c) && (0 == pv(c.items[0]))
                    && (63 == pv(c.items[63])));

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
    expect_eq_int("rbtree size status after removes",
                  shrewd_basic_rbtree_size(tree, &size),
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
                  shrewd_basic_rbtree_foreach(tree, collect, &c),
                  SHREWD_BASIC_ERR_OK);
    expect_true("rbtree still sorted", (c.n == (size_t)(64 - removed)) && collected_sorted(&c));
    expect_true("rbtree invariants after removes", rb_invariants_ok(tree));

    expect_eq_int("rbtree destroy", shrewd_basic_rbtree_destroy(tree), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "rbtree destroy NULL", shrewd_basic_rbtree_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
}

/* Key collector for trie/radix foreach. */
struct key_ctx {
    char keys[32][32];
    size_t n;
    int stop_after;
};

static int collect_key(const char *key, void *value, void *ctx)
{
    (void)value;
    struct key_ctx *c = ctx;
    if (c->n < sizeof(c->keys) / sizeof(c->keys[0])) {
        snprintf(c->keys[c->n], sizeof(c->keys[0]), "%s", key);
        ++c->n;
    }
    if ((0 != c->stop_after) && (c->n >= (size_t)c->stop_after)) {
        return SHREWD_BASIC_ERR_KEY_EXISTS;
    }
    return SHREWD_BASIC_ERR_OK;
}

static int keys_match(const struct key_ctx *c, const char *const *want, size_t n)
{
    if (c->n != n) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        if (0 != strcmp(c->keys[i], want[i])) {
            return 0;
        }
    }
    return 1;
}

static void test_trie(void)
{
    shrewd_basic_trie_t *trie = NULL;
    expect_eq_int("trie create", shrewd_basic_trie_create(&trie), SHREWD_BASIC_ERR_OK);
    expect_true("trie create out", NULL != trie);
    expect_true("trie empty", shrewd_basic_trie_empty(trie));
    expect_eq_int(
        "trie create NULL out", shrewd_basic_trie_create(NULL), SHREWD_BASIC_ERR_ARG_INVALID);

    expect_eq_int(
        "trie insert app", shrewd_basic_trie_insert(trie, "app", iv(1)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "trie insert apple", shrewd_basic_trie_insert(trie, "apple", iv(2)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "trie insert ape", shrewd_basic_trie_insert(trie, "ape", iv(3)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "trie insert bat", shrewd_basic_trie_insert(trie, "bat", iv(4)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "trie insert empty key", shrewd_basic_trie_insert(trie, "", iv(5)), SHREWD_BASIC_ERR_OK);
    expect_eq_int("trie insert duplicate",
                  shrewd_basic_trie_insert(trie, "app", iv(9)),
                  SHREWD_BASIC_ERR_KEY_EXISTS);

    size_t size = 0;
    expect_eq_int("trie size", shrewd_basic_trie_size(trie, &size), SHREWD_BASIC_ERR_OK);
    expect_true("trie size is 5", 5 == size);

    void *out = NULL;
    expect_eq_int(
        "trie search app", shrewd_basic_trie_search(trie, "app", &out), SHREWD_BASIC_ERR_OK);
    expect_true("trie search app value", 1 == pv(out));
    expect_eq_int(
        "trie search empty key", shrewd_basic_trie_search(trie, "", &out), SHREWD_BASIC_ERR_OK);
    expect_true("trie search empty value", 5 == pv(out));
    expect_eq_int("trie search prefix miss",
                  shrewd_basic_trie_search(trie, "ap", &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("trie search miss",
                  shrewd_basic_trie_search(trie, "zap", &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    /* Lexicographic order: "", "ape", "app", "apple", "bat". */
    static const char *const sorted[] = {"", "ape", "app", "apple", "bat"};
    struct key_ctx kc = {0};
    expect_eq_int(
        "trie foreach", shrewd_basic_trie_foreach(trie, collect_key, &kc), SHREWD_BASIC_ERR_OK);
    expect_true("trie foreach order", keys_match(&kc, sorted, 5));

    static const char *const under_app[] = {"app", "apple"};
    struct key_ctx kp = {0};
    expect_eq_int("trie foreach_prefix app",
                  shrewd_basic_trie_foreach_prefix(trie, "app", collect_key, &kp),
                  SHREWD_BASIC_ERR_OK);
    expect_true("trie prefix app keys", keys_match(&kp, under_app, 2));

    struct key_ctx kz = {0};
    expect_eq_int("trie foreach_prefix miss",
                  shrewd_basic_trie_foreach_prefix(trie, "zz", collect_key, &kz),
                  SHREWD_BASIC_ERR_OK);
    expect_true("trie prefix miss visits nothing", 0 == kz.n);

    struct key_ctx ka = {.stop_after = 2};
    expect_eq_int("trie foreach abort",
                  shrewd_basic_trie_foreach(trie, collect_key, &ka),
                  SHREWD_BASIC_ERR_KEY_EXISTS);
    expect_true("trie foreach abort count", 2 == ka.n);

    out = NULL;
    expect_eq_int(
        "trie remove app", shrewd_basic_trie_remove(trie, "app", &out), SHREWD_BASIC_ERR_OK);
    expect_true("trie remove value", 1 == pv(out));
    expect_eq_int("trie remove again",
                  shrewd_basic_trie_remove(trie, "app", &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("trie search after remove",
                  shrewd_basic_trie_search(trie, "apple", &out),
                  SHREWD_BASIC_ERR_OK);
    expect_true("trie apple survives", 2 == pv(out));
    expect_eq_int(
        "trie remove apple", shrewd_basic_trie_remove(trie, "apple", NULL), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "trie size after removes", shrewd_basic_trie_size(trie, &size), SHREWD_BASIC_ERR_OK);
    expect_true("trie size is 3", 3 == size);

    expect_eq_int("trie insert NULL key",
                  shrewd_basic_trie_insert(trie, NULL, iv(0)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("trie search NULL out",
                  shrewd_basic_trie_search(trie, "app", NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    expect_eq_int("trie destroy", shrewd_basic_trie_destroy(trie), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "trie destroy NULL", shrewd_basic_trie_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_radix(void)
{
    shrewd_basic_radix_t *tree = NULL;
    expect_eq_int("radix create", shrewd_basic_radix_create(&tree), SHREWD_BASIC_ERR_OK);
    expect_true("radix create out", NULL != tree);
    expect_true("radix empty", shrewd_basic_radix_empty(tree));

    /* Prefix-heavy words exercise edge splits on nearly every insert. */
    expect_eq_int("radix insert romane",
                  shrewd_basic_radix_insert(tree, "romane", iv(1)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix insert romanus",
                  shrewd_basic_radix_insert(tree, "romanus", iv(2)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix insert romulus",
                  shrewd_basic_radix_insert(tree, "romulus", iv(3)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix insert rubens",
                  shrewd_basic_radix_insert(tree, "rubens", iv(4)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "radix insert ruber", shrewd_basic_radix_insert(tree, "ruber", iv(5)), SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix insert rubicon",
                  shrewd_basic_radix_insert(tree, "rubicon", iv(6)),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "radix insert rom", shrewd_basic_radix_insert(tree, "rom", iv(7)), SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix insert duplicate",
                  shrewd_basic_radix_insert(tree, "romane", iv(9)),
                  SHREWD_BASIC_ERR_KEY_EXISTS);

    size_t size = 0;
    expect_eq_int("radix size", shrewd_basic_radix_size(tree, &size), SHREWD_BASIC_ERR_OK);
    expect_true("radix size is 7", 7 == size);

    void *out = NULL;
    expect_eq_int("radix search romane",
                  shrewd_basic_radix_search(tree, "romane", &out),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix search romane value", 1 == pv(out));
    expect_eq_int(
        "radix search rom", shrewd_basic_radix_search(tree, "rom", &out), SHREWD_BASIC_ERR_OK);
    expect_true("radix search rom value", 7 == pv(out));
    expect_eq_int("radix search node without value",
                  shrewd_basic_radix_search(tree, "roman", &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("radix search mid-edge miss",
                  shrewd_basic_radix_search(tree, "roma", &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("radix search miss",
                  shrewd_basic_radix_search(tree, "zip", &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    /* Byte order is lexicographic order. */
    static const char *const sorted[]
        = {"rom", "romane", "romanus", "romulus", "rubens", "ruber", "rubicon"};
    struct key_ctx kc = {0};
    expect_eq_int(
        "radix foreach", shrewd_basic_radix_foreach(tree, collect_key, &kc), SHREWD_BASIC_ERR_OK);
    expect_true("radix foreach order", keys_match(&kc, sorted, 7));

    static const char *const under_rom[] = {"rom", "romane", "romanus", "romulus"};
    struct key_ctx kp = {0};
    expect_eq_int("radix foreach_prefix rom",
                  shrewd_basic_radix_foreach_prefix(tree, "rom", collect_key, &kp),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix prefix rom keys", keys_match(&kp, under_rom, 4));

    /* "roma" ends inside an edge label: the whole subtree matches. */
    static const char *const under_roma[] = {"romane", "romanus"};
    struct key_ctx km = {0};
    expect_eq_int("radix foreach_prefix roma",
                  shrewd_basic_radix_foreach_prefix(tree, "roma", collect_key, &km),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix prefix roma keys", keys_match(&km, under_roma, 2));

    /* "romae" diverges inside an edge label: nothing matches. */
    struct key_ctx kd = {0};
    expect_eq_int("radix foreach_prefix diverge",
                  shrewd_basic_radix_foreach_prefix(tree, "romae", collect_key, &kd),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix prefix diverge visits nothing", 0 == kd.n);

    static const char *const under_rub[] = {"rubens", "ruber", "rubicon"};
    struct key_ctx kr = {0};
    expect_eq_int("radix foreach_prefix rub",
                  shrewd_basic_radix_foreach_prefix(tree, "rub", collect_key, &kr),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix prefix rub keys", keys_match(&kr, under_rub, 3));

    /* Leaf removal re-compresses the path; siblings must survive. */
    out = NULL;
    expect_eq_int("radix remove romane",
                  shrewd_basic_radix_remove(tree, "romane", &out),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix remove value", 1 == pv(out));
    expect_eq_int("radix search removed romane",
                  shrewd_basic_radix_search(tree, "romane", &out),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("radix search romanus survives",
                  shrewd_basic_radix_search(tree, "romanus", &out),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix romanus value", 2 == pv(out));

    /* Value on a branching node: removing it keeps the children. */
    expect_eq_int(
        "radix remove rom", shrewd_basic_radix_remove(tree, "rom", NULL), SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix search romulus after rom removed",
                  shrewd_basic_radix_search(tree, "romulus", &out),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix romulus value", 3 == pv(out));
    expect_eq_int("radix remove rom again",
                  shrewd_basic_radix_remove(tree, "rom", NULL),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int(
        "radix size after removes", shrewd_basic_radix_size(tree, &size), SHREWD_BASIC_ERR_OK);
    expect_true("radix size is 5", 5 == size);

    /* Drain the tree; every removal must succeed and end at empty. */
    expect_eq_int("radix remove romanus",
                  shrewd_basic_radix_remove(tree, "romanus", NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix remove romulus",
                  shrewd_basic_radix_remove(tree, "romulus", NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix remove rubens",
                  shrewd_basic_radix_remove(tree, "rubens", NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "radix remove ruber", shrewd_basic_radix_remove(tree, "ruber", NULL), SHREWD_BASIC_ERR_OK);
    expect_eq_int("radix remove rubicon",
                  shrewd_basic_radix_remove(tree, "rubicon", NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix empty after drain", shrewd_basic_radix_empty(tree));
    struct key_ctx ke = {0};
    expect_eq_int("radix foreach when empty",
                  shrewd_basic_radix_foreach(tree, collect_key, &ke),
                  SHREWD_BASIC_ERR_OK);
    expect_true("radix foreach empty visits nothing", 0 == ke.n);

    /* A key that is a prefix of another key. */
    expect_eq_int(
        "radix insert a", shrewd_basic_radix_insert(tree, "a", iv(8)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "radix insert ab", shrewd_basic_radix_insert(tree, "ab", iv(9)), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "radix remove ab", shrewd_basic_radix_remove(tree, "ab", NULL), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "radix search a survives", shrewd_basic_radix_search(tree, "a", &out), SHREWD_BASIC_ERR_OK);
    expect_true("radix a value", 8 == pv(out));

    expect_eq_int("radix insert NULL key",
                  shrewd_basic_radix_insert(tree, NULL, iv(0)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("radix search NULL out",
                  shrewd_basic_radix_search(tree, "a", NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    expect_eq_int("radix destroy", shrewd_basic_radix_destroy(tree), SHREWD_BASIC_ERR_OK);
    expect_eq_int(
        "radix destroy NULL", shrewd_basic_radix_destroy(NULL), SHREWD_BASIC_ERR_ARG_INVALID);
}

int main(void)
{
    test_slist();
    test_dlist();
    test_queue();
    test_stack();
    test_btree();
    test_rbtree();
    test_trie();
    test_radix();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

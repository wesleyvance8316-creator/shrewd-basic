/**
 * @file test_react.c
 * @brief Tests for the reactive primitives (ref/effect/computed/watch):
 *        tracking, fan-out, branch re-tracking, derived chains,
 *        change-only watch, dispose and re-entrancy. One summary line.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/reflect/react.h"

static int g_failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("test_react: FAILED %s\n", name);
        ++g_failures;
    }
}

/* --- fixtures ----------------------------------------------------------- */

typedef struct {
    shrewd_basic_reflect_ref_t *a;
    shrewd_basic_reflect_ref_t *b;
    int32_t *out;
    int32_t *runs;
} sum_ctx_t;

static void sum_two(void *ctx) {
    sum_ctx_t *s = (sum_ctx_t *)ctx;
    ++(*s->runs);
    *s->out = *(const int32_t *)shrewd_basic_reflect_ref_get(s->a)
              + *(const int32_t *)shrewd_basic_reflect_ref_get(s->b);
}

/* Reads b only when a is non-zero: the dependency set follows the
   branch (re-tracked on every run). */
static int32_t g_branch_runs = 0;
static int32_t g_branch_out = 0;

static void branch_fx(void *ctx) {
    shrewd_basic_reflect_ref_t **pair = (shrewd_basic_reflect_ref_t **)ctx;
    ++g_branch_runs;
    g_branch_out = *(const int32_t *)shrewd_basic_reflect_ref_get(pair[0]);
    if (0 != g_branch_out) {
        g_branch_out += *(const int32_t *)shrewd_basic_reflect_ref_get(pair[1]);
    }
}

/* Derived values. */
static int sum_fn(void *ctx, void *out, size_t size) {
    sum_ctx_t *s = (sum_ctx_t *)ctx;
    int32_t v = *(const int32_t *)shrewd_basic_reflect_ref_get(s->a)
                + *(const int32_t *)shrewd_basic_reflect_ref_get(s->b);
    (void)size;
    memcpy(out, &v, sizeof(v));
    return SHREWD_BASIC_ERR_OK;
}

static int double_fn(void *ctx, void *out, size_t size) {
    shrewd_basic_reflect_ref_t *src = *(shrewd_basic_reflect_ref_t **)ctx;
    int32_t v = 2 * (*(const int32_t *)shrewd_basic_reflect_ref_get(src));
    (void)size;
    memcpy(out, &v, sizeof(v));
    return SHREWD_BASIC_ERR_OK;
}

static int32_t g_seen = 0;
static int32_t g_seen_runs = 0;

static void see_value(void *ctx) {
    shrewd_basic_reflect_ref_t *src = (shrewd_basic_reflect_ref_t *)ctx;
    ++g_seen_runs;
    g_seen = *(const int32_t *)shrewd_basic_reflect_ref_get(src);
}

/* Watch. */
static int32_t g_watch_calls = 0;
static int32_t g_watch_old = 0;
static int32_t g_watch_new = 0;

static void watch_cb(void *ctx, const void *old_value, const void *new_value, size_t size) {
    (void)ctx;
    (void)size;
    ++g_watch_calls;
    memcpy(&g_watch_old, old_value, sizeof(g_watch_old));
    memcpy(&g_watch_new, new_value, sizeof(g_watch_new));
}

/* Re-entrancy: an effect that sets the ref it reads. */
static int32_t g_loop_runs = 0;

static void loop_fx(void *ctx) {
    shrewd_basic_reflect_ref_t *x = (shrewd_basic_reflect_ref_t *)ctx;
    int32_t v = *(const int32_t *)shrewd_basic_reflect_ref_get(x);
    ++g_loop_runs;
    if (7 != v) {
        int32_t next = 7;
        (void)shrewd_basic_reflect_ref_set(x, &next, sizeof(next));
    }
}

int main(void) {
    shrewd_basic_reflect_ref_t a;
    shrewd_basic_reflect_ref_t b;
    shrewd_basic_reflect_effect_t fx;
    shrewd_basic_reflect_effect_t fx2;
    sum_ctx_t s;
    int32_t out = 0;
    int32_t out2 = 0;
    int32_t runs = 0;
    int32_t runs2 = 0;
    int32_t v = 0;

    {
        int32_t a_storage = 1;
        int32_t b_storage = 2;
        expect("ref init a",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_ref_init(&a, &a_storage, sizeof(a_storage)));
        expect("ref init b",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_reflect_ref_init(&b, &b_storage, sizeof(b_storage)));
        s.a = &a;
        s.b = &b;
        s.out = &out;
        s.runs = &runs;

        /* Basic tracking: set re-runs the effect. */
        expect("effect init",
               SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_effect_init(&fx, sum_two, &s));
        expect("effect first run", (1 == runs) && (3 == out));
        v = 10;
        expect("set a", SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&a, &v, sizeof(v)));
        expect("effect reran", (2 == runs) && (12 == out));
        expect("set size mismatch",
               SHREWD_BASIC_ERR_REFLECT_MISMATCH
                   == shrewd_basic_reflect_ref_set(&a, &v, sizeof(v) - 1));

        /* Fan-out + dispose (each effect gets its own context). */
        {
            sum_ctx_t s2;
            s2.a = &a;
            s2.b = &b;
            s2.out = &out2;
            s2.runs = &runs2;
            expect("effect2 init",
                   SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_effect_init(&fx2, sum_two, &s2));
            v = 20;
            expect("set fan",
                   SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&a, &v, sizeof(v)));
            expect("both consumers", (3 == runs) && (22 == out) && (2 == runs2) && (22 == out2));
            shrewd_basic_reflect_effect_dispose(&fx2);
            v = 30;
            expect("set after dispose",
                   SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&a, &v, sizeof(v)));
            expect("disposed stayed quiet", (2 == runs2) && (4 == runs) && (32 == out));
        }

        /* Branch re-tracking. */
        {
            shrewd_basic_reflect_ref_t *pair[2];
            shrewd_basic_reflect_effect_t bfx;
            int32_t before = 0;
            pair[0] = &a;
            pair[1] = &b;
            g_branch_runs = 0;
            expect("branch init",
                   SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_effect_init(&bfx, branch_fx, pair));
            expect("branch first", 1 == g_branch_runs);
            v = 0;
            (void)shrewd_basic_reflect_ref_set(&a, &v, sizeof(v)); /* branch: a only */
            expect("branch a only", 2 == g_branch_runs);
            before = g_branch_runs;
            v = 5;
            (void)shrewd_basic_reflect_ref_set(&b, &v, sizeof(v)); /* b untracked here */
            expect("branch untracked b", before == g_branch_runs);
            v = 1;
            (void)shrewd_basic_reflect_ref_set(&a, &v, sizeof(v)); /* now b is read */
            before = g_branch_runs;
            v = 9;
            (void)shrewd_basic_reflect_ref_set(&b, &v, sizeof(v));
            expect("branch tracked b", g_branch_runs > before);
            shrewd_basic_reflect_effect_dispose(&bfx);
        }

        /* Computed chain: c2 = f(c1), c1 = a + b; a consumer on c2. */
        {
            shrewd_basic_reflect_computed_t c1;
            shrewd_basic_reflect_computed_t c2;
            shrewd_basic_reflect_ref_t *c1_ptr = &c1.ref;
            int32_t c1_storage = 0;
            int32_t c2_storage = 0;
            shrewd_basic_reflect_effect_t consumer;
            expect("computed c1",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_reflect_computed_init(
                           &c1, sum_fn, &s, &c1_storage, sizeof(c1_storage)));
            expect("computed c2",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_reflect_computed_init(
                           &c2, double_fn, &c1_ptr, &c2_storage, sizeof(c2_storage)));
            g_seen_runs = 0;
            g_seen = 0;
            expect("consumer init",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_reflect_effect_init(&consumer, see_value, &c2.ref));
            expect("chain first", (1 == g_seen_runs) && (g_seen == 2 * (1 + 9))); /* a=1, b=9 */
            v = 40;
            (void)shrewd_basic_reflect_ref_set(&a, &v, sizeof(v)); /* a: 30 -> 40 */
            expect("chain propagated", (2 == g_seen_runs) && (g_seen == 2 * (40 + 9)));
            shrewd_basic_reflect_effect_dispose(&consumer);
            shrewd_basic_reflect_computed_dispose(&c1);
            shrewd_basic_reflect_computed_dispose(&c2);
        }

        /* Watch: fires on change only, carrying old and new. */
        {
            shrewd_basic_reflect_watch_t w;
            int32_t memo = 0;
            g_watch_calls = 0;
            expect("watch init",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_reflect_watch_init(
                           &w, &a, watch_cb, NULL, &memo, sizeof(memo)));
            expect("watch silent on init", 0 == g_watch_calls);
            v = 77;
            (void)shrewd_basic_reflect_ref_set(&a, &v, sizeof(v));
            expect("watch fired",
                   (1 == g_watch_calls) && (40 == g_watch_old) && (77 == g_watch_new));
            shrewd_basic_reflect_watch_dispose(&w);
        }

        /* Re-entrancy: set inside an effect converges. */
        {
            shrewd_basic_reflect_ref_t x;
            shrewd_basic_reflect_effect_t lfx;
            int32_t x_storage = 0;
            g_loop_runs = 0;
            expect("loop ref init",
                   SHREWD_BASIC_ERR_OK
                       == shrewd_basic_reflect_ref_init(&x, &x_storage, sizeof(x_storage)));
            expect("loop effect",
                   SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_effect_init(&lfx, loop_fx, &x));
            expect("loop converged", (2 == g_loop_runs) && (7 == x_storage));
            shrewd_basic_reflect_effect_dispose(&lfx);
        }

        shrewd_basic_reflect_effect_dispose(&fx);
    }

    if (0 != g_failures) {
        printf("test_react: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_react: ok\n");
    return 0;
}

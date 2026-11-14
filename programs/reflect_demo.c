/**
 * @file reflect_demo.c
 * @brief Walkthrough of every shrewd-basic/reflect interface.
 *
 * Descriptors are authored with macros at compile time (const data);
 * generic code then enumerates fields, reads/writes values, walks paths,
 * prints objects, validates them and deep-copies them. dump output goes
 * through the "reflect" log module - colored on a terminal - while the
 * section banners use plain printf.
 *
 * Covered: TYPE_DEFINE/DECLARE/DEFINE_OPS/OPAQUE_DEFINE, FIELD/FIELD_
 * STRUCT/FIELD_ARRAY/FIELD_PTR/FIELD_PTR_ARRAY (+ _OPS variants),
 * kind_name, type_find_field, field_get/set, dump, type_resolve,
 * path_get/set, value_get/set, check, copy, destroy.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

#include "reflect_demo_ext.h"

static void banner(const char *title)
{
    printf("\n=== %s ===\n", title);
    fflush(stdout);
}

static void print_status(const char *what, int status)
{
    printf("  %-46s -> %s (%d)\n", what, shrewd_basic_error_name(status), status);
    fflush(stdout);
}

/* --- reflected types -------------------------------------------------- */

typedef struct {
    int32_t x;
    int32_t y;
} point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_type, point_t,
    SHREWD_BASIC_REFLECT_FIELD(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD(point_t, y));

/* Self-referential type (linked list). */
typedef struct node {
    int32_t data;
    struct node *next;
} node_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(node_type, node_t,
    SHREWD_BASIC_REFLECT_FIELD(node_t, data),
    SHREWD_BASIC_REFLECT_FIELD_PTR(node_t, next, node_type));

/* --- ops fixtures ----------------------------------------------------- */

/* An opaque handle: the application knows the layout (an int32), reflect
 * only sees "opaque + ops". Invariant: value >= 0; set clamps to 100. */
static int handle_get(const void *pointee, void *out_value, size_t value_size)
{
    memcpy(out_value, pointee, value_size);
    return SHREWD_BASIC_ERR_OK;
}

static int handle_set(void *pointee, const void *value, size_t value_size)
{
    int32_t rounded = 0;
    memcpy(&rounded, value, sizeof(rounded));
    if (100 < rounded) {
        rounded = 100;
    }
    memcpy(pointee, &rounded, sizeof(rounded));
    (void)value_size;
    return SHREWD_BASIC_ERR_OK;
}

static int handle_check(const void *pointee)
{
    int32_t value = 0;
    memcpy(&value, pointee, sizeof(value));
    return (0 <= value) ? SHREWD_BASIC_ERR_OK : SHREWD_BASIC_ERR_ARG_INVALID;
}

static int handle_copy(void **out, const void *pointee)
{
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(int32_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(block, pointee, sizeof(int32_t));
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

static void handle_destroy(void *pointee)
{
    (void)shrewd_basic_platform_free(&pointee);
}

static const shrewd_basic_reflect_ops_t g_handle_ops = {
    .value_size = sizeof(int32_t),
    .get = handle_get,
    .set = handle_set,
    .check = handle_check,
    .copy = handle_copy,
    .destroy = handle_destroy };

SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(handle_type, "handle", g_handle_ops);

/* Field-level override: only get differs (x10), the rest reuses the
 * handle behavior - a field ops table replaces the type's per field. */
static int scale_get(const void *pointee, void *out_value, size_t value_size)
{
    int32_t value = 0;
    memcpy(&value, pointee, sizeof(value));
    value *= 10;
    memcpy(out_value, &value, sizeof(value));
    (void)value_size;
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_ops_t g_scale_ops = {
    .value_size = sizeof(int32_t),
    .get = scale_get,
    .set = handle_set,
    .check = handle_check,
    .copy = handle_copy,
    .destroy = handle_destroy };

/* A rich struct exercising every field flavor. */
typedef struct {
    const char *name;
    char grade;
    int8_t small;
    int32_t age;
    uint64_t big;
    float ratio;
    double precise;
    char tag[4];
    int32_t scores[3];
    point_t home;
    point_t pair[2];
    point_t *ptr;
    const char **p_str;
    void *ctx;
    int32_t *handle;
    int32_t *scaled;
} person_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(person_type, person_t,
    SHREWD_BASIC_REFLECT_FIELD(person_t, name),
    SHREWD_BASIC_REFLECT_FIELD(person_t, grade),
    SHREWD_BASIC_REFLECT_FIELD(person_t, small),
    SHREWD_BASIC_REFLECT_FIELD(person_t, age),
    SHREWD_BASIC_REFLECT_FIELD(person_t, big),
    SHREWD_BASIC_REFLECT_FIELD(person_t, ratio),
    SHREWD_BASIC_REFLECT_FIELD(person_t, precise),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, tag, shrewd_basic_reflect_char),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, scores, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(person_t, home, point_type),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, pair, point_type),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, ptr, point_type),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, p_str, shrewd_basic_reflect_string),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, ctx, shrewd_basic_reflect_void),
    SHREWD_BASIC_REFLECT_FIELD_PTR(person_t, handle, handle_type),
    SHREWD_BASIC_REFLECT_FIELD_PTR_OPS(person_t, scaled, handle_type, g_scale_ops));

static void describe_field(const char *name)
{
    const shrewd_basic_reflect_field_t *field = NULL;
    int status = shrewd_basic_reflect_type_find_field(&person_type, name, &field);
    if (SHREWD_BASIC_ERR_OK != status) {
        printf("  %-10s -> %s\n", name, shrewd_basic_error_name(status));
        return;
    }
    printf("  %-10s kind=%-6s offset=%-3zu size=%-3zu count=%zu%s%s\n",
           name, shrewd_basic_reflect_kind_name(field->type->kind),
           field->offset, field->size, field->count,
           (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) ? " ARRAY" : "",
           (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) ? " PTR" : "");
}

int main(void)
{
    /* Line-buffer stdout so printf output interleaves cleanly with the
       log module's stderr output. */
    (void)setvbuf(stdout, NULL, _IOLBF, 0);

    /* The reflect module logs every operation at TRACE through the shared
       "reflect" log module (see docs/reflect.md). The module joins the
       registry on first use, so trigger one lookup miss first (its TRACE
       line is the only unmasked one), then mask to INFO: dump lines are
       INFO and still show. */
    const shrewd_basic_reflect_field_t *probe = NULL;
    (void)shrewd_basic_reflect_type_find_field(&person_type, "warmup", &probe);
    shrewd_basic_log_module_t *log_module = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("reflect", &log_module)) {
        (void)shrewd_basic_log_module_set_level(log_module, SHREWD_BASIC_LOG_LEVEL_INFO);
    }

    person_t person;
    memset(&person, 0, sizeof(person));
    person.name = "Alice";
    person.grade = 'A';
    person.small = -7;
    person.age = 30;
    person.big = UINT64_C(0x1122334455667788);
    person.ratio = 1.5f;
    person.precise = 2.25;
    memcpy(person.tag, "tag", 4);
    person.scores[0] = 1;
    person.scores[1] = 2;
    person.scores[2] = 3;
    person.home.x = 4;
    person.home.y = 5;
    person.pair[0].x = 6;
    person.pair[0].y = 7;
    person.pair[1].x = 8;
    person.pair[1].y = 9;

    banner("1. type description: find_field + kind_name");
    printf("  person_t has %zu fields, size %zu bytes, kind %s\n",
           person_type.field_count, person_type.size,
           shrewd_basic_reflect_kind_name(person_type.kind));
    describe_field("age");
    describe_field("name");
    describe_field("tag");
    describe_field("scores");
    describe_field("home");
    describe_field("ptr");
    describe_field("nope");

    banner("2. field_get/field_set: raw-byte access");
    const shrewd_basic_reflect_field_t *age_field = NULL;
    (void)shrewd_basic_reflect_type_find_field(&person_type, "age", &age_field);
    int32_t age = 0;
    (void)shrewd_basic_reflect_field_get(&person, age_field, &age, sizeof(age));
    printf("  get 'age' -> %d\n", age);
    int32_t updated = 31;
    print_status("set 'age' to 31",
                 shrewd_basic_reflect_field_set(&person, age_field, &updated, sizeof(updated)));
    (void)shrewd_basic_reflect_field_get(&person, age_field, &age, sizeof(age));
    printf("  get 'age' -> %d\n", age);
    print_status("set 'age' with wrong size (2)",
                 shrewd_basic_reflect_field_set(&person, age_field, &updated, (size_t)2));

    banner("3. dump: every field kind, nested and arrays");
    (void)shrewd_basic_reflect_dump(&person_type, &person, "person");

    banner("4. pointers: dump follows them");
    point_t point = {1, 2};
    const char *text = "hi";
    int32_t marker = 0;
    person.ptr = &point;
    person.p_str = &text;
    person.ctx = &marker; /* opaque: dump shows the address */
    (void)shrewd_basic_reflect_dump(&person_type, &person, "person");

    node_t tail;
    memset(&tail, 0, sizeof(tail));
    tail.data = 2;
    node_t head;
    memset(&head, 0, sizeof(head));
    head.data = 1;
    head.next = &tail;
    printf("\n  self-referential list (node_t *next):\n");
    (void)shrewd_basic_reflect_dump(&node_type, &head, "node");

    node_t loop;
    memset(&loop, 0, sizeof(loop));
    loop.next = &loop;
    printf("\n  self-cycle terminates at the depth cap:\n");
    (void)shrewd_basic_reflect_dump(&node_type, &loop, "node");
    person.ptr = NULL;
    (void)shrewd_basic_reflect_dump(&person_type, &person, "person (ptr = NULL)");

    banner("5. paths: resolve / path_get / path_set");
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t offset = 0;
    size_t size = 0;
    print_status("resolve \"home.x\"",
                 shrewd_basic_reflect_type_resolve(&person_type, "home.x",
                                                   &field, &offset, &size));
    printf("  -> field \"%s\", offset %zu, value size %zu\n",
           field->name, offset, size);
    int32_t value = 77;
    print_status("path_set \"home.x\" = 77",
                 shrewd_basic_reflect_path_set(&person, &person_type, "home.x",
                                               &value, sizeof(value)));
    value = 0;
    print_status("path_get \"home.x\"",
                 shrewd_basic_reflect_path_get(&person, &person_type, "home.x",
                                               &value, sizeof(value)));
    printf("  -> home.x = %d\n", value);
    value = 88;
    print_status("path_set \"pair[1].y\" = 88",
                 shrewd_basic_reflect_path_set(&person, &person_type, "pair[1].y",
                                               &value, sizeof(value)));
    printf("  -> pair[1].y = %d\n", person.pair[1].y);
    person.ptr = &point;
    value = 0;
    print_status("path_get \"ptr.y\" (through a pointer)",
                 shrewd_basic_reflect_path_get(&person, &person_type, "ptr.y",
                                               &value, sizeof(value)));
    printf("  -> ptr->y = %d\n", value);
    person.ptr = NULL;
    print_status("path_get \"ptr.y\" with NULL pointer",
                 shrewd_basic_reflect_path_get(&person, &person_type, "ptr.y",
                                               &value, sizeof(value)));
    print_status("resolve \"ptr.y\" (crosses a pointer)",
                 shrewd_basic_reflect_type_resolve(&person_type, "ptr.y",
                                                   &field, &offset, &size));
    print_status("path_get \"home..x\" (bad syntax)",
                 shrewd_basic_reflect_path_get(&person, &person_type, "home..x",
                                               &value, sizeof(value)));

    banner("6. ops: value_get/set, check, copy/destroy");
    int32_t handle = 42;
    int32_t scaled = 21;
    person.handle = &handle;
    person.scaled = &scaled;
    person.ptr = &point;
    const shrewd_basic_reflect_field_t *handle_field = NULL;
    const shrewd_basic_reflect_field_t *scaled_field = NULL;
    (void)shrewd_basic_reflect_type_find_field(&person_type, "handle", &handle_field);
    (void)shrewd_basic_reflect_type_find_field(&person_type, "scaled", &scaled_field);
    value = 0;
    print_status("value_get 'handle' (type ops)",
                 shrewd_basic_reflect_value_get(&person, handle_field,
                                                &value, sizeof(value)));
    printf("  -> handle = %d\n", value);
    value = 777;
    print_status("value_set 'handle' = 777 (clamped)",
                 shrewd_basic_reflect_value_set(&person, handle_field,
                                                &value, sizeof(value)));
    printf("  -> handle = %d (set clamps to 100)\n", handle);
    value = 0;
    print_status("value_get 'scaled' (field ops, x10)",
                 shrewd_basic_reflect_value_get(&person, scaled_field,
                                                &value, sizeof(value)));
    printf("  -> scaled = %d (stored %d)\n", value, scaled);

    print_status("check (invariants hold)",
                 shrewd_basic_reflect_check(&person, &person_type));
    handle = -5;
    print_status("check (handle invariant violated)",
                 shrewd_basic_reflect_check(&person, &person_type));
    handle = 100;
    person.ctx = NULL; /* an opaque ctx without ops would block the copy */

    void *raw = NULL;
    print_status("copy (deep clone)", shrewd_basic_reflect_copy(&raw, &person, &person_type));
    person_t *clone = raw;
    printf("  clone: home.x=%d handle=%d scaled=%d\n",
           clone->home.x, *clone->handle, *clone->scaled);
    printf("  clone->ptr is a new object: %s\n",
           (clone->ptr != person.ptr) ? "yes" : "no");
    printf("  clone->p_str is aliased (strings borrowed): %s\n",
           (clone->p_str == person.p_str) ? "yes" : "no");
    point.x = 999;
    printf("  after mutating the original: clone->ptr->x = %d (independent)\n",
           clone->ptr->x);
    point.x = 1;
    print_status("check the clone", shrewd_basic_reflect_check(clone, &person_type));
    print_status("destroy the clone", shrewd_basic_reflect_destroy(clone, &person_type));

    person.ctx = &marker;
    raw = NULL;
    print_status("copy with an opaque ctx (no ops)",
                 shrewd_basic_reflect_copy(&raw, &person, &person_type));
    person.ctx = NULL;

    banner("7. cross-TU sharing: TYPE_DECLARE in another .c");
    printf("  reflect_demo_ext.c imports point_type and never sees point_t:\n");
    point.x = 3;
    point.y = 4;
    printf("  shrewd_demo_reflect_ext_sum(&point) -> %d\n",
           shrewd_demo_reflect_ext_sum(&point));
    shrewd_demo_reflect_ext_dump(&point);

    printf("\nDemo done. See docs/reflect.md for the full reference.\n");
    return 0;
}

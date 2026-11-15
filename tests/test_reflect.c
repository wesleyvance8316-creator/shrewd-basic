#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"

#include "test_reflect_ext.h"

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

/* Reflected test types. */
typedef struct {
    int32_t x;
    int32_t y;
} point_t;

typedef struct {
    bool active;
    char grade;
    int8_t small;
    int32_t age;
    uint64_t big;
    float ratio;
    double precise;
    const char *name;
    char tag[4];
    int32_t scores[3];
    point_t home;
    point_t pair[2];
} person_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_point_reflect, point_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_person_reflect, person_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, active),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, grade),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, small),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, age),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, big),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, ratio),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, precise),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, name),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, tag, shrewd_basic_reflect_char),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, scores, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(person_t, home, g_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, pair, g_point_reflect));

/* Oversized array: exercises the dump element cap. */
typedef struct {
    int32_t wide[40];
} wide_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_wide_reflect, wide_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(wide_t, wide, shrewd_basic_reflect_int32));

/* Oversized object: exercises the 1 MiB dump body cap (struct arrays
   render one block each, so this one really is megabytes of output). */
typedef struct {
    point_t points[100000];
} huge_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_huge_reflect, huge_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(huge_t, points, g_point_reflect));

/* Self-referential type: DEFINE forward-declares the descriptor, so it
 * can appear inside its own field table. */
typedef struct node {
    int32_t data;
    struct node *next;
} node_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_node_reflect, node_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(node_t, data),
    SHREWD_BASIC_REFLECT_FIELD_PTR(node_t, next, g_node_reflect));

/* Pointer members of every flavor. */
typedef struct {
    const char **p_str;
    void *ctx;
    int32_t *counter;
    point_t *team[2];
} refs_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_refs_reflect, refs_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR(refs_t, p_str, shrewd_basic_reflect_string),
    SHREWD_BASIC_REFLECT_FIELD_PTR(refs_t, ctx, shrewd_basic_reflect_void),
    SHREWD_BASIC_REFLECT_FIELD_PTR(refs_t, counter, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY(refs_t, team, g_point_reflect));

/* --- ops fixtures ---------------------------------------------------- */

static int g_type_ops_calls = 0;
static int g_override_ops_calls = 0;

static int ops_get(const void *pointee, void *out_value, size_t value_size)
{
    ++g_type_ops_calls;
    memcpy(out_value, pointee, value_size);
    return SHREWD_BASIC_ERR_OK;
}

static int ops_set(void *pointee, const void *value, size_t value_size)
{
    ++g_type_ops_calls;
    memcpy(pointee, value, value_size);
    return SHREWD_BASIC_ERR_OK;
}

/* Field-level override: rounds the value up by one so its use is visible. */
static int ops_get_override(const void *pointee, void *out_value, size_t value_size)
{
    int32_t value = 0;
    ++g_override_ops_calls;
    memcpy(&value, pointee, sizeof(value));
    value += 1;
    memcpy(out_value, &value, sizeof(value));
    (void)value_size;
    return SHREWD_BASIC_ERR_OK;
}

static int ops_set_override(void *pointee, const void *value, size_t value_size)
{
    int32_t rounded = 0;
    ++g_override_ops_calls;
    memcpy(&rounded, value, sizeof(rounded));
    rounded += 1;
    memcpy(pointee, &rounded, sizeof(rounded));
    (void)value_size;
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_ops_t g_type_ops = {
    .value_size = sizeof(int32_t),
    .get = ops_get,
    .set = ops_set,
    .check = NULL,
    .copy = NULL,
    .destroy = NULL };

static const shrewd_basic_reflect_ops_t g_override_ops = {
    .value_size = sizeof(int32_t),
    .get = ops_get_override,
    .set = ops_set_override,
    .check = NULL,
    .copy = NULL,
    .destroy = NULL };

/* An opaque handle whose layout only the application knows (an int32 here). */
SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(g_handle_reflect, "handle", g_type_ops);

typedef struct {
    int32_t *type_ops_slot;
    int32_t *field_ops_slot;
    int32_t *many[1];
    int32_t prim_ops;
    int32_t arr_ops[2];
} ops_refs_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_ops_refs_reflect, ops_refs_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR(ops_refs_t, type_ops_slot, g_handle_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR_OPS(ops_refs_t, field_ops_slot, g_handle_reflect,
                                       g_override_ops),
    SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY_OPS(ops_refs_t, many, g_handle_reflect,
                                             g_override_ops),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE_OPS(ops_refs_t, prim_ops, g_type_ops),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY_OPS(ops_refs_t, arr_ops, shrewd_basic_reflect_int32,
                                         g_override_ops));

static int g_check_calls = 0;

static int ops_check(const void *pointee)
{
    (void)pointee;
    ++g_check_calls;
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_ops_t g_point_ops = {
    .value_size = 0,
    .get = NULL,
    .set = NULL,
    .check = ops_check,
    .copy = NULL,
    .destroy = NULL };

SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(g_point_ops_reflect, point_t, g_point_ops,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

/* --- graph ops fixtures ---------------------------------------------- */

static int ops_handle_check(const void *pointee)
{
    int32_t value = 0;
    ++g_check_calls;
    memcpy(&value, pointee, sizeof(value));
    return (0 > value) ? SHREWD_BASIC_ERR_KEY_EXISTS : SHREWD_BASIC_ERR_OK;
}

static int ops_handle_copy(void **out, const void *pointee)
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

static void ops_handle_destroy(void *pointee)
{
    (void)shrewd_basic_platform_free(&pointee);
}

static int ops_handle_render(const void *pointee, char *buf, size_t buf_size)
{
    int32_t value = 0;
    memcpy(&value, pointee, sizeof(value));
    (void)snprintf(buf, buf_size, "handle#%d", value);
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_ops_t g_handle_ops = {
    .value_size = sizeof(int32_t),
    .get = NULL,
    .set = NULL,
    .check = ops_handle_check,
    .copy = ops_handle_copy,
    .render = ops_handle_render,
    .destroy = ops_handle_destroy };

SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(g_full_handle_reflect, "handle", g_handle_ops);

typedef struct {
    point_t *home;
    int32_t *counter;
    const char **p_str;
    int32_t *handle;
} graph_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_graph_reflect, graph_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR(graph_t, home, g_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR(graph_t, counter, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PTR(graph_t, p_str, shrewd_basic_reflect_string),
    SHREWD_BASIC_REFLECT_FIELD_PTR(graph_t, handle, g_full_handle_reflect));

/* find_field wrapper that reports failures as test failures. */
static const shrewd_basic_reflect_field_t *find_or_fail(const char *test_name,
                                                        const shrewd_basic_reflect_type_t *type,
                                                        const char *field_name)
{
    const shrewd_basic_reflect_field_t *field = NULL;
    int status = shrewd_basic_reflect_type_find_field(type, field_name, &field);
    if (SHREWD_BASIC_ERR_OK != status) {
        fprintf(stderr, "FAIL %s: find_field(%s) returned %d\n",
                test_name, field_name, status);
        ++g_failures;
        return NULL;
    }
    return field;
}

static void test_describe(void)
{
    expect_true("describe type name", 0 == strcmp(g_person_reflect.sb_refl_name, "person_t"));
    expect_true("describe type kind",
                SHREWD_BASIC_REFLECT_KIND_STRUCT == g_person_reflect.sb_refl_kind);
    expect_true("describe type size", sizeof(person_t) == g_person_reflect.sb_refl_size);
    expect_true("describe field count", (size_t)12 == g_person_reflect.sb_refl_field_count);
    expect_true("describe fields table", NULL != g_person_reflect.sb_refl_fields);
    expect_true("describe primitive has no fields",
                (0 == shrewd_basic_reflect_int32.sb_refl_field_count)
                && (NULL == shrewd_basic_reflect_int32.sb_refl_fields));

    const shrewd_basic_reflect_field_t *f = find_or_fail("describe", &g_person_reflect, "age");
    if (NULL != f) {
        expect_true("describe age type", &shrewd_basic_reflect_int32 == f->sb_refl_type);
        expect_true("describe age offset", offsetof(person_t, age) == f->sb_refl_offset);
        expect_true("describe age size", sizeof(int32_t) == f->sb_refl_size);
        expect_true("describe age count", (size_t)1 == f->sb_refl_count);
        expect_true("describe age flags", (uint32_t)0 == f->sb_refl_flags);
    }
    f = find_or_fail("describe", &g_person_reflect, "name");
    if (NULL != f) {
        expect_true("describe name type", &shrewd_basic_reflect_string == f->sb_refl_type);
        expect_true("describe name size", sizeof(const char *) == f->sb_refl_size);
    }
    f = find_or_fail("describe", &g_person_reflect, "tag");
    if (NULL != f) {
        expect_true("describe tag type", &shrewd_basic_reflect_char == f->sb_refl_type);
        expect_true("describe tag array shape",
                    ((size_t)4 == f->sb_refl_count) && ((size_t)4 == f->sb_refl_size)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY == f->sb_refl_flags));
    }
    f = find_or_fail("describe", &g_person_reflect, "scores");
    if (NULL != f) {
        expect_true("describe scores type", &shrewd_basic_reflect_int32 == f->sb_refl_type);
        expect_true("describe scores array shape",
                    ((size_t)3 == f->sb_refl_count)
                    && (3 * sizeof(int32_t) == f->sb_refl_size)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY == f->sb_refl_flags));
    }
    f = find_or_fail("describe", &g_person_reflect, "home");
    if (NULL != f) {
        expect_true("describe home type", &g_point_reflect == f->sb_refl_type);
        expect_true("describe home shape",
                    ((size_t)1 == f->sb_refl_count) && ((uint32_t)0 == f->sb_refl_flags));
    }
    f = find_or_fail("describe", &g_person_reflect, "active");
    if (NULL != f) {
        expect_true("describe active type", &shrewd_basic_reflect_bool == f->sb_refl_type);
    }
    f = find_or_fail("describe", &g_person_reflect, "big");
    if (NULL != f) {
        expect_true("describe big type", &shrewd_basic_reflect_uint64 == f->sb_refl_type);
    }

    expect_true("describe kind name int32",
                0 == strcmp(shrewd_basic_reflect_kind_name(SHREWD_BASIC_REFLECT_KIND_INT32),
                            "int32"));
    expect_true("describe kind name struct",
                0 == strcmp(shrewd_basic_reflect_kind_name(SHREWD_BASIC_REFLECT_KIND_STRUCT),
                            "struct"));
    expect_true("describe kind name unknown",
                0 == strcmp(shrewd_basic_reflect_kind_name((shrewd_basic_reflect_kind_t)99),
                            "unknown"));

    /* Traversal accessors. */
    expect_true("field count accessor",
                (size_t)12 == shrewd_basic_reflect_type_field_count(&g_person_reflect));
    expect_true("field at accessor",
                0 == strcmp(shrewd_basic_reflect_type_field_at(&g_person_reflect, 3)
                            ->sb_refl_name, "age"));
    expect_true("field at range",
                NULL == shrewd_basic_reflect_type_field_at(&g_person_reflect, 12));
    expect_true("field count on NULL", 0 == shrewd_basic_reflect_type_field_count(NULL));
}

static void test_find_field(void)
{
    const shrewd_basic_reflect_field_t *field = NULL;
    expect_eq_int("find_field hit",
                  shrewd_basic_reflect_type_find_field(&g_person_reflect, "age", &field),
                  SHREWD_BASIC_ERR_OK);
    expect_true("find_field hit pointer", field == &g_person_reflect.sb_refl_fields[3]);
    expect_eq_int("find_field miss",
                  shrewd_basic_reflect_type_find_field(&g_person_reflect, "nope", &field),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("find_field NULL type",
                  shrewd_basic_reflect_type_find_field(NULL, "age", &field),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("find_field NULL name",
                  shrewd_basic_reflect_type_find_field(&g_person_reflect, NULL, &field),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("find_field NULL out",
                  shrewd_basic_reflect_type_find_field(&g_person_reflect, "age", NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_get_set_scalars(void)
{
    person_t person;
    memset(&person, 0, sizeof(person));

    const shrewd_basic_reflect_field_t *f = find_or_fail("scalars", &g_person_reflect, "age");
    if (NULL == f) {
        return;
    }
    int32_t v32 = 42;
    expect_eq_int("set age", shrewd_basic_reflect_field_set(&person, f, &v32, sizeof(v32)),
                  SHREWD_BASIC_ERR_OK);
    int32_t r32 = 0;
    expect_eq_int("get age", shrewd_basic_reflect_field_get(&person, f, &r32, sizeof(r32)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("age round-trip", (42 == r32) && (42 == person.age));

    bool flag = true;
    f = find_or_fail("scalars", &g_person_reflect, "active");
    if (NULL != f) {
        expect_eq_int("set active",
                      shrewd_basic_reflect_field_set(&person, f, &flag, sizeof(flag)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("active round-trip", true == person.active);
    }
    char ch = 'A';
    f = find_or_fail("scalars", &g_person_reflect, "grade");
    if (NULL != f) {
        expect_eq_int("set grade", shrewd_basic_reflect_field_set(&person, f, &ch, sizeof(ch)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("grade round-trip", 'A' == person.grade);
    }
    int8_t s8 = -7;
    f = find_or_fail("scalars", &g_person_reflect, "small");
    if (NULL != f) {
        expect_eq_int("set small", shrewd_basic_reflect_field_set(&person, f, &s8, sizeof(s8)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("small round-trip", -7 == person.small);
    }
    uint64_t u64 = UINT64_C(0x1122334455667788);
    f = find_or_fail("scalars", &g_person_reflect, "big");
    if (NULL != f) {
        expect_eq_int("set big", shrewd_basic_reflect_field_set(&person, f, &u64, sizeof(u64)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("big round-trip", u64 == person.big);
    }
    float f32 = 1.5f;
    f = find_or_fail("scalars", &g_person_reflect, "ratio");
    if (NULL != f) {
        expect_eq_int("set ratio", shrewd_basic_reflect_field_set(&person, f, &f32, sizeof(f32)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("ratio round-trip", 1.5f == person.ratio);
    }
    double f64 = 2.25;
    f = find_or_fail("scalars", &g_person_reflect, "precise");
    if (NULL != f) {
        expect_eq_int("set precise",
                      shrewd_basic_reflect_field_set(&person, f, &f64, sizeof(f64)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("precise round-trip", 2.25 == person.precise);
    }

    /* Size mismatches are rejected in both directions. */
    f = find_or_fail("scalars", &g_person_reflect, "age");
    if (NULL != f) {
        expect_eq_int("get age too small",
                      shrewd_basic_reflect_field_get(&person, f, &r32, (size_t)2),
                      SHREWD_BASIC_ERR_REFLECT_MISMATCH);
        expect_eq_int("set age too big",
                      shrewd_basic_reflect_field_set(&person, f, &v32, (size_t)8),
                      SHREWD_BASIC_ERR_REFLECT_MISMATCH);
        expect_eq_int("get NULL object",
                      shrewd_basic_reflect_field_get(NULL, f, &r32, sizeof(r32)),
                      SHREWD_BASIC_ERR_ARG_INVALID);
        expect_eq_int("get NULL out",
                      shrewd_basic_reflect_field_get(&person, f, NULL, sizeof(r32)),
                      SHREWD_BASIC_ERR_ARG_INVALID);
        expect_eq_int("set NULL value",
                      shrewd_basic_reflect_field_set(&person, f, NULL, sizeof(v32)),
                      SHREWD_BASIC_ERR_ARG_INVALID);
    }
}

static void test_get_set_string(void)
{
    person_t person;
    memset(&person, 0, sizeof(person));

    const shrewd_basic_reflect_field_t *f = find_or_fail("string", &g_person_reflect, "name");
    if (NULL == f) {
        return;
    }
    const char *text = "hello";
    expect_eq_int("set name", shrewd_basic_reflect_field_set(&person, f, &text, sizeof(text)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("name pointer lands", text == person.name);
    const char *read = NULL;
    expect_eq_int("get name", shrewd_basic_reflect_field_get(&person, f, &read, sizeof(read)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("name round-trip", (text == read) && (0 == strcmp(read, "hello")));

    /* The field holds the pointer itself; a NULL pointer is a valid value. */
    const char *none = NULL;
    expect_eq_int("set name NULL",
                  shrewd_basic_reflect_field_set(&person, f, &none, sizeof(none)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("name NULL lands", NULL == person.name);
    expect_eq_int("get name NULL",
                  shrewd_basic_reflect_field_get(&person, f, &read, sizeof(read)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("name NULL round-trip", NULL == read);
}

static void test_nested(void)
{
    person_t person;
    memset(&person, 0, sizeof(person));

    const shrewd_basic_reflect_field_t *home =
        find_or_fail("nested", &g_person_reflect, "home");
    if (NULL == home) {
        return;
    }
    expect_true("nested type", &g_point_reflect == home->sb_refl_type);

    /* Nested values are addressed through the nested type's descriptors. */
    const shrewd_basic_reflect_field_t *x = find_or_fail("nested", &g_point_reflect, "x");
    if (NULL == x) {
        return;
    }
    int32_t value = 5;
    expect_eq_int("set nested x",
                  shrewd_basic_reflect_field_set(&person.home, x, &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("nested x lands", 5 == person.home.x);
    int32_t back = 0;
    expect_eq_int("get nested x",
                  shrewd_basic_reflect_field_get(&person.home, x, &back, sizeof(back)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("nested x round-trip", 5 == back);

    /* The whole nested struct can also be moved as one blob. */
    point_t point = {11, 22};
    expect_eq_int("set nested blob",
                  shrewd_basic_reflect_field_set(&person, home, &point, sizeof(point)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("nested blob lands", (11 == person.home.x) && (22 == person.home.y));
}

static void test_array(void)
{
    person_t person;
    memset(&person, 0, sizeof(person));

    const shrewd_basic_reflect_field_t *scores =
        find_or_fail("array", &g_person_reflect, "scores");
    if (NULL == scores) {
        return;
    }
    int32_t values[3] = {1, 2, 3};
    expect_eq_int("set scores",
                  shrewd_basic_reflect_field_set(&person, scores, values, sizeof(values)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("scores land", 0 == memcmp(person.scores, values, sizeof(values)));
    int32_t back[3] = {0};
    expect_eq_int("get scores",
                  shrewd_basic_reflect_field_get(&person, scores, back, sizeof(back)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("scores round-trip", 0 == memcmp(back, values, sizeof(values)));
    expect_eq_int("get scores wrong size",
                  shrewd_basic_reflect_field_get(&person, scores, back, (size_t)8),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);

    const shrewd_basic_reflect_field_t *tag = find_or_fail("array", &g_person_reflect, "tag");
    if (NULL != tag) {
        char text[4] = {'t', 'a', 'g', '\0'};
        expect_eq_int("set tag", shrewd_basic_reflect_field_set(&person, tag, text, sizeof(text)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("tag lands", 0 == memcmp(person.tag, text, sizeof(text)));
    }
}

/* Read a stream's whole content into buf (NUL-terminated). */
static void slurp(FILE *stream, char *buf, size_t size)
{
    size_t n;
    rewind(stream);
    n = fread(buf, 1, size - 1, stream);
    buf[n] = '\0';
}

/* --- enum fixtures --------------------------------------------------- */

typedef enum {
    MODE_OFF = 0,
    MODE_ON = 1,
    MODE_AUTO = 2
} mode_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(g_mode_reflect, mode_t,
    SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_OFF),
    SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_ON),
    SHREWD_BASIC_REFLECT_ENUM_VALUE(MODE_AUTO));

typedef struct {
    mode_t mode;
    mode_t *mode_ptr;
} config_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_config_reflect, config_t,
    SHREWD_BASIC_REFLECT_FIELD_ENUM(config_t, mode, g_mode_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR(config_t, mode_ptr, g_mode_reflect));
/* --- equal fixtures --------------------------------------------------- */

/* Field-level ops that always report inequality (visible override). */
static int ops_equal_always(const void *a, const void *b, bool *out_equal)
{
    (void)a;
    (void)b;
    *out_equal = false;
    return SHREWD_BASIC_ERR_OK;
}

static const shrewd_basic_reflect_ops_t g_equal_ops = {
    .value_size = 0,
    .get = NULL,
    .set = NULL,
    .check = NULL,
    .equal = ops_equal_always,
    .copy = NULL,
    .destroy = NULL };

typedef struct {
    int32_t *p;
} wrap_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_wrap_reflect, wrap_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR_OPS(wrap_t, p, shrewd_basic_reflect_int32,
                                       g_equal_ops));

static void test_equal(void)
{
    bool same = false;

    /* Scalars and strings compare by value, not by pointer. */
    person_t a;
    person_t b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.age = 30;
    b.age = 30;
    const char *alice = "Alice";
    const char *alice2 = "Alice";
    a.name = alice;
    b.name = alice2;
    expect_eq_int("equal identical",
                  shrewd_basic_reflect_equal(&a, &b, &g_person_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal identical yes", same);
    b.age = 31;
    expect_eq_int("equal scalar differs",
                  shrewd_basic_reflect_equal(&a, &b, &g_person_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal scalar differs no", !same);
    b.age = 30;
    b.name = "Bob";
    expect_eq_int("equal string differs",
                  shrewd_basic_reflect_equal(&a, &b, &g_person_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal string differs no", !same);
    b.name = alice2;
    expect_eq_int("equal string content matches",
                  shrewd_basic_reflect_equal(&a, &b, &g_person_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal string content yes", same);
    b.name = NULL;
    expect_eq_int("equal string null rules",
                  shrewd_basic_reflect_equal(&a, &b, &g_person_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal string null no", !same);

    /* Enums compare numerically. */
    config_t ca;
    config_t cb;
    memset(&ca, 0, sizeof(ca));
    memset(&cb, 0, sizeof(cb));
    ca.mode = MODE_ON;
    cb.mode = MODE_ON;
    expect_eq_int("equal enum same",
                  shrewd_basic_reflect_equal(&ca, &cb, &g_config_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal enum same yes", same);
    cb.mode = MODE_OFF;
    expect_eq_int("equal enum differs",
                  shrewd_basic_reflect_equal(&ca, &cb, &g_config_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal enum differs no", !same);

    /* Pointer targets compare deeply, whatever their addresses. */
    point_t pa;
    point_t pb;
    memset(&pa, 0, sizeof(pa));
    memset(&pb, 0, sizeof(pb));
    pa.x = 1;
    pa.y = 2;
    pb.x = 1;
    pb.y = 2;
    graph_t ga;
    graph_t gb;
    memset(&ga, 0, sizeof(ga));
    memset(&gb, 0, sizeof(gb));
    ga.home = &pa;
    gb.home = &pb;
    expect_eq_int("equal deep pointers",
                  shrewd_basic_reflect_equal(&ga, &gb, &g_graph_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal deep pointers yes", same);
    pb.x = 5;
    expect_eq_int("equal deep pointers differ",
                  shrewd_basic_reflect_equal(&ga, &gb, &g_graph_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal deep pointers differ no", !same);
    ga.home = NULL;
    expect_eq_int("equal pointer null rules",
                  shrewd_basic_reflect_equal(&ga, &gb, &g_graph_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal pointer null no", !same);
    gb.home = NULL;
    expect_eq_int("equal pointer both null",
                  shrewd_basic_reflect_equal(&ga, &gb, &g_graph_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal pointer both null yes", same);

    /* Cycles terminate and compare coinductively. */
    node_t ra;
    node_t rb;
    memset(&ra, 0, sizeof(ra));
    memset(&rb, 0, sizeof(rb));
    ra.data = 3;
    rb.data = 3;
    ra.next = &ra;
    rb.next = &rb;
    expect_eq_int("equal cycles",
                  shrewd_basic_reflect_equal(&ra, &rb, &g_node_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal cycles yes", same);
    rb.data = 4;
    expect_eq_int("equal cycles differ",
                  shrewd_basic_reflect_equal(&ra, &rb, &g_node_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal cycles differ no", !same);

    /* Field-level ops override the comparison. */
    int32_t nine_a = 9;
    int32_t nine_b = 9;
    wrap_t wa;
    wrap_t wb;
    memset(&wa, 0, sizeof(wa));
    memset(&wb, 0, sizeof(wb));
    wa.p = &nine_a;
    wb.p = &nine_b;
    expect_eq_int("equal ops override",
                  shrewd_basic_reflect_equal(&wa, &wb, &g_wrap_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal ops override no", !same);

    /* Opaque targets fall back to identity. */
    refs_t oa;
    refs_t ob;
    memset(&oa, 0, sizeof(oa));
    memset(&ob, 0, sizeof(ob));
    oa.ctx = &pa;
    ob.ctx = &pa;
    expect_eq_int("equal opaque identity",
                  shrewd_basic_reflect_equal(&oa, &ob, &g_refs_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal opaque identity yes", same);
    ob.ctx = &pb;
    expect_eq_int("equal opaque identity differs",
                  shrewd_basic_reflect_equal(&oa, &ob, &g_refs_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("equal opaque identity differs no", !same);

    expect_eq_int("equal NULL args",
                  shrewd_basic_reflect_equal(NULL, &b, &g_person_reflect, &same),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_dump_body_cap(void)
{
    static huge_t huge; /* 800 KB: too big for the stack */
    memset(&huge, 0, sizeof(huge));
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("dump cap find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    FILE *stream = tmpfile();
    expect_true("dump cap tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    /* Hitting the cap degrades to a marker, not an error. */
    expect_eq_int("dump cap status",
                  shrewd_basic_reflect_dump(&g_huge_reflect, &huge, "huge"),
                  SHREWD_BASIC_ERR_OK);
    char tail[256] = {0};
    (void)fseek(stream, 0, SEEK_END);
    long size = ftell(stream);
    long back = (long)sizeof(tail) - 1;
    (void)fseek(stream, (size > back) ? (size - back) : 0, SEEK_SET);
    size_t n = fread(tail, 1, sizeof(tail) - 1, stream);
    tail[n] = '\0';
    expect_true("dump cap marker", NULL != strstr(tail, "...(dump truncated)"));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);
}

/* --- dynamic array fixtures ------------------------------------------- */

typedef struct {
    point_t **points; /* pointer table + linked count */
    size_t point_count;
    int32_t *values; /* value vector + linked count */
    size_t value_count;
    char **argv; /* NULL-terminated pointer table (argv style) */
} dyn_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_dyn_reflect, dyn_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR_DARRAY(dyn_t, points, point_count, g_point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dyn_t, point_count),
    SHREWD_BASIC_REFLECT_FIELD_DARRAY(dyn_t, values, value_count,
                                      shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dyn_t, value_count),
    SHREWD_BASIC_REFLECT_FIELD_SENTINEL(dyn_t, argv, shrewd_basic_reflect_string));

static void test_dynamic_arrays(void)
{
    /* Descriptor shapes. */
    const shrewd_basic_reflect_field_t *field =
        find_or_fail("dyn", &g_dyn_reflect, "points");
    if (NULL == field) {
        return;
    }
    expect_true("dyn points shape",
                ((SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_PTR)
                        == field->sb_refl_flags)
                && (sizeof(point_t *) == field->sb_refl_stride)
                && ((size_t)1 == field->sb_refl_count)
                && (sizeof(size_t) == field->sb_refl_limit_size));
    field = find_or_fail("dyn", &g_dyn_reflect, "values");
    if (NULL != field) {
        expect_true("dyn values shape",
                    (SHREWD_BASIC_REFLECT_FIELD_F_DARRAY == field->sb_refl_flags)
                    && (sizeof(int32_t) == field->sb_refl_stride));
    }
    field = find_or_fail("dyn", &g_dyn_reflect, "argv");
    if (NULL != field) {
        expect_true("dyn argv shape",
                    ((SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                      | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL) == field->sb_refl_flags)
                    && ((size_t)0 == field->sb_refl_limit_size));
    }

    /* A heap-owned graph (copy/destroy own what they touch, the root
       included, so even the container is heap-allocated here). */
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, sizeof(dyn_t))) {
        return;
    }
    dyn_t *src = block;
    point_t *pa = NULL;
    point_t *pb = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_alloc(&block, sizeof(point_t))) {
        pa = block;
        pa->x = 1;
        pa->y = 2;
    }
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_alloc(&block, sizeof(point_t))) {
        pb = block;
        pb->x = 3;
        pb->y = 4;
    }
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_alloc(&block, 2 * sizeof(point_t *))) {
        src->points = block;
        src->points[0] = pa;
        src->points[1] = pb;
    }
    src->point_count = 2;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_alloc(&block, 3 * sizeof(int32_t))) {
        src->values = block;
        src->values[0] = 7;
        src->values[1] = 8;
        src->values[2] = 9;
    }
    src->value_count = 3;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_alloc(&block, 3 * sizeof(char *))) {
        src->argv = block;
        src->argv[0] = (char *)"ls";
        src->argv[1] = (char *)"-l";
        src->argv[2] = NULL;
    }

    /* dump renders all three flavors and NULL tables. */
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("dyn find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    FILE *stream = tmpfile();
    expect_true("dyn tmpfile", NULL != stream);
    if (NULL == stream) {
        (void)shrewd_basic_reflect_destroy(src, &g_dyn_reflect);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dyn dump",
                  shrewd_basic_reflect_dump(&g_dyn_reflect, src, "dyn"),
                  SHREWD_BASIC_ERR_OK);
    dyn_t empty;
    memset(&empty, 0, sizeof(empty));
    expect_eq_int("dyn dump empty",
                  shrewd_basic_reflect_dump(&g_dyn_reflect, &empty, "dyn"),
                  SHREWD_BASIC_ERR_OK);
    char buf[8192] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("dyn dump pointer table",
                (NULL != strstr(buf, "points[0] = {"))
                && (NULL != strstr(buf, "points[1] = {"))
                && (NULL != strstr(buf, "    x = 3")));
    expect_true("dyn dump value vector",
                NULL != strstr(buf, "values = [ 7, 8, 9 ]"));
    expect_true("dyn dump sentinel table",
                NULL != strstr(buf, "argv = [ \"ls\", \"-l\" ]"));
    expect_true("dyn dump null tables",
                (NULL != strstr(buf, "points = (null)"))
                && (NULL != strstr(buf, "values = (null)"))
                && (NULL != strstr(buf, "argv = (null)")));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);

    /* copy: tables and pointees are cloned, strings stay borrowed;
       equal is deep and count-aware. */
    dyn_t *twin = NULL;
    expect_eq_int("dyn copy",
                  shrewd_basic_reflect_copy((void **)&twin, src, &g_dyn_reflect),
                  SHREWD_BASIC_ERR_OK);
    bool same = false;
    expect_eq_int("dyn equal deep",
                  shrewd_basic_reflect_equal(src, twin, &g_dyn_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dyn equal deep yes", same);
    expect_true("dyn clone independent",
                (NULL != twin) && (twin != src) && (twin->points != src->points)
                && (twin->points[0] != src->points[0])
                && (1 == twin->points[0]->x) && (9 == twin->values[2]));
    expect_true("dyn clone strings aliased", twin->argv[0] == src->argv[0]);
    twin->values[0] = 70;
    expect_eq_int("dyn equal differs",
                  shrewd_basic_reflect_equal(src, twin, &g_dyn_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dyn equal differs no", !same);
    twin->values[0] = 7;
    twin->value_count = 2;
    expect_eq_int("dyn equal count differs",
                  shrewd_basic_reflect_equal(src, twin, &g_dyn_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dyn equal count differs no", !same);
    twin->value_count = 3;

    /* destroy frees cloned tables and pointees exactly once (valgrind
       is the real assertion here). */
    expect_eq_int("dyn destroy clone",
                  shrewd_basic_reflect_destroy(twin, &g_dyn_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("dyn destroy src",
                  shrewd_basic_reflect_destroy(src, &g_dyn_reflect),
                  SHREWD_BASIC_ERR_OK);
}

/* --- flexible tail fixtures ------------------------------------------- */

typedef struct {
    size_t len;
    int32_t data[];
} flex_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_flex_reflect, flex_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(flex_t, len),
    SHREWD_BASIC_REFLECT_FIELD_FLEX(flex_t, data, len, shrewd_basic_reflect_int32));

/* The GNU zero-length form is supported too (its warning is silenced
   just for this fixture). */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
typedef struct {
    size_t len;
    int32_t zero[0];
} zflex_t;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_zflex_reflect, zflex_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(zflex_t, len),
    SHREWD_BASIC_REFLECT_FIELD_FLEX(zflex_t, zero, len, shrewd_basic_reflect_int32));

static void test_flex(void)
{
    /* Descriptor shape. */
    const shrewd_basic_reflect_field_t *field =
        find_or_fail("flex", &g_flex_reflect, "data");
    if (NULL == field) {
        return;
    }
    expect_true("flex shape",
                ((SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_FLEX)
                        == field->sb_refl_flags)
                && (sizeof(int32_t) == field->sb_refl_stride)
                && ((size_t)0 == field->sb_refl_count)
                && ((size_t)0 == field->sb_refl_size)
                && (sizeof(size_t) == field->sb_refl_limit_size));
    field = find_or_fail("flex", &g_zflex_reflect, "zero");
    if (NULL != field) {
        expect_true("flex zero-length shape",
                    sizeof(int32_t) == field->sb_refl_stride);
    }

    /* A heap-owned object sized struct + tail. */
    void *block = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(
                &block, 1, sizeof(flex_t) + (3 * sizeof(int32_t)))) {
        return;
    }
    flex_t *src = block;
    src->len = 3;
    src->data[0] = 7;
    src->data[1] = 8;
    src->data[2] = 9;

    /* dump renders the tail as an inline array. */
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("flex find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    FILE *stream = tmpfile();
    expect_true("flex tmpfile", NULL != stream);
    if (NULL == stream) {
        (void)shrewd_basic_reflect_destroy(src, &g_flex_reflect);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("flex dump",
                  shrewd_basic_reflect_dump(&g_flex_reflect, src, "flex"),
                  SHREWD_BASIC_ERR_OK);
    /* A FAM struct must be allocated with room for its tail: sizeof()
       covers the header only. */
    void *zblock = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_platform_calloc(
                &zblock, 1, sizeof(zflex_t) + (2 * sizeof(int32_t)))) {
        zflex_t *zero = zblock;
        zero->len = 2;
        zero->zero[0] = 5;
        zero->zero[1] = 6;
        expect_eq_int("flex zero-length dump",
                      shrewd_basic_reflect_dump(&g_zflex_reflect, zero, "zflex"),
                      SHREWD_BASIC_ERR_OK);
        (void)shrewd_basic_reflect_destroy(zero, &g_zflex_reflect);
    }
    char buf[4096] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("flex dump tail", NULL != strstr(buf, "data = [ 7, 8, 9 ]"));
    expect_true("flex dump zero tail", NULL != strstr(buf, "zero = [ 5, 6 ]"));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);

    /* whole-tail raw access and indexed paths. */
    int32_t tail[3] = {0};
    expect_eq_int("flex field_get tail",
                  shrewd_basic_reflect_field_get(src, field, tail, sizeof(tail)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex field_get values",
                (7 == tail[0]) && (8 == tail[1]) && (9 == tail[2]));
    expect_eq_int("flex field_get size rule",
                  shrewd_basic_reflect_field_get(src, field, tail, (size_t)8),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    int32_t value = 0;
    expect_eq_int("flex path_get",
                  shrewd_basic_reflect_path_get(src, &g_flex_reflect, "data[2]",
                                                &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex path_get value", 9 == value);
    value = 80;
    expect_eq_int("flex path_set",
                  shrewd_basic_reflect_path_set(src, &g_flex_reflect, "data[1]",
                                                &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex path_set lands", 80 == src->data[1]);
    src->data[1] = 8;
    expect_eq_int("flex path_get out of range",
                  shrewd_basic_reflect_path_get(src, &g_flex_reflect, "data[5]",
                                                &value, sizeof(value)),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    const shrewd_basic_reflect_field_t *resolved = NULL;
    size_t offset = 0;
    size_t size = 0;
    expect_eq_int("flex resolve",
                  shrewd_basic_reflect_type_resolve(&g_flex_reflect, "data[2]",
                                                    &resolved, &offset, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex resolve shape",
                (offsetof(flex_t, data) + (2 * sizeof(int32_t)) == offset)
                && (sizeof(int32_t) == size));

    /* copy allocates struct + tail; equal is count-aware; destroy frees
       the whole block (valgrind asserts the accounting). */
    flex_t *clone = NULL;
    expect_eq_int("flex copy",
                  shrewd_basic_reflect_copy((void **)&clone, src, &g_flex_reflect),
                  SHREWD_BASIC_ERR_OK);
    bool same = false;
    expect_eq_int("flex equal",
                  shrewd_basic_reflect_equal(src, clone, &g_flex_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex equal yes", same);
    expect_true("flex clone independent",
                (NULL != clone) && (clone->data != src->data)
                && (3 == clone->len) && (9 == clone->data[2]));
    clone->data[0] = 70;
    expect_eq_int("flex equal differs",
                  shrewd_basic_reflect_equal(src, clone, &g_flex_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex equal differs no", !same);
    clone->data[0] = 7;
    clone->len = 2;
    expect_eq_int("flex equal count differs",
                  shrewd_basic_reflect_equal(src, clone, &g_flex_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("flex equal count differs no", !same);
    expect_eq_int("flex destroy clone",
                  shrewd_basic_reflect_destroy(clone, &g_flex_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("flex destroy src",
                  shrewd_basic_reflect_destroy(src, &g_flex_reflect),
                  SHREWD_BASIC_ERR_OK);
}

static void test_logging_policy(void)
{
    FILE *stream = tmpfile();
    expect_true("logging tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("logging find module",
                  shrewd_basic_log_module_find("reflect", &module), SHREWD_BASIC_ERR_OK);
    expect_true("logging module found", NULL != module);
    shrewd_basic_log_module_set_stream(module, stream);

    const shrewd_basic_reflect_field_t *f =
        find_or_fail("logging", &g_person_reflect, "age");
    person_t person;
    memset(&person, 0, sizeof(person));
    person.age = 7;
    int32_t value = 0;
    expect_eq_int("logging WARN path",
                  shrewd_basic_reflect_field_get(NULL, f, &value, sizeof(value)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    /* Successful query must stay silent. */
    expect_eq_int("logging silent query",
                  shrewd_basic_reflect_type_find_field(&g_person_reflect, "age", &f),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("logging TRACE path",
                  shrewd_basic_reflect_field_get(&person, f, &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);

    char buf[2048] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("logging WARN emitted",
                (NULL != strstr(buf, "[WARN]"))
                && (NULL != strstr(buf, "field_get: object")));
    expect_true("logging TRACE emitted",
                (NULL != strstr(buf, "[TRACE]"))
                && (NULL != strstr(buf, "field_get ok")));
    expect_true("logging query silent", NULL == strstr(buf, "find_field"));

    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);
}

static void test_dump(void)
{
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("dump find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dump module found", NULL != module);
    if (NULL == module) {
        return;
    }

    person_t person;
    memset(&person, 0, sizeof(person));
    person.active = true;
    person.grade = 'A';
    person.small = -7;
    person.age = 30;
    person.big = 5;
    person.ratio = 1.5f;
    person.precise = 2.25;
    person.name = "Alice";
    person.tag[0] = 't';
    person.tag[1] = 'a';
    person.tag[2] = 'g';
    person.tag[3] = '\0';
    person.scores[0] = 1;
    person.scores[1] = 2;
    person.scores[2] = 3;
    person.home.x = 4;
    person.home.y = 5;
    person.pair[0].x = 6;
    person.pair[0].y = 7;
    person.pair[1].x = 8;
    person.pair[1].y = 9;

    /* Phase 1: full rendering of every field kind. */
    FILE *stream = tmpfile();
    expect_true("dump tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dump ok",
                  shrewd_basic_reflect_dump(&g_person_reflect, &person, "person"),
                  SHREWD_BASIC_ERR_OK);

    char buf[8192] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("dump header", NULL != strstr(buf, "person (person_t) {"));
    expect_true("dump bool", NULL != strstr(buf, "  active = true"));
    expect_true("dump char", NULL != strstr(buf, "  grade = 'A'"));
    expect_true("dump int8", NULL != strstr(buf, "  small = -7"));
    expect_true("dump int32", NULL != strstr(buf, "  age = 30"));
    expect_true("dump uint64", NULL != strstr(buf, "  big = 5"));
    expect_true("dump float", NULL != strstr(buf, "  ratio = 1.5"));
    expect_true("dump double", NULL != strstr(buf, "  precise = 2.25"));
    expect_true("dump string", NULL != strstr(buf, "  name = \"Alice\""));
    expect_true("dump char array",
                NULL != strstr(buf, "  tag = [ 't', 'a', 'g', '\\x00' ]"));
    expect_true("dump int array", NULL != strstr(buf, "  scores = [ 1, 2, 3 ]"));
    expect_true("dump struct field",
                (NULL != strstr(buf, "  home = {"))
                && (NULL != strstr(buf, "    x = 4"))
                && (NULL != strstr(buf, "    y = 5")));
    expect_true("dump struct array",
                (NULL != strstr(buf, "  pair[0] = {"))
                && (NULL != strstr(buf, "  pair[1] = {"))
                && (NULL != strstr(buf, "    x = 8"))
                && (NULL != strstr(buf, "    y = 9")));
    /* One log record: only the first line carries the prefix, the body
       starts on the next line, and the prefix points at the dump call
       site (the macro) rather than reflect_dump.c. */
    expect_true("dump single record",
                NULL != strstr(buf, "[dump] \nperson (person_t) {"));
    expect_true("dump call site", NULL != strstr(buf, "test_reflect.c:"));
    expect_true("dump nested close", NULL != strstr(buf, "    y = 5\n  }\n"));
    expect_true("dump top close", NULL != strstr(buf, "  }\n}\n"));
    fclose(stream);

    /* Phase 2: escapes and the label-free header. */
    person_t weird;
    memset(&weird, 0, sizeof(weird));
    weird.name = "a\"b\nc";
    stream = tmpfile();
    if (NULL == stream) {
        shrewd_basic_log_module_set_stream(module, stderr);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dump escapes",
                  shrewd_basic_reflect_dump(&g_person_reflect, &weird, NULL),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("dump no-label header", NULL != strstr(buf, "person_t {"));
    expect_true("dump string escapes", NULL != strstr(buf, "name = \"a\\\"b\\nc\""));
    fclose(stream);

    /* Phase 3: NULL string and long-string truncation. */
    weird.name = NULL;
    stream = tmpfile();
    if (NULL == stream) {
        shrewd_basic_log_module_set_stream(module, stderr);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dump null string",
                  shrewd_basic_reflect_dump(&g_person_reflect, &weird, NULL),
                  SHREWD_BASIC_ERR_OK);
    char huge[300];
    memset(huge, 'x', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    weird.name = huge;
    expect_eq_int("dump long string",
                  shrewd_basic_reflect_dump(&g_person_reflect, &weird, NULL),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("dump null string value", NULL != strstr(buf, "name = (null)"));
    expect_true("dump string truncated", NULL != strstr(buf, "...(truncated)"));
    fclose(stream);

    /* Phase 4: array element cap and error paths. */
    wide_t wide;
    memset(&wide, 0, sizeof(wide));
    wide.wide[39] = 9;
    stream = tmpfile();
    if (NULL == stream) {
        shrewd_basic_log_module_set_stream(module, stderr);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dump wide array",
                  shrewd_basic_reflect_dump(&g_wide_reflect, &wide, "wide"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("dump array capped", NULL != strstr(buf, "...(8 more)"));

    expect_eq_int("dump NULL type",
                  shrewd_basic_reflect_dump(NULL, &person, "person"),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("dump NULL object",
                  shrewd_basic_reflect_dump(&g_person_reflect, NULL, "person"),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("dump non-struct",
                  shrewd_basic_reflect_dump(&shrewd_basic_reflect_int32,
                                            &person.age, NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);
}

static void test_cross_tu(void)
{
    /* The foreign translation unit must see the very same descriptor. */
    point_t point;
    memset(&point, 0, sizeof(point));
    expect_eq_int("cross_tu set_x",
                  shrewd_test_reflect_ext_set_x(&point, 17), SHREWD_BASIC_ERR_OK);
    expect_true("cross_tu set lands", 17 == point.x);
    int32_t value = 0;
    expect_eq_int("cross_tu get_x",
                  shrewd_test_reflect_ext_get_x(&point, &value), SHREWD_BASIC_ERR_OK);
    expect_true("cross_tu round-trip", 17 == value);
    expect_true("cross_tu field count", 2 == shrewd_test_reflect_ext_field_count());
}

static void test_pointer_fields(void)
{
    /* Descriptor shape. */
    const shrewd_basic_reflect_field_t *f =
        find_or_fail("pointer", &g_node_reflect, "next");
    if (NULL != f) {
        expect_true("pointer self target", &g_node_reflect == f->sb_refl_type);
        expect_true("pointer shape",
                    ((size_t)1 == f->sb_refl_count) && (sizeof(node_t *) == f->sb_refl_size)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_PTR == f->sb_refl_flags));
    }
    f = find_or_fail("pointer", &g_refs_reflect, "p_str");
    if (NULL != f) {
        expect_true("pointer string target", &shrewd_basic_reflect_string == f->sb_refl_type);
    }
    f = find_or_fail("pointer", &g_refs_reflect, "ctx");
    if (NULL != f) {
        expect_true("pointer opaque target",
                    (&shrewd_basic_reflect_void == f->sb_refl_type)
                    && (SHREWD_BASIC_REFLECT_KIND_OPAQUE == f->sb_refl_type->sb_refl_kind));
    }
    f = find_or_fail("pointer", &g_refs_reflect, "team");
    if (NULL != f) {
        expect_true("pointer array shape",
                    (&g_point_reflect == f->sb_refl_type)
                    && ((size_t)2 == f->sb_refl_count)
                    && (2 * sizeof(point_t *) == f->sb_refl_size)
                    && ((SHREWD_BASIC_REFLECT_FIELD_F_PTR
                         | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY) == f->sb_refl_flags));
    }
    expect_true("pointer kind name opaque",
                0 == strcmp(shrewd_basic_reflect_kind_name(SHREWD_BASIC_REFLECT_KIND_OPAQUE),
                            "opaque"));

    /* get/set copy the pointer value, never the pointee. */
    refs_t refs;
    memset(&refs, 0, sizeof(refs));
    int32_t value = 9;
    int32_t other = 11;
    const shrewd_basic_reflect_field_t *counter =
        find_or_fail("pointer", &g_refs_reflect, "counter");
    if (NULL == counter) {
        return;
    }
    int32_t *link = &value;
    expect_eq_int("pointer set counter",
                  shrewd_basic_reflect_field_set(&refs, counter, &link, sizeof(link)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("pointer set lands", &value == refs.counter);
    int32_t *read = NULL;
    expect_eq_int("pointer get counter",
                  shrewd_basic_reflect_field_get(&refs, counter, &read, sizeof(read)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("pointer round-trip", &value == read);
    link = &other;
    expect_eq_int("pointer re-point",
                  shrewd_basic_reflect_field_set(&refs, counter, &link, sizeof(link)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("pointer re-point lands", &other == refs.counter);
    expect_eq_int("pointer size mismatch",
                  shrewd_basic_reflect_field_set(&refs, counter, &link, (size_t)4),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);

    /* Self-referential pointer through the shared descriptor. */
    node_t tail;
    memset(&tail, 0, sizeof(tail));
    node_t head;
    memset(&head, 0, sizeof(head));
    const shrewd_basic_reflect_field_t *next =
        find_or_fail("pointer", &g_node_reflect, "next");
    if (NULL != next) {
        node_t *link_node = &tail;
        expect_eq_int("pointer self set",
                      shrewd_basic_reflect_field_set(&head, next, &link_node, sizeof(link_node)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("pointer self lands", &tail == head.next);
    }
}

static void test_dump_pointers(void)
{
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("dump_ptr find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    expect_true("dump_ptr module found", NULL != module);
    if (NULL == module) {
        return;
    }

    /* Phase 1: followed pointers of every flavor. */
    const char *text = "hi";
    int32_t counter = 7;
    point_t point;
    memset(&point, 0, sizeof(point));
    point.x = 1;
    point.y = 2;
    refs_t refs;
    memset(&refs, 0, sizeof(refs));
    refs.p_str = &text;
    refs.ctx = &counter; /* any address: opaque targets show 0x... */
    refs.counter = &counter;
    refs.team[0] = &point;
    refs.team[1] = NULL;

    FILE *stream = tmpfile();
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dump_ptr ok",
                  shrewd_basic_reflect_dump(&g_refs_reflect, &refs, "refs"),
                  SHREWD_BASIC_ERR_OK);
    char buf[8192] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("dump_ptr string followed", NULL != strstr(buf, "p_str = \"hi\""));
    expect_true("dump_ptr scalar followed", NULL != strstr(buf, "counter = 7"));
    expect_true("dump_ptr opaque address", NULL != strstr(buf, "ctx = 0x"));
    expect_true("dump_ptr array follows",
                (NULL != strstr(buf, "team[0] = {"))
                && (NULL != strstr(buf, "    x = 1"))
                && (NULL != strstr(buf, "team[1] = (null)")));

    /* NULL pointer shows (null) instead of following. */
    refs.p_str = NULL;
    refs.counter = NULL;
    expect_eq_int("dump_ptr null",
                  shrewd_basic_reflect_dump(&g_refs_reflect, &refs, "refs"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("dump_ptr null string", NULL != strstr(buf, "p_str = (null)"));
    expect_true("dump_ptr null scalar", NULL != strstr(buf, "counter = (null)"));
    fclose(stream);

    /* Phase 2: self-referential chain terminates via the depth cap. */
    node_t tail;
    memset(&tail, 0, sizeof(tail));
    tail.data = 2;
    node_t head;
    memset(&head, 0, sizeof(head));
    head.data = 1;
    head.next = &tail;
    stream = tmpfile();
    if (NULL == stream) {
        shrewd_basic_log_module_set_stream(module, stderr);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dump_ptr chain",
                  shrewd_basic_reflect_dump(&g_node_reflect, &head, "node"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("dump_ptr chain nested",
                (NULL != strstr(buf, "  next = {"))
                && (NULL != strstr(buf, "    data = 2"))
                && (NULL != strstr(buf, "    next = (null)")));
    fclose(stream);

    node_t loop;
    memset(&loop, 0, sizeof(loop));
    loop.next = &loop;
    stream = tmpfile();
    if (NULL == stream) {
        shrewd_basic_log_module_set_stream(module, stderr);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("dump_ptr cycle",
                  shrewd_basic_reflect_dump(&g_node_reflect, &loop, "node"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("dump_ptr cycle detected", NULL != strstr(buf, "...(cyclic)"));
    expect_true("dump_ptr cycle no spam", NULL == strstr(buf, "...(max depth)"));

    /* A deep but finite chain still hits the depth cap. */
    node_t chain[20];
    memset(chain, 0, sizeof(chain));
    for (size_t i = 0; i + 1 < 20; ++i) {
        chain[i].data = (int32_t)i;
        chain[i].next = &chain[i + 1];
    }
    expect_eq_int("dump_ptr deep chain",
                  shrewd_basic_reflect_dump(&g_node_reflect, &chain[0], "node"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("dump_ptr deep chain capped", NULL != strstr(buf, "...(max depth)"));

    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);
}

static void test_paths(void)
{
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t offset = 0;
    size_t size = 0;

    /* resolve: value nesting and indices. */
    expect_eq_int("resolve home.x",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "home.x",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("resolve home.x shape",
                (0 == strcmp(field->sb_refl_name, "x"))
                && (offsetof(person_t, home) + offsetof(point_t, x) == offset)
                && (sizeof(int32_t) == size));
    expect_eq_int("resolve pair[1].y",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "pair[1].y",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("resolve pair[1].y shape",
                (offsetof(person_t, pair) + sizeof(point_t) + offsetof(point_t, y) == offset)
                && (sizeof(int32_t) == size));
    expect_eq_int("resolve scores",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "scores",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("resolve scores is the whole array", 3 * sizeof(int32_t) == size);
    expect_eq_int("resolve scores[2]",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "scores[2]",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("resolve scores[2] is one element",
                (offsetof(person_t, scores) + 2 * sizeof(int32_t) == offset)
                && (sizeof(int32_t) == size));

    /* Misses, bounds and syntax. */
    expect_eq_int("resolve miss",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "nope",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("resolve nested miss",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "home.nope",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("resolve index out of range",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "scores[3]",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("resolve empty path",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve trailing dot",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "home.",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve double dot",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "home..x",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve leading digit",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "1bad",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve index on scalar",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "age[0]",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve double index",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "pair[1][0]",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve non-numeric index",
                  shrewd_basic_reflect_type_resolve(&g_person_reflect, "scores[x]",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve NULL type",
                  shrewd_basic_reflect_type_resolve(NULL, "age", &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    /* path_get/path_set on value paths. */
    person_t person;
    memset(&person, 0, sizeof(person));
    int32_t value = 21;
    expect_eq_int("path_set home.x",
                  shrewd_basic_reflect_path_set(&person, &g_person_reflect,
                                                "home.x", &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_set home.x lands", 21 == person.home.x);
    int32_t back = 0;
    expect_eq_int("path_get home.x",
                  shrewd_basic_reflect_path_get(&person, &g_person_reflect,
                                                "home.x", &back, sizeof(back)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_get home.x value", 21 == back);
    value = 5;
    expect_eq_int("path_set pair[1].y",
                  shrewd_basic_reflect_path_set(&person, &g_person_reflect,
                                                "pair[1].y", &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_set pair[1].y lands", 5 == person.pair[1].y);
    int32_t scores[3] = {7, 8, 9};
    expect_eq_int("path_set scores blob",
                  shrewd_basic_reflect_path_set(&person, &g_person_reflect,
                                                "scores", scores, sizeof(scores)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_set scores blob lands", 0 == memcmp(person.scores, scores, sizeof(scores)));
    value = 42;
    expect_eq_int("path_set scores[2]",
                  shrewd_basic_reflect_path_set(&person, &g_person_reflect,
                                                "scores[2]", &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_set scores[2] lands", 42 == person.scores[2]);
    expect_eq_int("path_get size mismatch",
                  shrewd_basic_reflect_path_get(&person, &g_person_reflect,
                                                "home.x", &back, (size_t)2),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);

    /* Pointer segments are followed for addressing. */
    node_t tail;
    memset(&tail, 0, sizeof(tail));
    node_t head;
    memset(&head, 0, sizeof(head));
    head.next = &tail;
    value = 7;
    expect_eq_int("path_set next.data",
                  shrewd_basic_reflect_path_set(&head, &g_node_reflect,
                                                "next.data", &value, sizeof(value)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_set next.data lands", 7 == tail.data);
    expect_eq_int("path_get next.data",
                  shrewd_basic_reflect_path_get(&head, &g_node_reflect,
                                                "next.data", &back, sizeof(back)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_get next.data value", 7 == back);
    node_t *link = NULL;
    expect_eq_int("path_get next (pointer value)",
                  shrewd_basic_reflect_path_get(&head, &g_node_reflect,
                                                "next", &link, sizeof(link)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("path_get next copies pointer", &tail == link);
    head.next = NULL;
    expect_eq_int("path_get through NULL",
                  shrewd_basic_reflect_path_get(&head, &g_node_reflect,
                                                "next.data", &back, sizeof(back)),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("path_set through NULL",
                  shrewd_basic_reflect_path_set(&head, &g_node_reflect,
                                                "next.data", &value, sizeof(value)),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_true("path_set through NULL untouched", 7 == tail.data);

    /* resolve is metadata-only: crossing a pointer is not static. */
    head.next = &tail;
    expect_eq_int("resolve crosses pointer",
                  shrewd_basic_reflect_type_resolve(&g_node_reflect, "next.data",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("resolve pointer field itself",
                  shrewd_basic_reflect_type_resolve(&g_node_reflect, "next",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("resolve pointer field size", sizeof(node_t *) == size);

    expect_eq_int("path_get NULL path",
                  shrewd_basic_reflect_path_get(&person, &g_person_reflect,
                                                NULL, &back, sizeof(back)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("path_set NULL value",
                  shrewd_basic_reflect_path_set(&person, &g_person_reflect,
                                                "home.x", NULL, sizeof(value)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}


static void test_enum(void)
{
    /* Descriptor shape. */
    expect_true("enum kind", SHREWD_BASIC_REFLECT_KIND_ENUM == g_mode_reflect.sb_refl_kind);
    expect_true("enum name", 0 == strcmp(g_mode_reflect.sb_refl_name, "mode_t"));
    expect_true("enum size", sizeof(mode_t) == g_mode_reflect.sb_refl_size);
    expect_true("enum entries", (size_t)3 == g_mode_reflect.sb_refl_enum_count);
    expect_true("enum kind name",
                0 == strcmp(shrewd_basic_reflect_kind_name(SHREWD_BASIC_REFLECT_KIND_ENUM),
                            "enum"));

    /* Enum traversal accessors. */
    expect_true("enum count accessor",
                (size_t)3 == shrewd_basic_reflect_enum_count(&g_mode_reflect));
    expect_true("enum at accessor",
                0 == strcmp(shrewd_basic_reflect_enum_at(&g_mode_reflect, 1)
                            ->sb_refl_name, "MODE_ON"));
    expect_true("enum at range",
                NULL == shrewd_basic_reflect_enum_at(&g_mode_reflect, 3));
    expect_true("enum count on struct", 0 == shrewd_basic_reflect_enum_count(&g_person_reflect));

    /* name/value lookup. */
    const char *name = NULL;
    expect_eq_int("enum find name",
                  shrewd_basic_reflect_enum_find_name(&g_mode_reflect, 1, &name),
                  SHREWD_BASIC_ERR_OK);
    expect_true("enum find name value", 0 == strcmp(name, "MODE_ON"));
    expect_eq_int("enum find name unknown",
                  shrewd_basic_reflect_enum_find_name(&g_mode_reflect, 99, &name),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    int64_t value = 0;
    expect_eq_int("enum find value",
                  shrewd_basic_reflect_enum_find_value(&g_mode_reflect, "MODE_AUTO", &value),
                  SHREWD_BASIC_ERR_OK);
    expect_true("enum find value found", 2 == value);
    expect_eq_int("enum find value unknown",
                  shrewd_basic_reflect_enum_find_value(&g_mode_reflect, "MODE_X", &value),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("enum find on non-enum",
                  shrewd_basic_reflect_enum_find_name(&shrewd_basic_reflect_int32, 1, &name),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("enum find NULL args",
                  shrewd_basic_reflect_enum_find_value(NULL, "MODE_ON", &value),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    /* Field binding plus raw and path access. */
    config_t config;
    memset(&config, 0, sizeof(config));
    config.mode = MODE_ON;
    const shrewd_basic_reflect_field_t *field =
        find_or_fail("enum", &g_config_reflect, "mode");
    if (NULL == field) {
        return;
    }
    expect_true("enum field type", &g_mode_reflect == field->sb_refl_type);
    mode_t back = MODE_OFF;
    expect_eq_int("enum field get",
                  shrewd_basic_reflect_field_get(&config, field, &back, sizeof(back)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("enum field get value", MODE_ON == back);
    back = MODE_AUTO;
    expect_eq_int("enum field set",
                  shrewd_basic_reflect_field_set(&config, field, &back, sizeof(back)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("enum field set lands", MODE_AUTO == config.mode);
    back = MODE_OFF;
    expect_eq_int("enum path get",
                  shrewd_basic_reflect_path_get(&config, &g_config_reflect,
                                                "mode", &back, sizeof(back)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("enum path get value", MODE_AUTO == back);

    /* dump renders constants by name; unknown values show their number. */
    config.mode_ptr = &config.mode;
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("enum find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    FILE *stream = tmpfile();
    expect_true("enum tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("enum dump",
                  shrewd_basic_reflect_dump(&g_config_reflect, &config, "config"),
                  SHREWD_BASIC_ERR_OK);
    config.mode = (mode_t)99;
    expect_eq_int("enum dump unknown",
                  shrewd_basic_reflect_dump(&g_config_reflect, &config, "config"),
                  SHREWD_BASIC_ERR_OK);
    char buf[4096] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("enum dump name", NULL != strstr(buf, "mode = MODE_AUTO"));
    expect_true("enum dump followed", NULL != strstr(buf, "mode_ptr = MODE_AUTO"));
    expect_true("enum dump unknown value", NULL != strstr(buf, "mode = 99"));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);
}

static void test_ops(void)
{
    /* Attachment shapes. */
    expect_true("ops type attach", &g_point_ops == g_point_ops_reflect.sb_refl_ops);
    const shrewd_basic_reflect_field_t *type_field =
        find_or_fail("ops", &g_ops_refs_reflect, "type_ops_slot");
    const shrewd_basic_reflect_field_t *field_field =
        find_or_fail("ops", &g_ops_refs_reflect, "field_ops_slot");
    const shrewd_basic_reflect_field_t *array_field =
        find_or_fail("ops", &g_ops_refs_reflect, "many");
    if ((NULL == type_field) || (NULL == field_field) || (NULL == array_field)) {
        return;
    }
    expect_true("ops field falls back to type",
                (NULL == type_field->sb_refl_ops) && (&g_handle_reflect == type_field->sb_refl_type));
    expect_true("ops field override wins", &g_override_ops == field_field->sb_refl_ops);
    const shrewd_basic_reflect_field_t *prim_ops_field =
        find_or_fail("ops", &g_ops_refs_reflect, "prim_ops");
    const shrewd_basic_reflect_field_t *arr_ops_field =
        find_or_fail("ops", &g_ops_refs_reflect, "arr_ops");
    if ((NULL != prim_ops_field) && (NULL != arr_ops_field)) {
        expect_true("ops primitive field", &g_type_ops == prim_ops_field->sb_refl_ops);
        expect_true("ops array field",
                    (&g_override_ops == arr_ops_field->sb_refl_ops)
                    && ((size_t)2 == arr_ops_field->sb_refl_count)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY == arr_ops_field->sb_refl_flags));
    }
    expect_true("ops opaque target", &g_type_ops == g_handle_reflect.sb_refl_ops);

    /* value_get/set through the type-level ops. */
    ops_refs_t refs;
    memset(&refs, 0, sizeof(refs));
    int32_t target = 5;
    refs.type_ops_slot = &target;
    int32_t out = 0;
    g_type_ops_calls = 0;
    expect_eq_int("value_get type ops",
                  shrewd_basic_reflect_value_get(&refs, type_field, &out, sizeof(out)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("value_get type ops value", (5 == out) && (1 == g_type_ops_calls));
    int32_t in = 6;
    expect_eq_int("value_set type ops",
                  shrewd_basic_reflect_value_set(&refs, type_field, &in, sizeof(in)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("value_set type ops lands", (6 == target) && (2 == g_type_ops_calls));

    /* Field-level ops override the type's. */
    int32_t target2 = 10;
    refs.field_ops_slot = &target2;
    g_type_ops_calls = 0;
    g_override_ops_calls = 0;
    expect_eq_int("value_get override ops",
                  shrewd_basic_reflect_value_get(&refs, field_field, &out, sizeof(out)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("value_get override ran",
                (11 == out) && (1 == g_override_ops_calls) && (0 == g_type_ops_calls));
    in = 20;
    expect_eq_int("value_set override ops",
                  shrewd_basic_reflect_value_set(&refs, field_field, &in, sizeof(in)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("value_set override lands", (21 == target2) && (2 == g_override_ops_calls));

    /* Size rule, NULL pointee, arrays and missing ops. */
    expect_eq_int("value_get size mismatch",
                  shrewd_basic_reflect_value_get(&refs, type_field, &out, (size_t)2),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    refs.type_ops_slot = NULL;
    expect_eq_int("value_get NULL pointee",
                  shrewd_basic_reflect_value_get(&refs, type_field, &out, sizeof(out)),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("value_get on array",
                  shrewd_basic_reflect_value_get(&refs, array_field, &out, sizeof(out)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    const shrewd_basic_reflect_field_t *plain =
        find_or_fail("ops", &g_person_reflect, "age");
    person_t person;
    memset(&person, 0, sizeof(person));
    if (NULL != plain) {
        expect_eq_int("value_get without ops",
                      shrewd_basic_reflect_value_get(&person, plain, &out, sizeof(out)),
                      SHREWD_BASIC_ERR_ARG_INVALID);
    }
    expect_eq_int("value_get NULL object",
                  shrewd_basic_reflect_value_get(NULL, type_field, &out, sizeof(out)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_graph_ops(void)
{
    point_t home;
    memset(&home, 0, sizeof(home));
    home.x = 1;
    home.y = 2;
    int32_t counter = 3;
    const char *text = "hi";
    int32_t handle = 4;
    graph_t graph;
    memset(&graph, 0, sizeof(graph));
    graph.home = &home;
    graph.counter = &counter;
    graph.p_str = &text;
    graph.handle = &handle;

    /* check: ops->check invariants, NULL targets skipped. */
    g_check_calls = 0;
    expect_eq_int("check ok", shrewd_basic_reflect_check(&graph, &g_graph_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_true("check ops ran", 1 == g_check_calls);
    handle = -1;
    expect_eq_int("check invariant",
                  shrewd_basic_reflect_check(&graph, &g_graph_reflect),
                  SHREWD_BASIC_ERR_KEY_EXISTS);
    handle = 4;
    graph.home = NULL;
    expect_eq_int("check skips NULL",
                  shrewd_basic_reflect_check(&graph, &g_graph_reflect),
                  SHREWD_BASIC_ERR_OK);
    graph.home = &home;
    expect_eq_int("check NULL object",
                  shrewd_basic_reflect_check(NULL, &g_graph_reflect),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    /* copy: generic struct/scalar clones, ops clone, string aliasing. */
    void *raw = NULL;
    expect_eq_int("copy ok", shrewd_basic_reflect_copy(&raw, &graph, &g_graph_reflect),
                  SHREWD_BASIC_ERR_OK);
    graph_t *clone = raw;
    expect_true("copy struct cloned",
                (NULL != clone->home) && (clone->home != &home)
                && (1 == clone->home->x) && (2 == clone->home->y));
    expect_true("copy scalar cloned",
                (NULL != clone->counter) && (clone->counter != &counter)
                && (3 == *clone->counter));
    expect_true("copy handle via ops",
                (NULL != clone->handle) && (clone->handle != &handle)
                && (4 == *clone->handle));
    expect_true("copy string aliased", &text == clone->p_str);
    home.x = 99;
    counter = 99;
    handle = 99;
    expect_true("copy independent",
                (1 == clone->home->x) && (3 == *clone->counter)
                && (4 == *clone->handle));
    expect_eq_int("check clone", shrewd_basic_reflect_check(clone, &g_graph_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("destroy clone",
                  shrewd_basic_reflect_destroy(clone, &g_graph_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_true("destroy left borrowed string", 0 == strcmp(text, "hi"));

    /* Opaque targets without ops cannot be cloned. */
    refs_t refs;
    memset(&refs, 0, sizeof(refs));
    refs.ctx = &counter;
    void *bad = NULL;
    expect_eq_int("copy opaque without ops",
                  shrewd_basic_reflect_copy(&bad, &refs, &g_refs_reflect),
                  SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED);

    /* Cyclic and shared graphs: the clone map and the visited set make
       copy and destroy sound - the clone keeps the cycle, shared sources
       stay shared, and every clone is freed exactly once. */
    node_t loop;
    memset(&loop, 0, sizeof(loop));
    loop.next = &loop;
    void *loop_clone = NULL;
    expect_eq_int("copy cycle",
                  shrewd_basic_reflect_copy(&loop_clone, &loop, &g_node_reflect),
                  SHREWD_BASIC_ERR_OK);
    node_t *cycle = loop_clone;
    expect_true("copy cycle shape",
                (NULL != cycle) && (cycle != &loop) && (cycle->next == cycle));
    expect_eq_int("check cycle",
                  shrewd_basic_reflect_check(loop_clone, &g_node_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("destroy cycle",
                  shrewd_basic_reflect_destroy(loop_clone, &g_node_reflect),
                  SHREWD_BASIC_ERR_OK);

    node_t ring_a;
    node_t ring_b;
    memset(&ring_a, 0, sizeof(ring_a));
    memset(&ring_b, 0, sizeof(ring_b));
    ring_a.next = &ring_b;
    ring_b.next = &ring_a;
    void *ring_clone = NULL;
    expect_eq_int("copy ring",
                  shrewd_basic_reflect_copy(&ring_clone, &ring_a, &g_node_reflect),
                  SHREWD_BASIC_ERR_OK);
    node_t *ring_head = ring_clone;
    expect_true("copy ring shape",
                (NULL != ring_head) && (ring_head != &ring_a)
                && (ring_head->next != &ring_b)
                && (ring_head->next->next == ring_head));
    expect_eq_int("destroy ring",
                  shrewd_basic_reflect_destroy(ring_clone, &g_node_reflect),
                  SHREWD_BASIC_ERR_OK);

    /* Diamond: two references to one object stay one shared clone. */
    point_t shared;
    memset(&shared, 0, sizeof(shared));
    shared.x = 7;
    refs_t diamond;
    memset(&diamond, 0, sizeof(diamond));
    diamond.team[0] = &shared;
    diamond.team[1] = &shared;
    void *diamond_clone = NULL;
    expect_eq_int("copy diamond",
                  shrewd_basic_reflect_copy(&diamond_clone, &diamond, &g_refs_reflect),
                  SHREWD_BASIC_ERR_OK);
    refs_t *d = diamond_clone;
    expect_true("copy diamond shared",
                (NULL != d->team[0]) && (d->team[0] == d->team[1])
                && (d->team[0] != &shared) && (7 == d->team[0]->x));
    expect_eq_int("destroy diamond",
                  shrewd_basic_reflect_destroy(diamond_clone, &g_refs_reflect),
                  SHREWD_BASIC_ERR_OK);

    expect_eq_int("copy NULL out",
                  shrewd_basic_reflect_copy(NULL, &graph, &g_graph_reflect),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("destroy NULL",
                  shrewd_basic_reflect_destroy(NULL, &g_graph_reflect),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("destroy primitive root",
                  shrewd_basic_reflect_destroy(&counter, &shrewd_basic_reflect_int32),
                  SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED);
}

/* --- visitor fixtures ------------------------------------------------- */

#define VISIT_LOG_MAX 128

typedef struct visit_entry {
    shrewd_basic_reflect_visit_event_t event;
    char path[64];
    int value_null;
    size_t count;
} visit_entry_t;

typedef struct visit_rec {
    visit_entry_t entries[VISIT_LOG_MAX];
    size_t count;
    shrewd_basic_reflect_visit_event_t prune_event; /* prune when this matches */
    const char *prune_path;
    size_t stop_after; /* abort the walk after this many events; 0 = never */
} visit_rec_t;

static shrewd_basic_reflect_visit_action_t visit_collect(
        void *ctx, const shrewd_basic_reflect_visit_node_t *node)
{
    visit_rec_t *rec = (visit_rec_t *)ctx;
    if (rec->count < VISIT_LOG_MAX) {
        visit_entry_t *entry = &rec->entries[rec->count];
        size_t n = strlen(node->path);
        if (n >= sizeof(entry->path)) {
            n = sizeof(entry->path) - 1;
        }
        memcpy(entry->path, node->path, n);
        entry->path[n] = '\0';
        entry->event = node->event;
        entry->value_null = (NULL == node->value) ? 1 : 0;
        entry->count = node->count;
    }
    ++rec->count;
    if ((node->event == rec->prune_event) && (NULL != rec->prune_path)
            && (0 == strcmp(node->path, rec->prune_path))) {
        return SHREWD_BASIC_REFLECT_VISIT_PRUNE;
    }
    if ((0 != rec->stop_after) && (rec->count >= rec->stop_after)) {
        return SHREWD_BASIC_REFLECT_VISIT_STOP;
    }
    return SHREWD_BASIC_REFLECT_VISIT_CONTINUE;
}

/* Index of the first matching event, or -1. */
static int visit_find(const visit_rec_t *rec,
                      shrewd_basic_reflect_visit_event_t event, const char *path)
{
    for (size_t i = 0; i < rec->count; ++i) {
        if ((event == rec->entries[i].event) && (0 == strcmp(rec->entries[i].path, path))) {
            return (int)i;
        }
    }
    return -1;
}

static void test_visit(void)
{
    person_t person;
    memset(&person, 0, sizeof(person));
    person.name = "Alice";
    person.tag[0] = 't';
    person.tag[1] = 'g';
    person.scores[0] = 1;
    person.scores[1] = 2;
    person.scores[2] = 3;
    person.home.x = 4;
    person.pair[1].y = 9;

    /* Full walk of person_t: every event in order. */
    visit_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit person",
                  shrewd_basic_reflect_visit(&person, &g_person_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    expect_true("visit person count", (size_t)35 == rec.count);
    expect_true("visit root enter",
                (0 == visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ENTER, ""))
                && (SHREWD_BASIC_REFLECT_VISIT_ENTER == rec.entries[0].event));
    expect_true("visit root exit",
                ((int)rec.count - 1
                 == visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_EXIT, "")));
    expect_true("visit leaf scalar", 0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "age"));
    expect_true("visit leaf string", 0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "name"));
    expect_true("visit nested order",
                (visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ENTER, "home")
                 < visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "home.y"))
                && (visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "home.y")
                    < visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_EXIT, "home")));
    expect_true("visit struct array order",
                (visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "pair")
                 < visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ENTER, "pair[1]"))
                && (visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "pair[1].y")
                    < visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_EXIT, "pair")));
    int tag_enter = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "tag");
    expect_true("visit array count",
                (0 <= tag_enter) && ((size_t)4 == rec.entries[tag_enter].count));
    expect_true("visit array elements",
                (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "tag[0]"))
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "tag[3]")));
    expect_true("visit no leaves under arrays missing",
                0 > visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "pair"));

    /* PRUNE at a struct: its subtree (and matching EXIT) is suppressed. */
    memset(&rec, 0, sizeof(rec));
    rec.prune_event = SHREWD_BASIC_REFLECT_VISIT_ENTER;
    rec.prune_path = "home";
    expect_eq_int("visit prune",
                  shrewd_basic_reflect_visit(&person, &g_person_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    expect_true("visit prune enter reported",
                0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ENTER, "home"));
    expect_true("visit prune subtree gone",
                (0 > visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "home.x"))
                && (0 > visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_EXIT, "home")));
    expect_true("visit prune siblings kept",
                (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "age"))
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "pair")));

    /* PRUNE at an array: elements and the matching ARRAY_EXIT are suppressed. */
    memset(&rec, 0, sizeof(rec));
    rec.prune_event = SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER;
    rec.prune_path = "scores";
    expect_eq_int("visit prune array",
                  shrewd_basic_reflect_visit(&person, &g_person_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    expect_true("visit prune array reported",
                0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "scores"));
    expect_true("visit prune array gone",
                (0 > visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "scores[0]"))
                && (0 > visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_EXIT, "scores")));

    /* STOP aborts at once - no further events at all. */
    memset(&rec, 0, sizeof(rec));
    rec.stop_after = 2;
    expect_eq_int("visit stop",
                  shrewd_basic_reflect_visit(&person, &g_person_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    expect_true("visit stop truncates", (size_t)2 == rec.count);
    expect_true("visit stop last leaf",
                (SHREWD_BASIC_REFLECT_VISIT_LEAF == rec.entries[1].event)
                && (0 == strcmp(rec.entries[1].path, "active")));

    /* NULL pointer slots, opaque targets and pointer-to-struct elements. */
    point_t team_point;
    int32_t counter = 5;
    const char *text = "hi";
    int32_t handle = 9;
    refs_t refs;
    memset(&refs, 0, sizeof(refs));
    memset(&team_point, 0, sizeof(team_point));
    refs.p_str = &text;
    refs.ctx = &handle;
    refs.counter = &counter;
    refs.team[0] = &team_point;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit refs",
                  shrewd_basic_reflect_visit(&refs, &g_refs_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    int at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "p_str");
    expect_true("visit ptr-to-string leaf",
                (0 <= at) && (0 == rec.entries[at].value_null));
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "ctx");
    expect_true("visit opaque leaf",
                (0 <= at) && (0 == rec.entries[at].value_null));
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "counter");
    expect_true("visit ptr-to-scalar leaf",
                (0 <= at) && (0 == rec.entries[at].value_null));
    expect_true("visit ptr array element struct",
                (visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "team")
                 < visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ENTER, "team[0]"))
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "team[0].y"))
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_EXIT, "team")));
    refs.counter = NULL;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit refs null",
                  shrewd_basic_reflect_visit(&refs, &g_refs_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "counter");
    expect_true("visit null pointer leaf",
                (0 <= at) && (0 != rec.entries[at].value_null));

    /* Cycles: a self-link reports CYCLE instead of expanding. */
    node_t loop;
    memset(&loop, 0, sizeof(loop));
    loop.data = 1;
    loop.next = &loop;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit cycle",
                  shrewd_basic_reflect_visit(&loop, &g_node_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_CYCLE, "next");
    expect_true("visit cycle reported", (0 <= at) && ((size_t)4 == rec.count));
    expect_true("visit cycle target",
                (0 <= at) && (0 == rec.entries[at].value_null));
    loop.next = NULL;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit null link",
                  shrewd_basic_reflect_visit(&loop, &g_node_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "next");
    expect_true("visit null struct link",
                (0 <= at) && (0 != rec.entries[at].value_null));

    /* Depth cap: a 20-long chain stops with a CYCLE event. */
    node_t chain[20];
    memset(chain, 0, sizeof(chain));
    for (size_t i = 0; i + 1 < 20; ++i) {
        chain[i].next = &chain[i + 1];
    }
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit depth cap",
                  shrewd_basic_reflect_visit(&chain[0], &g_node_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    int saw_cycle = 0;
    for (size_t i = 0; i < rec.count; ++i) {
        if (SHREWD_BASIC_REFLECT_VISIT_CYCLE == rec.entries[i].event) {
            saw_cycle = 1;
        }
    }
    expect_true("visit depth cap events",
                ((size_t)49 == rec.count) && (0 != saw_cycle));
    expect_true("visit depth cap shallow",
                0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ENTER, "next.next.next"));

    /* Dynamic arrays: ARRAY_ENTER carries the run-time count. */
    dyn_t dyn;
    point_t dyn_points[2];
    point_t *dyn_table[2];
    int32_t dyn_values[3];
    char *dyn_argv[3];
    memset(&dyn, 0, sizeof(dyn));
    memset(dyn_points, 0, sizeof(dyn_points));
    dyn_table[0] = &dyn_points[0];
    dyn_table[1] = &dyn_points[1];
    dyn_values[0] = 7;
    dyn_values[1] = 8;
    dyn_values[2] = 9;
    dyn_argv[0] = (char *)"ls";
    dyn_argv[1] = (char *)"-l";
    dyn_argv[2] = NULL;
    dyn.points = dyn_table;
    dyn.point_count = 2;
    dyn.values = dyn_values;
    dyn.value_count = 3;
    dyn.argv = dyn_argv;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit dyn",
                  shrewd_basic_reflect_visit(&dyn, &g_dyn_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "values");
    expect_true("visit darray count",
                (0 <= at) && ((size_t)3 == rec.entries[at].count)
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "values[2]")));
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "points");
    expect_true("visit ptr darray",
                (0 <= at) && ((size_t)2 == rec.entries[at].count)
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ENTER, "points[1]")));
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "argv");
    expect_true("visit sentinel count",
                (0 <= at) && ((size_t)2 == rec.entries[at].count)
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "argv[1]")));
    dyn_t dyn_empty;
    memset(&dyn_empty, 0, sizeof(dyn_empty));
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("visit dyn empty",
                  shrewd_basic_reflect_visit(&dyn_empty, &g_dyn_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "points");
    expect_true("visit null table leaf",
                (0 <= at) && (0 != rec.entries[at].value_null));

    /* Argument validation. */
    expect_eq_int("visit NULL object",
                  shrewd_basic_reflect_visit(NULL, &g_person_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("visit NULL type",
                  shrewd_basic_reflect_visit(&person, NULL, visit_collect, &rec),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("visit NULL callback",
                  shrewd_basic_reflect_visit(&person, &g_person_reflect, NULL, &rec),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("visit non-struct type",
                  shrewd_basic_reflect_visit(&person.age, &shrewd_basic_reflect_int32,
                                             visit_collect, &rec),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

/* --- diff/hash fixtures ------------------------------------------------ */

static int ops_equal_false(const void *a, const void *b, bool *out_equal)
{
    (void)a;
    (void)b;
    *out_equal = false;
    return SHREWD_BASIC_ERR_OK;
}

/* Type-level ops whose equal always says "different" (root mismatch). */
static const shrewd_basic_reflect_ops_t g_never_equal_ops = {
    .value_size = 0,
    .equal = ops_equal_false };

SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(g_never_equal_reflect, point_t, g_never_equal_ops,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

static int ops_hash_constant(const void *value, size_t *out_hash)
{
    (void)value;
    *out_hash = 42;
    return SHREWD_BASIC_ERR_OK;
}

/* Type-level ops whose hash ignores the value entirely. */
static const shrewd_basic_reflect_ops_t g_const_hash_ops = {
    .value_size = 0,
    .hash = ops_hash_constant };

SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(g_const_hash_reflect, point_t, g_const_hash_ops,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

static void test_diff(void)
{
    person_t a;
    person_t b;
    char path[64];
    memset(&a, 0, sizeof(a));
    b = a;

    /* Equal graphs: OK, no path reported. */
    expect_eq_int("diff equal",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_OK);

    /* The first difference's path, in path_get syntax. */
    a.home.y = 5;
    expect_eq_int("diff leaf",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff leaf path", 0 == strcmp(path, "home.y"));
    b.home.y = 5;

    a.pair[1].x = 8;
    expect_eq_int("diff array elem",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff array elem path", 0 == strcmp(path, "pair[1].x"));
    b.pair[1].x = 8;

    /* Field order decides which difference is "first". */
    a.age = 30;
    a.name = "Alice";
    b.age = 31;
    b.name = "Bob";
    expect_eq_int("diff first in order",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff first in order path", 0 == strcmp(path, "age"));
    a.age = 0;
    b.age = 0;

    /* Strings compare by content. */
    b.name = "Alice";
    expect_eq_int("diff strings same",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_OK);
    b.name = "Bob";
    expect_eq_int("diff string",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff string path", 0 == strcmp(path, "name"));
    b.name = "Alice";

    /* NULL vs non-NULL pointer target. */
    node_t left;
    node_t right;
    node_t target;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&target, 0, sizeof(target));
    left.next = &target;
    expect_eq_int("diff null pointer",
                  shrewd_basic_reflect_diff(&left, &right, &g_node_reflect,
                                            path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff null pointer path", 0 == strcmp(path, "next"));

    /* Dynamic arrays of different lengths report the field itself. */
    dyn_t d1;
    dyn_t d2;
    int32_t v_a[3] = {7, 8, 9};
    int32_t v_b[3] = {7, 80, 9};
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.values = v_a;
    d1.value_count = 3;
    d2.values = v_b;
    d2.value_count = 2;
    expect_eq_int("diff count mismatch",
                  shrewd_basic_reflect_diff(&d1, &d2, &g_dyn_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff count mismatch path", 0 == strcmp(path, "values"));
    d2.value_count = 3; /* same length now, element 1 differs */
    expect_eq_int("diff darray elem",
                  shrewd_basic_reflect_diff(&d1, &d2, &g_dyn_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff darray elem path", 0 == strcmp(path, "values[1]"));

    /* Root-level ops->equal mismatch reports "(root)". */
    point_t p1;
    point_t p2;
    memset(&p1, 0, sizeof(p1));
    p2 = p1;
    expect_eq_int("diff root ops",
                  shrewd_basic_reflect_diff(&p1, &p2, &g_never_equal_reflect,
                                            path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff root ops path", 0 == strcmp(path, "(root)"));

    /* No report buffer, and a truncated one stays NUL-terminated. */
    a.home.y = 9;
    expect_eq_int("diff without path",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, NULL, 0),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    char tiny[4] = {0};
    expect_eq_int("diff tiny buffer",
                  shrewd_basic_reflect_diff(&a, &b, &g_person_reflect, tiny, sizeof(tiny)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("diff tiny buffer terminated",
                (3 == strlen(tiny)) && (0 == strcmp(tiny, "hom")));
    a.home.y = 5;

    expect_eq_int("diff NULL a",
                  shrewd_basic_reflect_diff(NULL, &b, &g_person_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("diff NULL type",
                  shrewd_basic_reflect_diff(&a, &b, NULL, path, sizeof(path)),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_hash(void)
{
    person_t a;
    person_t b;
    char name[] = "Alice";
    size_t ha = 0;
    size_t hb = 0;
    memset(&a, 0, sizeof(a));
    a.age = 30;
    a.name = "Alice";
    a.precise = -0.0;
    a.home.x = 4;
    b = a;
    b.name = name; /* equal content, different pointer */

    /* Equal objects hash equal (the hash/equal contract). */
    bool same = false;
    expect_eq_int("hash equal objects",
                  shrewd_basic_reflect_hash(&a, &g_person_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("hash equal objects b",
                  shrewd_basic_reflect_hash(&b, &g_person_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hash equal objects same",
                (0 != ha) && (ha == hb));
    expect_eq_int("hash contract equal",
                  shrewd_basic_reflect_equal(&a, &b, &g_person_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hash contract equal yes", same);

    /* -0.0 and +0.0 compare equal, so they must fold alike. */
    b.precise = 0.0;
    expect_eq_int("hash signed zero",
                  shrewd_basic_reflect_hash(&b, &g_person_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hash signed zero same", ha == hb);

    /* Different content hashes differently. */
    b.precise = -0.0;
    b.age = 31;
    expect_eq_int("hash different",
                  shrewd_basic_reflect_hash(&b, &g_person_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hash different differs", ha != hb);

    /* Deep and dynamic content: equal graphs hash equal. */
    dyn_t d1;
    dyn_t d2;
    int32_t vals[3] = {7, 8, 9};
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    d1.values = vals;
    d1.value_count = 3;
    d2.values = vals;
    d2.value_count = 3;
    expect_eq_int("hash dyn a",
                  shrewd_basic_reflect_hash(&d1, &g_dyn_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("hash dyn b",
                  shrewd_basic_reflect_hash(&d2, &g_dyn_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hash dyn same", ha == hb);

    /* Cyclic graphs hash deterministically (back-edges fold as a marker). */
    node_t loop_a;
    node_t loop_b;
    memset(&loop_a, 0, sizeof(loop_a));
    memset(&loop_b, 0, sizeof(loop_b));
    loop_a.next = &loop_a;
    loop_b.next = &loop_b;
    expect_eq_int("hash cycle a",
                  shrewd_basic_reflect_hash(&loop_a, &g_node_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("hash cycle b",
                  shrewd_basic_reflect_hash(&loop_b, &g_node_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hash cycle same", ha == hb);

    /* ops->hash overrides the metadata-driven fold. */
    point_t p1;
    point_t p2;
    memset(&p1, 0, sizeof(p1));
    memset(&p2, 0, sizeof(p2));
    p2.x = 999;
    p2.y = -7;
    expect_eq_int("hash ops a",
                  shrewd_basic_reflect_hash(&p1, &g_const_hash_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("hash ops b",
                  shrewd_basic_reflect_hash(&p2, &g_const_hash_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("hash ops override", ha == hb);

    expect_eq_int("hash NULL object",
                  shrewd_basic_reflect_hash(NULL, &g_person_reflect, &ha),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("hash NULL out",
                  shrewd_basic_reflect_hash(&a, &g_person_reflect, NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

/* --- string buffer fixtures -------------------------------------------- */

typedef struct {
    char title[8];
    int32_t id;
} text_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_text_reflect, text_t,
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(text_t, title),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(text_t, id));

static void test_stringbuf(void)
{
    /* Descriptor shape: an inline char array with the string-buffer flag. */
    const shrewd_basic_reflect_field_t *field =
        find_or_fail("stringbuf", &g_text_reflect, "title");
    if (NULL == field) {
        return;
    }
    expect_true("stringbuf shape",
                (&shrewd_basic_reflect_char == field->sb_refl_type)
                && ((size_t)8 == field->sb_refl_count)
                && ((size_t)1 == field->sb_refl_stride)
                && ((uint32_t)(SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                               | SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)
                    == field->sb_refl_flags));

    text_t a;
    text_t b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memcpy(a.title, "hi", 3);
    memcpy(b.title, "hi", 3);
    b.title[3] = 'x'; /* garbage after the NUL: ignored */
    b.title[4] = 'y';
    a.id = 7;
    b.id = 7;

    /* dump renders it as one quoted, escaped string. */
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("stringbuf find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    FILE *stream = tmpfile();
    expect_true("stringbuf tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("stringbuf dump",
                  shrewd_basic_reflect_dump(&g_text_reflect, &a, "text"),
                  SHREWD_BASIC_ERR_OK);
    char buf[4096] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("stringbuf dump quoted", NULL != strstr(buf, "title = \"hi\""));
    expect_true("stringbuf dump not array", NULL == strstr(buf, "[ 'h'"));

    /* Escapes and an unterminated buffer. */
    memcpy(a.title, "a\"b\nc", 6);
    a.title[6] = '\0';
    expect_eq_int("stringbuf dump escapes",
                  shrewd_basic_reflect_dump(&g_text_reflect, &a, "text"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("stringbuf dump escaped",
                NULL != strstr(buf, "title = \"a\\\"b\\nc\""));
    memcpy(a.title, "abcdefgh", 8); /* no NUL at all */
    expect_eq_int("stringbuf dump unterminated",
                  shrewd_basic_reflect_dump(&g_text_reflect, &a, "text"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("stringbuf dump all bytes",
                NULL != strstr(buf, "title = \"abcdefgh\""));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);

    /* equal/diff/hash work on content up to the NUL. */
    memcpy(a.title, "hi", 3);
    a.title[3] = 'z';
    bool same = false;
    expect_eq_int("stringbuf equal",
                  shrewd_basic_reflect_equal(&a, &b, &g_text_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("stringbuf equal yes", same);
    char path[64];
    expect_eq_int("stringbuf diff",
                  shrewd_basic_reflect_diff(&a, &b, &g_text_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_OK);
    size_t ha = 0;
    size_t hb = 0;
    expect_eq_int("stringbuf hash a",
                  shrewd_basic_reflect_hash(&a, &g_text_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("stringbuf hash b",
                  shrewd_basic_reflect_hash(&b, &g_text_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("stringbuf hash same", ha == hb);

    memcpy(b.title, "hello", 6);
    expect_eq_int("stringbuf diff content",
                  shrewd_basic_reflect_diff(&a, &b, &g_text_reflect, path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("stringbuf diff content path", 0 == strcmp(path, "title"));
    expect_eq_int("stringbuf equal content",
                  shrewd_basic_reflect_equal(&a, &b, &g_text_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("stringbuf equal content no", !same);

    /* visit reports one LEAF, not an element list. */
    memset(&a, 0, sizeof(a));
    memcpy(a.title, "hi", 3);
    visit_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("stringbuf visit",
                  shrewd_basic_reflect_visit(&a, &g_text_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    int at = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "title");
    expect_true("stringbuf visit leaf",
                (0 <= at) && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "id"))
                && (0 > visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "title")));

    /* Storage is still bytes: get/set move them raw, paths index chars. */
    char raw[8];
    memcpy(a.title, "abcdef", 7);
    expect_eq_int("stringbuf field_get",
                  shrewd_basic_reflect_field_get(&a, field, raw, sizeof(raw)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("stringbuf raw bytes", 0 == memcmp(raw, "abcdef\0\0", 8));
    char ch = 'Z';
    expect_eq_int("stringbuf path_set",
                  shrewd_basic_reflect_path_set(&a, &g_text_reflect, "title[2]", &ch,
                                                sizeof(ch)),
                  SHREWD_BASIC_ERR_OK);
    char got = '\0';
    expect_eq_int("stringbuf path_get",
                  shrewd_basic_reflect_path_get(&a, &g_text_reflect, "title[2]", &got,
                                                sizeof(got)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("stringbuf path index", 'Z' == got);
}

/* --- lookup index fixtures --------------------------------------------- */

/*
 * More fields than the find_field lookup index holds (64): exercises the
 * linear-scan fallback for oversized types.
 */
typedef struct {
    int32_t f0;
    int32_t f1;
    int32_t f2;
    int32_t f3;
    int32_t f4;
    int32_t f5;
    int32_t f6;
    int32_t f7;
    int32_t f8;
    int32_t f9;
    int32_t f10;
    int32_t f11;
    int32_t f12;
    int32_t f13;
    int32_t f14;
    int32_t f15;
    int32_t f16;
    int32_t f17;
    int32_t f18;
    int32_t f19;
    int32_t f20;
    int32_t f21;
    int32_t f22;
    int32_t f23;
    int32_t f24;
    int32_t f25;
    int32_t f26;
    int32_t f27;
    int32_t f28;
    int32_t f29;
    int32_t f30;
    int32_t f31;
    int32_t f32;
    int32_t f33;
    int32_t f34;
    int32_t f35;
    int32_t f36;
    int32_t f37;
    int32_t f38;
    int32_t f39;
    int32_t f40;
    int32_t f41;
    int32_t f42;
    int32_t f43;
    int32_t f44;
    int32_t f45;
    int32_t f46;
    int32_t f47;
    int32_t f48;
    int32_t f49;
    int32_t f50;
    int32_t f51;
    int32_t f52;
    int32_t f53;
    int32_t f54;
    int32_t f55;
    int32_t f56;
    int32_t f57;
    int32_t f58;
    int32_t f59;
    int32_t f60;
    int32_t f61;
    int32_t f62;
    int32_t f63;
    int32_t f64;
    int32_t f65;
    int32_t f66;
    int32_t f67;
    int32_t f68;
    int32_t f69;
} many_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_many_reflect, many_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f0),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f1),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f2),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f3),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f4),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f5),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f6),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f7),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f8),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f9),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f10),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f11),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f12),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f13),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f14),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f15),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f16),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f17),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f18),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f19),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f20),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f21),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f22),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f23),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f24),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f25),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f26),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f27),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f28),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f29),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f30),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f31),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f33),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f34),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f35),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f36),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f37),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f38),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f39),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f40),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f41),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f42),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f43),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f44),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f45),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f46),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f47),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f48),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f49),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f50),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f51),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f52),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f53),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f54),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f55),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f56),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f57),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f58),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f59),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f60),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f61),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f62),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f63),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f64),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f65),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f66),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f67),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f68),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(many_t, f69));

static void test_lookup_index(void)
{
    /* The sorted index is built lazily: every person field must resolve
       to the very same descriptor as the raw table. */
    for (size_t i = 0; i < g_person_reflect.sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = NULL;
        const shrewd_basic_reflect_field_t *want =
                &g_person_reflect.sb_refl_fields[i];
        expect_eq_int("lookup hit",
                      shrewd_basic_reflect_type_find_field(
                              &g_person_reflect, want->sb_refl_name, &field),
                      SHREWD_BASIC_ERR_OK);
        expect_true("lookup hit pointer", field == want);
    }
    const shrewd_basic_reflect_field_t *field = NULL;
    expect_eq_int("lookup miss",
                  shrewd_basic_reflect_type_find_field(&g_person_reflect, "nope",
                                                       &field),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    /* More types than lookup slots: results stay correct across
       round-robin eviction and rebuild. */
    const shrewd_basic_reflect_type_t *types[] = {
        &g_point_reflect, &g_node_reflect, &g_refs_reflect, &g_wide_reflect,
        &g_huge_reflect, &g_dyn_reflect, &g_flex_reflect, &g_zflex_reflect,
        &g_text_reflect, &g_ops_refs_reflect, &g_graph_reflect };
    for (size_t i = 0; i < (sizeof(types) / sizeof(types[0])); ++i) {
        const shrewd_basic_reflect_field_t *first =
                shrewd_basic_reflect_type_field_at(types[i], 0);
        expect_eq_int("lookup churn",
                      shrewd_basic_reflect_type_find_field(
                              types[i], first->sb_refl_name, &field),
                      SHREWD_BASIC_ERR_OK);
        expect_true("lookup churn pointer", field == first);
    }
    /* person_t may have been evicted by now: it must still resolve. */
    expect_eq_int("lookup rebuilt",
                  shrewd_basic_reflect_type_find_field(&g_person_reflect, "age",
                                                       &field),
                  SHREWD_BASIC_ERR_OK);
    expect_true("lookup rebuilt pointer",
                field == &g_person_reflect.sb_refl_fields[3]);

    /* Oversized type (70 fields): the linear fallback finds every one. */
    expect_true("lookup oversized count",
                (size_t)70 == g_many_reflect.sb_refl_field_count);
    for (size_t i = 0; i < g_many_reflect.sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *want = &g_many_reflect.sb_refl_fields[i];
        expect_eq_int("lookup fallback",
                      shrewd_basic_reflect_type_find_field(
                              &g_many_reflect, want->sb_refl_name, &field),
                      SHREWD_BASIC_ERR_OK);
        expect_true("lookup fallback pointer", field == want);
    }
    expect_eq_int("lookup fallback miss",
                  shrewd_basic_reflect_type_find_field(&g_many_reflect, "nope",
                                                       &field),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
}

/* --- owned string fixtures --------------------------------------------- */

typedef struct {
    char *title;
    char *shared;
    const char *borrowed; /* plain STRING: aliased on copy, never freed */
} owned_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_owned_reflect, owned_t,
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(owned_t, title),
    SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(owned_t, shared),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(owned_t, borrowed));

static void test_owned_strings(void)
{
    /* Descriptor shape. */
    const shrewd_basic_reflect_field_t *field =
        find_or_fail("owned", &g_owned_reflect, "title");
    if (NULL != field) {
        expect_true("owned shape",
                    (&shrewd_basic_reflect_string == field->sb_refl_type)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED
                        == field->sb_refl_flags));
    }

    /* A heap-owned graph: two fields share one buffer, one is borrowed. */
    void *block = NULL;
    char *title = NULL;
    char *shared = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, 6)) {
        return;
    }
    title = block;
    memcpy(title, "hello", 6);
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, 4)) {
        (void)shrewd_basic_platform_free((void **)&title);
        return;
    }
    shared = block;
    memcpy(shared, "dup", 4);
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, sizeof(owned_t))) {
        (void)shrewd_basic_platform_free((void **)&title);
        (void)shrewd_basic_platform_free((void **)&shared);
        return;
    }
    owned_t *src = block;
    src->title = title;
    src->shared = shared;
    src->borrowed = "static literal";

    owned_t *twin = NULL;
    expect_eq_int("owned copy",
                  shrewd_basic_reflect_copy((void **)&twin, src, &g_owned_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_true("owned clone deep",
                (NULL != twin) && (twin->title != src->title)
                && (0 == strcmp(twin->title, "hello")));
    expect_true("owned clone shared",
                (twin->shared != src->shared)   /* own copy of the buffer ... */
                && (0 == strcmp(twin->shared, "dup")));
    expect_true("owned clone borrowed", twin->borrowed == src->borrowed);

    bool same = false;
    expect_eq_int("owned equal",
                  shrewd_basic_reflect_equal(src, twin, &g_owned_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("owned equal yes", same);
    size_t ha = 0;
    size_t hb = 0;
    expect_eq_int("owned hash src",
                  shrewd_basic_reflect_hash(src, &g_owned_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("owned hash clone",
                  shrewd_basic_reflect_hash(twin, &g_owned_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("owned hash same", ha == hb);

    /* NULL owned strings stay NULL through the clone. */
    (void)shrewd_basic_platform_free((void **)&title);
    src->title = NULL;
    owned_t *null_clone = NULL;
    expect_eq_int("owned copy null",
                  shrewd_basic_reflect_copy((void **)&null_clone, src,
                                            &g_owned_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_true("owned null stays null",
                (NULL != null_clone) && (NULL == null_clone->title));
    expect_eq_int("owned destroy null clone",
                  shrewd_basic_reflect_destroy(null_clone, &g_owned_reflect),
                  SHREWD_BASIC_ERR_OK);

    /* Destroy frees cloned content exactly once (valgrind asserts it). */
    expect_eq_int("owned destroy clone",
                  shrewd_basic_reflect_destroy(twin, &g_owned_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("owned destroy src",
                  shrewd_basic_reflect_destroy(src, &g_owned_reflect),
                  SHREWD_BASIC_ERR_OK);
}

/* --- bit-group fixtures ------------------------------------------------ */

/* A register-style unit: three named groups share one uint32 word. */
typedef struct {
    uint32_t cfg;
    int32_t value;
} reg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_reg_reflect, reg_t,
    SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, ready, 0, 1),
    SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, mode, 1, 3),
    SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, divider, 8, 16),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(reg_t, value));

static void test_bits(void)
{
    /* Descriptor shape: unit offsets plus bit position/width. */
    const shrewd_basic_reflect_field_t *field =
        find_or_fail("bits", &g_reg_reflect, "mode");
    if (NULL == field) {
        return;
    }
    expect_true("bits shape",
                (SHREWD_BASIC_REFLECT_FIELD_F_BITS == field->sb_refl_flags)
                && (offsetof(reg_t, cfg) == field->sb_refl_offset)
                && (sizeof(uint32_t) == field->sb_refl_size)
                && ((size_t)1 == field->sb_refl_bit_offset)
                && ((size_t)3 == field->sb_refl_bit_width)
                && (&shrewd_basic_reflect_uint32 == field->sb_refl_type));

    /* Extract: unused unit bits are invisible to get. Garbage lives in
       bits 4-7 and 24-31 (the groups cover bits 0 and 1-3 and 8-23). */
    reg_t reg;
    memset(&reg, 0, sizeof(reg));
    reg.cfg = (1u << 0) | (5u << 1) | (100u << 8) | 0xf0u | 0xab000000u;
    reg.value = 7;
    uint32_t word = 0;
    field = find_or_fail("bits", &g_reg_reflect, "ready");
    if (NULL != field) {
        expect_eq_int("bits get ready",
                      shrewd_basic_reflect_field_get(&reg, field, &word, sizeof(word)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("bits ready value", 1u == word);
    }
    field = find_or_fail("bits", &g_reg_reflect, "mode");
    if (NULL != field) {
        expect_eq_int("bits get mode",
                      shrewd_basic_reflect_field_get(&reg, field, &word, sizeof(word)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("bits mode value", 5u == word);
    }
    field = find_or_fail("bits", &g_reg_reflect, "divider");
    if (NULL != field) {
        expect_eq_int("bits get divider",
                      shrewd_basic_reflect_field_get(&reg, field, &word, sizeof(word)),
                      SHREWD_BASIC_ERR_OK);
        expect_true("bits divider value", 100u == word);
    }

    /* Insert: only the group's bits change (read-modify-write). */
    word = 3;
    field = find_or_fail("bits", &g_reg_reflect, "mode");
    if (NULL != field) {
        expect_eq_int("bits set mode",
                      shrewd_basic_reflect_field_set(&reg, field, &word, sizeof(word)),
                      SHREWD_BASIC_ERR_OK);
    }
    expect_true("bits set only group",
                reg.cfg == ((1u << 0) | (3u << 1) | (100u << 8) | 0xf0u | 0xab000000u));

    /* Paths resolve to groups too. */
    word = 0;
    expect_eq_int("bits path_get",
                  shrewd_basic_reflect_path_get(&reg, &g_reg_reflect, "divider",
                                                &word, sizeof(word)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("bits path value", 100u == word);
    word = 0;
    expect_eq_int("bits path_set",
                  shrewd_basic_reflect_path_set(&reg, &g_reg_reflect, "ready",
                                                &word, sizeof(word)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("bits path wrote", 0u == (reg.cfg & 1u));

    /* equal/hash use the groups only: garbage in unused bits is equal. */
    reg_t other;
    memset(&other, 0, sizeof(other));
    other.cfg = (0u << 0) | (3u << 1) | (100u << 8) | 0x30u | 0x07000000u;
    other.value = 7;
    bool same = false;
    expect_eq_int("bits equal",
                  shrewd_basic_reflect_equal(&reg, &other, &g_reg_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("bits equal yes", same);
    size_t ha = 0;
    size_t hb = 0;
    expect_eq_int("bits hash a",
                  shrewd_basic_reflect_hash(&reg, &g_reg_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("bits hash b",
                  shrewd_basic_reflect_hash(&other, &g_reg_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("bits hash same", ha == hb);

    /* A group difference is a first-class difference. */
    other.cfg |= (1u << 8); /* divider 100 -> 101 */
    char path[64];
    expect_eq_int("bits diff",
                  shrewd_basic_reflect_diff(&reg, &other, &g_reg_reflect,
                                            path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("bits diff path", 0 == strcmp(path, "divider"));

    /* dump shows the extracted numbers, not the whole unit. */
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("bits find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    FILE *stream = tmpfile();
    expect_true("bits tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    reg.cfg = (1u << 0) | (3u << 1) | (100u << 8) | 0xf0u | 0xab000000u;
    expect_eq_int("bits dump",
                  shrewd_basic_reflect_dump(&g_reg_reflect, &reg, "reg"),
                  SHREWD_BASIC_ERR_OK);
    char buf[4096] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("bits dump groups",
                (NULL != strstr(buf, "  ready = 1"))
                && (NULL != strstr(buf, "  mode = 3"))
                && (NULL != strstr(buf, "  divider = 100")));
    expect_true("bits dump hides unit", NULL == strstr(buf, "2868903936")); /* 0xab000000 */
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);

    /* visit reports each group as one LEAF. */
    visit_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("bits visit",
                  shrewd_basic_reflect_visit(&reg, &g_reg_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    expect_true("bits visit leaves",
                (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "ready"))
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "mode"))
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "divider")));

    /* copy carries the unit bytes; equal stays group-based. */
    reg_t *clone = NULL;
    expect_eq_int("bits copy",
                  shrewd_basic_reflect_copy((void **)&clone, &reg, &g_reg_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_true("bits clone unit",
                (NULL != clone) && (clone->cfg == reg.cfg) && (clone->value == reg.value));
    expect_eq_int("bits destroy clone",
                  shrewd_basic_reflect_destroy(clone, &g_reg_reflect),
                  SHREWD_BASIC_ERR_OK);
}

/* --- multi-dimension array fixtures ------------------------------------ */

/* int32_t m[2][3]: a matrix of 2 rows x 3 columns. */
typedef struct {
    int32_t m[2][3];
    int32_t id;
} matrix_t;

SHREWD_BASIC_REFLECT_ARRAY_DEFINE(g_row3_reflect, shrewd_basic_reflect_int32,
                                  int32_t, 3);
SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_matrix_reflect, matrix_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(matrix_t, m, g_row3_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(matrix_t, id));

/* int32_t cube[2][2][2]: rows nest (row of rows). */
typedef struct {
    int32_t cube[2][2][2];
} cube_t;

SHREWD_BASIC_REFLECT_ARRAY_DEFINE(g_row2_reflect, shrewd_basic_reflect_int32,
                                  int32_t, 2);
SHREWD_BASIC_REFLECT_ARRAY_DEFINE(g_plane22_reflect, g_row2_reflect,
                                  int32_t[2], 2);
SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_cube_reflect, cube_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(cube_t, cube, g_plane22_reflect));

/* point_t grid[2][2]: a matrix of struct values. */
typedef struct {
    point_t grid[2][2];
} grid_t;

SHREWD_BASIC_REFLECT_ARRAY_DEFINE(g_pair2_reflect, g_point_reflect, point_t, 2);
SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_grid_reflect, grid_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(grid_t, grid, g_pair2_reflect));

static void test_multidim(void)
{
    /* Row descriptor shape. */
    expect_true("multidim row kind",
                SHREWD_BASIC_REFLECT_KIND_ARRAY == g_row3_reflect.sb_refl_kind);
    expect_true("multidim row shape",
                (&shrewd_basic_reflect_int32 == g_row3_reflect.sb_refl_elem)
                && ((size_t)3 == g_row3_reflect.sb_refl_elem_count)
                && (sizeof(int32_t) == g_row3_reflect.sb_refl_elem_stride)
                && ((3 * sizeof(int32_t)) == g_row3_reflect.sb_refl_size));
    expect_true("multidim kind name",
                0 == strcmp(shrewd_basic_reflect_kind_name(
                                    g_row3_reflect.sb_refl_kind), "array"));

    matrix_t mx;
    memset(&mx, 0, sizeof(mx));
    for (size_t r = 0; r < 2; ++r) {
        for (size_t c = 0; c < 3; ++c) {
            mx.m[r][c] = (int32_t)((r * 3) + c) + 1; /* 1..6 */
        }
    }
    mx.id = 7;

    /* Chained paths: one index per row level. */
    int32_t at = 0;
    expect_eq_int("multidim path_get",
                  shrewd_basic_reflect_path_get(&mx, &g_matrix_reflect, "m[1][2]",
                                                &at, sizeof(at)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("multidim path value", 6 == at);
    at = 50;
    expect_eq_int("multidim path_set",
                  shrewd_basic_reflect_path_set(&mx, &g_matrix_reflect, "m[0][1]",
                                                &at, sizeof(at)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("multidim path wrote", 50 == mx.m[0][1]);
    at = 0;
    expect_eq_int("multidim path row",
                  shrewd_basic_reflect_path_get(&mx, &g_matrix_reflect, "m[1]",
                                                &at, sizeof(at)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_eq_int("multidim path row size",
                  shrewd_basic_reflect_path_get(&mx, &g_matrix_reflect, "m[1]",
                                                &mx.m[1][0], sizeof(mx.m[1])),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("multidim path row range",
                  shrewd_basic_reflect_path_get(&mx, &g_matrix_reflect, "m[2][0]",
                                                &at, sizeof(at)),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("multidim path col range",
                  shrewd_basic_reflect_path_get(&mx, &g_matrix_reflect, "m[0][3]",
                                                &at, sizeof(at)),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    /* resolve is static all the way (no pointer segments). */
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t offset = 0;
    size_t size = 0;
    expect_eq_int("multidim resolve",
                  shrewd_basic_reflect_type_resolve(&g_matrix_reflect, "m[1][2]",
                                                    &field, &offset, &size),
                  SHREWD_BASIC_ERR_OK);
    expect_true("multidim resolve shape",
                (offsetof(matrix_t, m) + (1 * 3 * sizeof(int32_t))
                 + (2 * sizeof(int32_t))) == offset
                && (sizeof(int32_t) == size));

    /* equal/diff/hash. */
    matrix_t twin;
    twin = mx;
    bool same = false;
    expect_eq_int("multidim equal",
                  shrewd_basic_reflect_equal(&mx, &twin, &g_matrix_reflect, &same),
                  SHREWD_BASIC_ERR_OK);
    expect_true("multidim equal yes", same);
    size_t ha = 0;
    size_t hb = 0;
    expect_eq_int("multidim hash a",
                  shrewd_basic_reflect_hash(&mx, &g_matrix_reflect, &ha),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("multidim hash b",
                  shrewd_basic_reflect_hash(&twin, &g_matrix_reflect, &hb),
                  SHREWD_BASIC_ERR_OK);
    expect_true("multidim hash same", ha == hb);
    twin.m[1][0] = 99;
    char path[64];
    expect_eq_int("multidim diff",
                  shrewd_basic_reflect_diff(&mx, &twin, &g_matrix_reflect,
                                            path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("multidim diff path", 0 == strcmp(path, "m[1][0]"));
    twin.m[1][0] = mx.m[1][0];

    /* dump renders nested rows inline. */
    shrewd_basic_log_module_t *module = NULL;
    expect_eq_int("multidim find module",
                  shrewd_basic_log_module_find("reflect", &module),
                  SHREWD_BASIC_ERR_OK);
    FILE *stream = tmpfile();
    expect_true("multidim tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("multidim dump",
                  shrewd_basic_reflect_dump(&g_matrix_reflect, &mx, "matrix"),
                  SHREWD_BASIC_ERR_OK);
    char buf[4096] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("multidim dump rows",
                (NULL != strstr(buf, "m = [ [ 1, 50, 3 ], [ 4, 5, 6 ] ]"))
                && (NULL != strstr(buf, "id = 7")));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);

    /* 3D: rows nest. */
    cube_t cube;
    memset(&cube, 0, sizeof(cube));
    cube.cube[1][0][1] = 42;
    at = 0;
    expect_eq_int("multidim 3d get",
                  shrewd_basic_reflect_path_get(&cube, &g_cube_reflect,
                                                "cube[1][0][1]", &at, sizeof(at)),
                  SHREWD_BASIC_ERR_OK);
    expect_true("multidim 3d value", 42 == at);
    stream = tmpfile();
    expect_true("multidim 3d tmpfile", NULL != stream);
    if (NULL == stream) {
        shrewd_basic_log_module_set_stream(module, stderr);
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("multidim 3d dump",
                  shrewd_basic_reflect_dump(&g_cube_reflect, &cube, "cube"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("multidim 3d dump nests",
                NULL != strstr(buf, "[ [ [ 0, 0 ], [ 0, 0 ] ], [ [ 0, 42 ], [ 0, 0 ] ] ]"));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);

    /* Struct rows: inline blocks per element. */
    grid_t grid;
    memset(&grid, 0, sizeof(grid));
    grid.grid[1][0].x = 3;
    grid.grid[1][0].y = 4;
    grid.grid[0][1].x = 8;
    stream = tmpfile();
    expect_true("multidim grid tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(module, stream);
    expect_eq_int("multidim grid dump",
                  shrewd_basic_reflect_dump(&g_grid_reflect, &grid, "grid"),
                  SHREWD_BASIC_ERR_OK);
    memset(buf, 0, sizeof(buf));
    slurp(stream, buf, sizeof(buf));
    expect_true("multidim grid dump inline",
                NULL != strstr(buf, "grid = [ [ { x = 0, y = 0 }, { x = 8, y = 0 } ],"
                                    " [ { x = 3, y = 4 }, { x = 0, y = 0 } ] ]"));
    shrewd_basic_log_module_set_stream(module, stderr);
    fclose(stream);
    grid_t grid_twin;
    grid_twin = grid;
    grid_twin.grid[1][0].y = 40;
    expect_eq_int("multidim grid diff",
                  shrewd_basic_reflect_diff(&grid, &grid_twin, &g_grid_reflect,
                                            path, sizeof(path)),
                  SHREWD_BASIC_ERR_REFLECT_MISMATCH);
    expect_true("multidim grid diff path", 0 == strcmp(path, "grid[1][0].y"));

    /* visit: nested ARRAY_ENTER per row level. */
    visit_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    expect_eq_int("multidim visit",
                  shrewd_basic_reflect_visit(&mx, &g_matrix_reflect, visit_collect, &rec),
                  SHREWD_BASIC_ERR_OK);
    int at_idx = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "m");
    expect_true("multidim visit outer",
                (0 <= at_idx) && ((size_t)2 == rec.entries[at_idx].count));
    at_idx = visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "m[1]");
    expect_true("multidim visit inner",
                (0 <= at_idx) && ((size_t)3 == rec.entries[at_idx].count)
                && (0 <= visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_LEAF, "m[1][2]")));
    expect_true("multidim visit rows bracketed",
                visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_EXIT, "m[0]")
                < visit_find(&rec, SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, "m[1]"));

    /* copy clones matrix bytes; equal stays green. */
    matrix_t *clone = NULL;
    expect_eq_int("multidim copy",
                  shrewd_basic_reflect_copy((void **)&clone, &mx, &g_matrix_reflect),
                  SHREWD_BASIC_ERR_OK);
    expect_true("multidim clone equal",
                (NULL != clone) && (0 == memcmp(clone->m, mx.m, sizeof(mx.m)))
                && (clone->id == mx.id));
    expect_eq_int("multidim destroy",
                  shrewd_basic_reflect_destroy(clone, &g_matrix_reflect),
                  SHREWD_BASIC_ERR_OK);
}

int main(void)
{
    test_describe();
    test_find_field();
    test_lookup_index();
    test_get_set_scalars();
    test_get_set_string();
    test_nested();
    test_array();
    test_pointer_fields();
    test_enum();
    test_ops();
    test_dump();
    test_dump_pointers();
    test_dump_body_cap();
    test_paths();
    test_visit();
    test_diff();
    test_hash();
    test_stringbuf();
    test_owned_strings();
    test_bits();
    test_multidim();
    test_graph_ops();
    test_equal();
    test_dynamic_arrays();
    test_flex();
    test_logging_policy();
    test_cross_tu();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

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
} ops_refs_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_ops_refs_reflect, ops_refs_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR(ops_refs_t, type_ops_slot, g_handle_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR_OPS(ops_refs_t, field_ops_slot, g_handle_reflect,
                                       g_override_ops),
    SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY_OPS(ops_refs_t, many, g_handle_reflect,
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

static const shrewd_basic_reflect_ops_t g_handle_ops = {
    .value_size = sizeof(int32_t),
    .get = NULL,
    .set = NULL,
    .check = ops_handle_check,
    .copy = ops_handle_copy,
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
                  SHREWD_BASIC_ERR_ARG_INVALID);

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
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

int main(void)
{
    test_describe();
    test_find_field();
    test_get_set_scalars();
    test_get_set_string();
    test_nested();
    test_array();
    test_pointer_fields();
    test_enum();
    test_ops();
    test_dump();
    test_dump_pointers();
    test_paths();
    test_graph_ops();
    test_logging_policy();
    test_cross_tu();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

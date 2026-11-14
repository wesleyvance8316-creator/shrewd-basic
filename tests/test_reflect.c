#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/reflect/reflect.h"

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
} person_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_point_reflect, point_t,
    SHREWD_BASIC_REFLECT_FIELD(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD(point_t, y));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_person_reflect, person_t,
    SHREWD_BASIC_REFLECT_FIELD(person_t, active),
    SHREWD_BASIC_REFLECT_FIELD(person_t, grade),
    SHREWD_BASIC_REFLECT_FIELD(person_t, small),
    SHREWD_BASIC_REFLECT_FIELD(person_t, age),
    SHREWD_BASIC_REFLECT_FIELD(person_t, big),
    SHREWD_BASIC_REFLECT_FIELD(person_t, ratio),
    SHREWD_BASIC_REFLECT_FIELD(person_t, precise),
    SHREWD_BASIC_REFLECT_FIELD(person_t, name),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, tag, shrewd_basic_reflect_char),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, scores, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(person_t, home, g_point_reflect));

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
    expect_true("describe type name", 0 == strcmp(g_person_reflect.name, "person_t"));
    expect_true("describe type kind",
                SHREWD_BASIC_REFLECT_KIND_STRUCT == g_person_reflect.kind);
    expect_true("describe type size", sizeof(person_t) == g_person_reflect.size);
    expect_true("describe field count", (size_t)11 == g_person_reflect.field_count);
    expect_true("describe fields table", NULL != g_person_reflect.fields);
    expect_true("describe primitive has no fields",
                (0 == shrewd_basic_reflect_int32.field_count)
                && (NULL == shrewd_basic_reflect_int32.fields));

    const shrewd_basic_reflect_field_t *f = find_or_fail("describe", &g_person_reflect, "age");
    if (NULL != f) {
        expect_true("describe age type", &shrewd_basic_reflect_int32 == f->type);
        expect_true("describe age offset", offsetof(person_t, age) == f->offset);
        expect_true("describe age size", sizeof(int32_t) == f->size);
        expect_true("describe age count", (size_t)1 == f->count);
        expect_true("describe age flags", (uint32_t)0 == f->flags);
    }
    f = find_or_fail("describe", &g_person_reflect, "name");
    if (NULL != f) {
        expect_true("describe name type", &shrewd_basic_reflect_string == f->type);
        expect_true("describe name size", sizeof(const char *) == f->size);
    }
    f = find_or_fail("describe", &g_person_reflect, "tag");
    if (NULL != f) {
        expect_true("describe tag type", &shrewd_basic_reflect_char == f->type);
        expect_true("describe tag array shape",
                    ((size_t)4 == f->count) && ((size_t)4 == f->size)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY == f->flags));
    }
    f = find_or_fail("describe", &g_person_reflect, "scores");
    if (NULL != f) {
        expect_true("describe scores type", &shrewd_basic_reflect_int32 == f->type);
        expect_true("describe scores array shape",
                    ((size_t)3 == f->count)
                    && (3 * sizeof(int32_t) == f->size)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY == f->flags));
    }
    f = find_or_fail("describe", &g_person_reflect, "home");
    if (NULL != f) {
        expect_true("describe home type", &g_point_reflect == f->type);
        expect_true("describe home shape",
                    ((size_t)1 == f->count) && ((uint32_t)0 == f->flags));
    }
    f = find_or_fail("describe", &g_person_reflect, "active");
    if (NULL != f) {
        expect_true("describe active type", &shrewd_basic_reflect_bool == f->type);
    }
    f = find_or_fail("describe", &g_person_reflect, "big");
    if (NULL != f) {
        expect_true("describe big type", &shrewd_basic_reflect_uint64 == f->type);
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
    expect_true("find_field hit pointer", field == &g_person_reflect.fields[3]);
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
    expect_true("nested type", &g_point_reflect == home->type);

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

int main(void)
{
    test_describe();
    test_find_field();
    test_get_set_scalars();
    test_get_set_string();
    test_nested();
    test_array();
    test_logging_policy();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

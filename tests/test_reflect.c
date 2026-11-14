#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/log/log.h"
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
    SHREWD_BASIC_REFLECT_FIELD(node_t, data),
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
    expect_true("describe field count", (size_t)12 == g_person_reflect.field_count);
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
    /* Every payload line is a full log line: match per line, not across. */
    expect_true("dump nested close", NULL != strstr(buf, "[dump]   }\n"));
    expect_true("dump top close", NULL != strstr(buf, "[dump] }\n"));
    fclose(stream);

    /* Phase 2: escapes and the label-free header. */
    person_t weird;
    memset(&weird, 0, sizeof(weird));
    weird.name = "a\"b\nc";
    stream = tmpfile();
    if (NULL != stream) {
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
    if (NULL != stream) {
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
    if (NULL != stream) {
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
        expect_true("pointer self target", &g_node_reflect == f->type);
        expect_true("pointer shape",
                    ((size_t)1 == f->count) && (sizeof(node_t *) == f->size)
                    && (SHREWD_BASIC_REFLECT_FIELD_F_PTR == f->flags));
    }
    f = find_or_fail("pointer", &g_refs_reflect, "p_str");
    if (NULL != f) {
        expect_true("pointer string target", &shrewd_basic_reflect_string == f->type);
    }
    f = find_or_fail("pointer", &g_refs_reflect, "ctx");
    if (NULL != f) {
        expect_true("pointer opaque target",
                    (&shrewd_basic_reflect_void == f->type)
                    && (SHREWD_BASIC_REFLECT_KIND_OPAQUE == f->type->kind));
    }
    f = find_or_fail("pointer", &g_refs_reflect, "team");
    if (NULL != f) {
        expect_true("pointer array shape",
                    (&g_point_reflect == f->type)
                    && ((size_t)2 == f->count)
                    && (2 * sizeof(point_t *) == f->size)
                    && ((SHREWD_BASIC_REFLECT_FIELD_F_PTR
                         | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY) == f->flags));
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

int main(void)
{
    test_describe();
    test_find_field();
    test_get_set_scalars();
    test_get_set_string();
    test_nested();
    test_array();
    test_pointer_fields();
    test_dump();
    test_logging_policy();
    test_cross_tu();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

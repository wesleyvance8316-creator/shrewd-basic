/**
 * @file test_json_map.c
 * @brief Tests for the json struct mapping: encode, decode and free.
 *
 * Round-trips every field shape through the DOM (scalars, enums,
 * strings, text buffers, bit groups, arrays of all flavors, pointers,
 * tagged unions in F1 form, multi-dimension rows, flexible tails) and
 * checks the wire format, the decode rules (derived counts, short
 * arrays, unknown keys) and the error paths (paths in errors, cycles).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/json.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"
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

static void expect_streq(const char *name, const char *actual, const char *expected)
{
    if ((NULL == actual) || (NULL == expected) || (0 != strcmp(actual, expected))) {
        fprintf(stderr, "FAIL %s: got \"%s\", want \"%s\"\n", name,
                (NULL != actual) ? actual : "(null)",
                (NULL != expected) ? expected : "(null)");
        ++g_failures;
    }
}

/* --- fixtures --------------------------------------------------------- */

typedef struct {
    int32_t x;
    int32_t y;
} point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_reflect, point_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

typedef enum {
    CFG_OFF = 0,
    CFG_ON = 1
} cfg_mode_t;

typedef struct {
    bool active;
    char grade;
    int32_t age;
    uint64_t big;
    double precise;
    cfg_mode_t mode;
    const char *name;
    char tag[8];
    int32_t scores[3];
    point_t home;
    point_t pair[2];
} person_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(mode_reflect, cfg_mode_t,
    SHREWD_BASIC_REFLECT_ENUM_VALUE(CFG_OFF),
    SHREWD_BASIC_REFLECT_ENUM_VALUE(CFG_ON));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(person_reflect, person_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, active),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, grade),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, age),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, big),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, precise),
    SHREWD_BASIC_REFLECT_FIELD_ENUM(person_t, mode, mode_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(person_t, name),
    SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(person_t, tag),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, scores, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(person_t, home, point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(person_t, pair, point_reflect));

typedef struct {
    int32_t *values;
    size_t value_count;
    char **argv;
} dyn_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(dyn_reflect, dyn_t,
    SHREWD_BASIC_REFLECT_FIELD_DARRAY(dyn_t, values, value_count,
                                      shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dyn_t, value_count),
    SHREWD_BASIC_REFLECT_FIELD_SENTINEL(dyn_t, argv, shrewd_basic_reflect_string));

typedef enum {
    PAYLOAD_INT = 1,
    PAYLOAD_POINT = 2
} payload_kind_t;

typedef union {
    int32_t number;
    point_t point;
} payload_t;

typedef struct {
    payload_kind_t kind;
    payload_t payload;
} message_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(payload_kind_reflect, payload_kind_t,
    SHREWD_BASIC_REFLECT_ENUM_VALUE(PAYLOAD_INT),
    SHREWD_BASIC_REFLECT_ENUM_VALUE(PAYLOAD_POINT));

SHREWD_BASIC_REFLECT_UNION_DEFINE(payload_reflect, payload_t,
    SHREWD_BASIC_REFLECT_UNION_VARIANT(PAYLOAD_INT,
        SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(payload_t, number)),
    SHREWD_BASIC_REFLECT_UNION_VARIANT(PAYLOAD_POINT,
        SHREWD_BASIC_REFLECT_FIELD_STRUCT(payload_t, point, point_reflect)));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(message_reflect, message_t,
    SHREWD_BASIC_REFLECT_FIELD_ENUM(message_t, kind, payload_kind_reflect),
    SHREWD_BASIC_REFLECT_FIELD_UNION(message_t, payload, payload_reflect, kind));

typedef struct {
    uint32_t cfg;
    char letter;
} reg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(reg_reflect, reg_t,
    SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, ready, 0, 1),
    SHREWD_BASIC_REFLECT_FIELD_BITS(reg_t, cfg, mode, 1, 3),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(reg_t, letter));

typedef struct {
    int32_t m[2][3];
} matrix_t;

SHREWD_BASIC_REFLECT_ARRAY_DEFINE(row3_reflect, shrewd_basic_reflect_int32,
                                  int32_t, 3);
SHREWD_BASIC_REFLECT_TYPE_DEFINE(matrix_reflect, matrix_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(matrix_t, m, row3_reflect));

typedef struct {
    point_t *home;
    const char **p_str;
    int32_t *counter;
    point_t *links[2];
} refs_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(refs_reflect, refs_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR(refs_t, home, point_reflect),
    SHREWD_BASIC_REFLECT_FIELD_PTR(refs_t, p_str, shrewd_basic_reflect_string),
    SHREWD_BASIC_REFLECT_FIELD_PTR(refs_t, counter, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY(refs_t, links, point_reflect));

typedef struct {
    size_t len;
    int32_t data[];
} packet_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(packet_reflect, packet_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(packet_t, len),
    SHREWD_BASIC_REFLECT_FIELD_FLEX(packet_t, data, len, shrewd_basic_reflect_int32));

typedef struct node {
    int32_t data;
    struct node *next;
} node_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(node_reflect, node_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(node_t, data),
    SHREWD_BASIC_REFLECT_FIELD_PTR(node_t, next, node_reflect));

/* Encode @p object and compare the compact text with @p expected. */
static void expect_encode(const char *name, const void *object,
                          const shrewd_basic_reflect_type_t *type,
                          const char *expected)
{
    char *text = NULL;
    int status = shrewd_basic_json_encode(object, type, NULL, &text, NULL, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        fprintf(stderr, "FAIL %s: encode returned %d\n", name, status);
        ++g_failures;
        return;
    }
    expect_streq(name, text, expected);
    shrewd_basic_json_text_free(text);
}

/* Decode @p text into a fresh heap graph and return it (NULL on failure). */
static void *decode_new(const char *name, const char *text,
                        const shrewd_basic_reflect_type_t *type, size_t size)
{
    shrewd_basic_json_error_t error;
    void *block = NULL;
    int status = 0;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, size)) {
        return NULL;
    }
    status = shrewd_basic_json_decode(text, strlen(text), block, type, NULL, NULL,
                                      &error);
    if (SHREWD_BASIC_ERR_OK != status) {
        fprintf(stderr, "FAIL %s: decode returned %d at %s (%s)\n", name,
                status, error.path, error.message);
        ++g_failures;
        (void)shrewd_basic_json_free(block, type, NULL);
        (void)shrewd_basic_platform_free(&block);
        return NULL;
    }
    return block;
}

/* Release a decode_new() graph: internals first, then the root. */
static int free_new(void *object, const shrewd_basic_reflect_type_t *type)
{
    int status = shrewd_basic_json_free(object, type, NULL);
    (void)shrewd_basic_platform_free(&object);
    return status;
}

static int map_sink_write(void *ctx, const char *data, size_t length)
{
    char **cursor = (char **)ctx;
    memcpy(*cursor, data, length);
    *cursor += length;
    **cursor = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static void test_encode_person(void)
{
    person_t person;
    memset(&person, 0, sizeof(person));
    person.active = true;
    person.grade = 'A';
    person.age = 30;
    person.big = UINT64_C(18446744073709551615);
    person.precise = 2.5;
    person.mode = CFG_ON;
    person.name = "Alice";
    memcpy(person.tag, "hi", 3);
    person.scores[0] = 1;
    person.scores[1] = 2;
    person.scores[2] = 3;
    person.home.x = 4;
    person.home.y = 5;
    person.pair[1].x = 8;

    {
        /* encode_to streams the same bytes through a writer sink. */
        char streamed[1024];
        char *cursor = streamed;
        expect_eq_int("encode person to sink",
                      shrewd_basic_json_encode_to(&person, &person_reflect, NULL,
                                                  map_sink_write, &cursor, NULL,
                                                  NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("encode person to sink body",
                    NULL != strstr(streamed, "\"name\":\"Alice\""));
    }
    expect_encode("encode person", &person, &person_reflect,
                  "{\"active\":true,\"grade\":\"A\",\"age\":30,"
                  "\"big\":18446744073709551615,\"precise\":2.5,"
                  "\"mode\":\"CFG_ON\",\"name\":\"Alice\",\"tag\":\"hi\","
                  "\"scores\":[1,2,3],\"home\":{\"x\":4,\"y\":5},"
                  "\"pair\":[{\"x\":0,\"y\":0},{\"x\":8,\"y\":0}]}");

    /* NULL string renders as null. */
    person.name = NULL;
    {
        char *text = NULL;
        expect_eq_int("encode null string",
                      shrewd_basic_json_encode(&person, &person_reflect, NULL, &text,
                                               NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("encode null string body",
                    (NULL != text) && (NULL != strstr(text, "\"name\":null")));
        shrewd_basic_json_text_free(text);
    }

    /* Round trip: encode -> decode -> equal. */
    {
        person_t *clone = NULL;
        bool same = false;
        person.name = "Alice";
        {
            char *text = NULL;
            expect_eq_int("roundtrip person encode",
                          shrewd_basic_json_encode(&person, &person_reflect, NULL,
                                                   &text, NULL, NULL),
                          SHREWD_BASIC_ERR_OK);
            clone = decode_new("roundtrip person", text, &person_reflect,
                               sizeof(person_t));
            shrewd_basic_json_text_free(text);
        }
        if (NULL == clone) {
            return;
        }
        expect_eq_int("roundtrip person equal",
                      shrewd_basic_reflect_equal(&person, clone, &person_reflect,
                                                 &same),
                      SHREWD_BASIC_ERR_OK);
        expect_true("roundtrip person same", same);
        expect_eq_int("roundtrip person free",
                      free_new(clone, &person_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_decode_rules(void)
{
    /* Decode fills fields; missing keys keep zero; count members are
       derived from array lengths; short arrays zero their tail. */
    person_t *person = decode_new(
            "decode person",
            "{\"age\":31,\"mode\":\"CFG_ON\",\"name\":\"Bob\","
            "\"tag\":\"ok\",\"scores\":[9],\"home\":{\"x\":1}}",
            &person_reflect, sizeof(person_t));
    if (NULL == person) {
        return;
    }
    expect_true("decode filled",
                (31 == person->age) && (CFG_ON == person->mode)
                && (NULL != person->name) && (0 == strcmp(person->name, "Bob"))
                && (0 == strcmp(person->tag, "ok")) && (9 == person->scores[0]));
    expect_true("decode zero defaults",
                (0 == person->scores[1]) && (0 == person->scores[2])
                && (0 == person->home.y) && (0 == person->pair[1].x));
    expect_eq_int("decode free", free_new(person, &person_reflect),
                  SHREWD_BASIC_ERR_OK);

    /* Enum numbers are accepted too; unknown keys are ignored. */
    person = decode_new("decode enum number",
                        "{\"mode\":1,\"extra\":{\"deep\":[1,2]}}",
                        &person_reflect, sizeof(person_t));
    if (NULL == person) {
        return;
    }
    expect_true("decode enum number value", CFG_ON == person->mode);
    expect_eq_int("decode enum free",
                  free_new(person, &person_reflect),
                  SHREWD_BASIC_ERR_OK);

    /* Errors carry the JSON path. */
    {
        shrewd_basic_json_error_t error;
        person_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("decode mismatch",
                      shrewd_basic_json_decode("{\"home\":{\"x\":\"bad\"}}", strlen("{\"home\":{\"x\":\"bad\"}}"),
                                               &target, &person_reflect, NULL, NULL,
                                               &error),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_streq("decode mismatch path", error.path, "home.x");
        expect_eq_int("decode mismatch free",
                      shrewd_basic_json_free(&target, &person_reflect, NULL),
                      SHREWD_BASIC_ERR_OK);
    }
    {
        shrewd_basic_json_error_t error;
        person_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("decode long array",
                      shrewd_basic_json_decode("{\"scores\":[1,2,3,4]}", strlen("{\"scores\":[1,2,3,4]}"),
                                               &target, &person_reflect, NULL, NULL,
                                               &error),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_streq("decode long array path", error.path, "scores");
        expect_eq_int("decode long array free",
                      shrewd_basic_json_free(&target, &person_reflect, NULL),
                      SHREWD_BASIC_ERR_OK);
    }
    {
        /* Exactly count chars fit (unterminated); one more errors. */
        person_t *filled = decode_new("decode buffer full",
                                      "{\"tag\":\"12345678\"}", &person_reflect,
                                      sizeof(person_t));
        if (NULL != filled) {
            expect_true("decode buffer full content",
                        0 == memcmp(filled->tag, "12345678", 8));
            expect_eq_int("decode buffer full free",
                          free_new(filled, &person_reflect), SHREWD_BASIC_ERR_OK);
        }
    }
    {
        shrewd_basic_json_error_t error;
        person_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("decode long buffer",
                      shrewd_basic_json_decode(
                              "{\"tag\":\"123456789\"}",
                              strlen("{\"tag\":\"123456789\"}"), &target,
                              &person_reflect, NULL, NULL, &error),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_streq("decode long buffer path", error.path, "tag");
        expect_eq_int("decode long buffer free",
                      shrewd_basic_json_free(&target, &person_reflect, NULL),
                      SHREWD_BASIC_ERR_OK);
    }
    {
        shrewd_basic_json_error_t error;
        person_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("decode bad enum",
                      shrewd_basic_json_decode("{\"mode\":\"NOPE\"}", strlen("{\"mode\":\"NOPE\"}"), &target,
                                               &person_reflect, NULL, NULL, &error),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_streq("decode bad enum path", error.path, "mode");
    }
}

static void test_union(void)
{
    message_t message;
    memset(&message, 0, sizeof(message));
    message.kind = PAYLOAD_POINT;
    message.payload.point.x = 1;
    message.payload.point.y = 2;

    /* F1 form: the tag is its own key, the body nests the variant. */
    expect_encode("encode union", &message, &message_reflect,
                  "{\"kind\":\"PAYLOAD_POINT\","
                  "\"payload\":{\"point\":{\"x\":1,\"y\":2}}}");

    /* Round trip both variants. */
    {
        message_t *clone = decode_new(
                "decode union",
                "{\"payload\":{\"point\":{\"x\":7,\"y\":8}},\"kind\":\"PAYLOAD_POINT\"}",
                &message_reflect, sizeof(message_t));
        if (NULL != clone) {
            expect_true("decode union tag late",
                        (PAYLOAD_POINT == clone->kind)
                        && (7 == clone->payload.point.x));
            expect_eq_int("decode union free",
                          free_new(clone, &message_reflect),
                          SHREWD_BASIC_ERR_OK);
        }
    }
    message.kind = PAYLOAD_INT;
    message.payload.number = 42;
    {
        message_t *clone = NULL;
        bool same = false;
        char *text = NULL;
        expect_eq_int("union encode int",
                      shrewd_basic_json_encode(&message, &message_reflect, NULL, &text,
                                               NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        clone = decode_new("decode union int", text, &message_reflect,
                           sizeof(message_t));
        shrewd_basic_json_text_free(text);
        if (NULL != clone) {
            expect_eq_int("union equal",
                          shrewd_basic_reflect_equal(&message, clone,
                                                     &message_reflect, &same),
                          SHREWD_BASIC_ERR_OK);
            expect_true("union equal yes", same);
            expect_eq_int("union free",
                          free_new(clone, &message_reflect),
                          SHREWD_BASIC_ERR_OK);
        }
    }

    /* Unknown tag: error by default, null body on request. */
    message.kind = (payload_kind_t)9;
    {
        shrewd_basic_json_error_t error;
        char *text = NULL;
        expect_eq_int("union unknown tag",
                      shrewd_basic_json_encode(&message, &message_reflect, NULL, &text,
                                               NULL, &error),
                      SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
        expect_streq("union unknown tag path", error.path, "payload");
        {
            shrewd_basic_json_options_t options;
            memset(&options, 0, sizeof(options));
            options.allow_unknown_union = 1;
            expect_eq_int("union unknown allowed",
                          shrewd_basic_json_encode(&message, &message_reflect, NULL,
                                                   &text, &options, NULL),
                          SHREWD_BASIC_ERR_OK);
            expect_true("union unknown body",
                        (NULL != text)
                        && (NULL != strstr(text, "\"payload\":null")));
            shrewd_basic_json_text_free(text);
        }
    }
}

static void test_arrays_and_bits(void)
{
    /* Dynamic arrays: counts are derived from array lengths. */
    dyn_t *dyn = decode_new(
            "decode dyn",
            "{\"values\":[7,8,9],\"value_count\":999,\"argv\":[\"ls\",\"-l\"]}",
            &dyn_reflect, sizeof(dyn_t));
    if (NULL != dyn) {
        expect_true("decode dyn counts",
                    ((size_t)3 == dyn->value_count) && (9 == dyn->values[2])
                    && (NULL != dyn->argv) && (NULL == dyn->argv[2])
                    && (0 == strcmp(dyn->argv[1], "-l")));
        expect_encode("encode dyn", dyn, &dyn_reflect,
                      "{\"values\":[7,8,9],\"value_count\":3,"
                      "\"argv\":[\"ls\",\"-l\"]}");
        expect_eq_int("decode dyn free",
                      free_new(dyn, &dyn_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* NULL dynamic table. */
    dyn = decode_new("decode dyn null", "{\"values\":null,\"argv\":null}",
                     &dyn_reflect, sizeof(dyn_t));
    if (NULL != dyn) {
        expect_true("decode dyn null tables",
                    (NULL == dyn->values) && (NULL == dyn->argv));
        expect_eq_int("decode dyn null free",
                      free_new(dyn, &dyn_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* Bit groups: numbers in, extracted numbers out. */
    {
        reg_t reg;
        reg_t *clone = NULL;
        memset(&reg, 0, sizeof(reg));
        reg.cfg = (1u << 0) | (5u << 1) | 0xf0u;
        reg.letter = 'Z';
        expect_encode("encode bits", &reg, &reg_reflect,
                      "{\"ready\":1,\"mode\":5,\"letter\":\"Z\"}");
        clone = decode_new("decode bits",
                           "{\"ready\":1,\"mode\":3,\"letter\":\"A\"}",
                           &reg_reflect, sizeof(reg_t));
        if (NULL != clone) {
            expect_true("decode bits rmw",
                        ((1u << 0) | (3u << 1)) == clone->cfg);
            expect_eq_int("decode bits free",
                          free_new(clone, &reg_reflect),
                          SHREWD_BASIC_ERR_OK);
        }
    }

    /* Multi-dimension rows nest. */
    {
        matrix_t matrix;
        matrix_t *clone = NULL;
        memset(&matrix, 0, sizeof(matrix));
        for (size_t r = 0; r < 2; ++r) {
            for (size_t c = 0; c < 3; ++c) {
                matrix.m[r][c] = (int32_t)((r * 3) + c);
            }
        }
        expect_encode("encode matrix", &matrix, &matrix_reflect,
                      "{\"m\":[[0,1,2],[3,4,5]]}");
        clone = decode_new("decode matrix", "{\"m\":[[9,8,7],[6,5,4]]}",
                           &matrix_reflect, sizeof(matrix_t));
        if (NULL != clone) {
            expect_true("decode matrix cells",
                        (9 == clone->m[0][0]) && (4 == clone->m[1][2]));
            expect_eq_int("decode matrix free",
                          free_new(clone, &matrix_reflect),
                          SHREWD_BASIC_ERR_OK);
        }
    }
}

static void test_pointers(void)
{
    refs_t *refs = decode_new(
            "decode refs",
            "{\"home\":{\"x\":3,\"y\":4},\"p_str\":\"hi\",\"counter\":7,"
            "\"links\":[{\"x\":1,\"y\":2},null]}",
            &refs_reflect, sizeof(refs_t));
    if (NULL != refs) {
        expect_true("decode refs targets",
                    (NULL != refs->home) && (3 == refs->home->x)
                    && (NULL != refs->p_str) && (0 == strcmp(*refs->p_str, "hi"))
                    && (NULL != refs->counter) && (7 == *refs->counter));
        expect_true("decode refs pointer array",
                    (NULL != refs->links[0]) && (1 == refs->links[0]->x)
                    && (NULL == refs->links[1]));
        /* Metadata pointer arrays round-trip (no ops involved). */
        expect_encode("encode refs", refs, &refs_reflect,
                      "{\"home\":{\"x\":3,\"y\":4},\"p_str\":\"hi\",\"counter\":7,"
                      "\"links\":[{\"x\":1,\"y\":2},null]}");
        expect_eq_int("decode refs free",
                      free_new(refs, &refs_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
    refs = decode_new("decode refs null",
                      "{\"home\":null,\"p_str\":null,\"counter\":null}",
                      &refs_reflect, sizeof(refs_t));
    if (NULL != refs) {
        expect_true("decode refs nulls",
                    (NULL == refs->home) && (NULL == refs->p_str)
                    && (NULL == refs->counter));
        expect_eq_int("decode refs nulls free",
                      free_new(refs, &refs_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* Flexible tail: the caller sizes the allocation from the JSON. */
    {
        shrewd_basic_json_error_t error;
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK
                == shrewd_basic_platform_calloc(
                        &block, 1, sizeof(packet_t) + (3 * sizeof(int32_t)))) {
            packet_t *packet = block;
            expect_eq_int("decode packet",
                          shrewd_basic_json_decode(
                                  "{\"data\":[7,8,9],\"len\":999}", strlen("{\"data\":[7,8,9],\"len\":999}"), packet,
                                  &packet_reflect, NULL, NULL, &error),
                          SHREWD_BASIC_ERR_OK);
            expect_true("decode packet tail",
                        ((size_t)3 == packet->len) && (7 == packet->data[0])
                        && (9 == packet->data[2]));
            expect_eq_int("decode packet free",
                          free_new(packet, &packet_reflect),
                          SHREWD_BASIC_ERR_OK);
            {
                /* value_size reports the FAM sizing formula. */
                shrewd_basic_json_value_t *dom = NULL;
                size_t needed = 0;
                const char *dom_text = "{\"data\":[7,8,9],\"len\":3}";
                expect_eq_int("packet dom",
                              shrewd_basic_json_parse(dom_text,
                                                      strlen(dom_text), &dom,
                                                      NULL, NULL),
                              SHREWD_BASIC_ERR_OK);
                expect_eq_int("packet value size",
                              shrewd_basic_json_value_size(dom, &packet_reflect,
                                                           &needed),
                              SHREWD_BASIC_ERR_OK);
                expect_true("packet value size value",
                            needed == (sizeof(packet_t) + (3 * sizeof(int32_t))));
                shrewd_basic_json_value_free(dom);
            }
        }
    }

    /* Cycles cannot be encoded. */
    {
        shrewd_basic_json_error_t error;
        node_t loop;
        char *text = NULL;
        memset(&loop, 0, sizeof(loop));
        loop.next = &loop;
        expect_eq_int("encode cycle",
                      shrewd_basic_json_encode(&loop, &node_reflect, NULL, &text, NULL,
                                               &error),
                      SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
        expect_streq("encode cycle path", error.path, "next");
    }
}

static void test_encode_options(void)
{
    person_t person;
    char *text = NULL;
    shrewd_basic_json_options_t options;
    memset(&person, 0, sizeof(person));
    {
        uint64_t bits = UINT64_C(0x7ff8000000000000); /* NaN */
        memcpy(&person.precise, &bits, sizeof(bits));
    }
    person.name = "x";

    /* Non-finite numbers fail by default, become null on request. */
    expect_eq_int("encode nan",
                  shrewd_basic_json_encode(&person, &person_reflect, NULL, &text, NULL,
                                           NULL),
                  SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
    memset(&options, 0, sizeof(options));
    options.allow_nan = 1;
    expect_eq_int("encode nan allowed",
                  shrewd_basic_json_encode(&person, &person_reflect, NULL, &text,
                                           &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("encode nan null",
                (NULL != text) && (NULL != strstr(text, "\"precise\":null")));
    shrewd_basic_json_text_free(text);

    /* Pretty output passes through the mapping layer. */
    person.precise = 1.5;
    memset(&options, 0, sizeof(options));
    options.pretty = 1;
    expect_eq_int("encode pretty",
                  shrewd_basic_json_encode(&person, &person_reflect, NULL, &text,
                                           &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("encode pretty body",
                (NULL != text) && (NULL != strstr(text, "\n  \"age\": 0")));
    shrewd_basic_json_text_free(text);
}

int main(void)
{
    /* The mapping traces are noisy; silence the reflect/json modules. */
    shrewd_basic_log_module_t *module = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("reflect", &module)) {
        (void)shrewd_basic_log_module_set_level(module,
                                                SHREWD_BASIC_LOG_LEVEL_NONE);
    }
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_log_module_find("json", &module)) {
        (void)shrewd_basic_log_module_set_level(module,
                                                SHREWD_BASIC_LOG_LEVEL_NONE);
    }

    test_encode_person();
    test_decode_rules();
    test_union();
    test_arrays_and_bits();
    test_pointers();
    test_encode_options();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * @file test_json_policy.c
 * @brief Tests for the json policy layer: key aliases, flags, ranges,
 * custom ops, union body styles and the json_keys() schema export.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/json/json.h"
#include "shrewd-basic/platform/memory.h"

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

/* --- fixture with a full policy --------------------------------------- */

typedef struct {
    int32_t age;
    const char *name;
    const char *secret;
    int32_t score;
} cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(cfg_reflect, cfg_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, age),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, name),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, secret),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(cfg_t, score));

/* Custom score hooks: the wire value is score + 1, negatives rejected. */
static int score_encode(const void *value, shrewd_basic_json_value_t **out)
{
    int32_t score = 0;
    memcpy(&score, value, sizeof(score));
    return shrewd_basic_json_new_i64((int64_t)score + 1, out);
}

static int score_decode(const shrewd_basic_json_value_t *value, void *target)
{
    int64_t wire = 0;
    int32_t score = 0;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &wire)) {
        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
    }
    score = (int32_t)(wire - 1);
    memcpy(target, &score, sizeof(score));
    return SHREWD_BASIC_ERR_OK;
}

static int score_check(const void *value)
{
    int32_t score = 0;
    memcpy(&score, value, sizeof(score));
    return (0 <= score) ? SHREWD_BASIC_ERR_OK : SHREWD_BASIC_ERR_ARG_INVALID;
}

static const shrewd_basic_json_ops_t g_score_ops = {
    .encode = score_encode,
    .decode = score_decode,
    .check = score_check };

SHREWD_BASIC_JSON_DEFINE(cfg_json, cfg_reflect,
    SHREWD_BASIC_JSON_KEY(name, "name_id", "nameID", "NameID"),
    SHREWD_BASIC_JSON_REQUIRED(name),
    SHREWD_BASIC_JSON_HIDDEN(secret),
    SHREWD_BASIC_JSON_OMIT_EMPTY(score),
    SHREWD_BASIC_JSON_RANGE(age, 0, 150),
    SHREWD_BASIC_JSON_OPS(score, g_score_ops));

/* --- union style fixture ---------------------------------------------- */

typedef enum {
    MSG_TEXT = 0,
    MSG_POINT = 1
} msg_kind_t;

typedef struct {
    int32_t x;
    int32_t y;
} point_t;

typedef union {
    char *text;
    point_t point;
} msg_body_t;

typedef struct {
    msg_kind_t kind;
    msg_body_t body;
} msg_t;

SHREWD_BASIC_REFLECT_ENUM_DEFINE(msg_kind_reflect, msg_kind_t,
    SHREWD_BASIC_REFLECT_ENUM_VALUE(MSG_TEXT),
    SHREWD_BASIC_REFLECT_ENUM_VALUE(MSG_POINT));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_reflect, point_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

SHREWD_BASIC_REFLECT_UNION_DEFINE(msg_body_reflect, msg_body_t,
    SHREWD_BASIC_REFLECT_UNION_VARIANT(MSG_TEXT,
        SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(msg_body_t, text)),
    SHREWD_BASIC_REFLECT_UNION_VARIANT(MSG_POINT,
        SHREWD_BASIC_REFLECT_FIELD_STRUCT(msg_body_t, point, point_reflect)));

SHREWD_BASIC_REFLECT_TYPE_DEFINE(msg_reflect, msg_t,
    SHREWD_BASIC_REFLECT_FIELD_ENUM(msg_t, kind, msg_kind_reflect),
    SHREWD_BASIC_REFLECT_FIELD_UNION(msg_t, body, msg_body_reflect, kind));

SHREWD_BASIC_JSON_DEFINE(msg_flat_json, msg_reflect,
    SHREWD_BASIC_JSON_UNION_STYLE(body, SHREWD_BASIC_JSON_UNION_FLAT));

SHREWD_BASIC_JSON_DEFINE(msg_unwrap_json, msg_reflect,
    SHREWD_BASIC_JSON_UNION_STYLE(body, SHREWD_BASIC_JSON_UNION_UNWRAP));

/* --- helpers ---------------------------------------------------------- */

/* Encode with @p info and compare compact text. */
static void expect_encode(const char *name, const void *object,
                          const shrewd_basic_reflect_type_t *type,
                          const shrewd_basic_json_info_t *info,
                          const char *expected)
{
    char *text = NULL;
    int status = shrewd_basic_json_encode(object, type, info, &text, NULL, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        fprintf(stderr, "FAIL %s: encode returned %d\n", name, status);
        ++g_failures;
        return;
    }
    expect_streq(name, text, expected);
    shrewd_basic_json_text_free(text);
}

/* Decode @p text into @p target and return the status. */
static int decode_into(const char *text, void *target,
                       const shrewd_basic_reflect_type_t *type,
                       const shrewd_basic_json_info_t *info,
                       const shrewd_basic_json_options_t *options,
                       shrewd_basic_json_error_t *error)
{
    return shrewd_basic_json_decode(text, strlen(text), target, type, info,
                                    options, error);
}

static void test_key_aliases(void)
{
    cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.age = 30;
    cfg.name = "bob";
    cfg.secret = "s3cr3t";
    cfg.score = 0;

    /* Canonical key on encode; hidden and empty fields skipped. */
    expect_encode("policy encode keys", &cfg, &cfg_reflect, &cfg_json,
                  "{\"age\":30,\"name_id\":\"bob\"}");

    /* Non-empty custom field appears through its ops (score + 1). */
    cfg.score = 5;
    expect_encode("policy encode ops", &cfg, &cfg_reflect, &cfg_json,
                  "{\"age\":30,\"name_id\":\"bob\",\"score\":6}");

    /* Decode accepts every alias and the plain member name. */
    {
        cfg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy decode alias",
                      decode_into("{\"age\":20,\"NameID\":\"al\",\"score\":4}",
                                  &target, &cfg_reflect, &cfg_json, NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("policy alias value",
                    (0 == strcmp(target.name, "al")) && (3 == target.score));
        expect_eq_int("policy alias free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
    {
        cfg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy decode member name",
                      decode_into("{\"name\":\"x\"}", &target, &cfg_reflect,
                                  &cfg_json, NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_streq("policy member name value", target.name, "x");
        expect_eq_int("policy member name free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* Aliases conflict: last wins by default, strict rejects. */
    {
        cfg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy conflict",
                      decode_into("{\"name_id\":\"a\",\"nameID\":\"b\"}", &target,
                                  &cfg_reflect, &cfg_json, NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_streq("policy conflict last wins", target.name, "b");
        expect_eq_int("policy conflict free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
    {
        shrewd_basic_json_options_t options;
        shrewd_basic_json_error_t error;
        cfg_t target;
        memset(&options, 0, sizeof(options));
        options.strict = 1;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy conflict strict",
                      decode_into("{\"name_id\":\"a\",\"nameID\":\"b\"}", &target,
                                  &cfg_reflect, &cfg_json, &options, &error),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_eq_int("policy conflict strict free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* Strict rejects unknown keys. */
    {
        shrewd_basic_json_options_t options;
        cfg_t target;
        memset(&options, 0, sizeof(options));
        options.strict = 1;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy strict unknown",
                      decode_into("{\"name\":\"x\",\"zz\":1}", &target,
                                  &cfg_reflect, &cfg_json, &options, NULL),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_eq_int("policy strict unknown free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_required_range_check(void)
{
    /* REQUIRED: the alias set must be present. */
    {
        shrewd_basic_json_error_t error;
        cfg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy required missing",
                      decode_into("{\"age\":1}", &target, &cfg_reflect, &cfg_json,
                                  NULL, &error),
                      SHREWD_BASIC_ERR_KEY_NOTFOUND);
        expect_streq("policy required path", error.path, "name");
        expect_eq_int("policy required free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* RANGE: checked both directions. */
    {
        shrewd_basic_json_error_t error;
        cfg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy range decode",
                      decode_into("{\"name\":\"x\",\"age\":200}", &target,
                                  &cfg_reflect, &cfg_json, NULL, &error),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_streq("policy range path", error.path, "age");
        expect_eq_int("policy range free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
    {
        cfg_t cfg;
        char *text = NULL;
        memset(&cfg, 0, sizeof(cfg));
        cfg.age = 200;
        cfg.name = "bob";
        expect_eq_int("policy range encode",
                      shrewd_basic_json_encode(&cfg, &cfg_reflect, &cfg_json,
                                               &text, NULL, NULL),
                      SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
        expect_true("policy range encode no text", NULL == text);
    }

    /* ops->check rejects on decode (and the error keeps the path). */
    {
        shrewd_basic_json_error_t error;
        cfg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy check decode",
                      decode_into("{\"name\":\"x\",\"score\":-2}", &target,
                                  &cfg_reflect, &cfg_json, NULL, &error),
                      SHREWD_BASIC_ERR_ARG_INVALID);
        expect_streq("policy check path", error.path, "score");
        expect_eq_int("policy check free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* HIDDEN keys are ignored on decode even when present. */
    {
        cfg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy hidden decode",
                      decode_into("{\"name\":\"x\",\"secret\":\"zz\"}", &target,
                                  &cfg_reflect, &cfg_json, NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("policy hidden stays zero", NULL == target.secret);
        expect_eq_int("policy hidden free",
                      shrewd_basic_json_free(&target, &cfg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_union_styles(void)
{
    msg_t msg;
    memset(&msg, 0, sizeof(msg));

    /* F4 flat: the variant's fields are promoted to the parent object
       (here the variant field is the struct "point"). */
    msg.kind = MSG_POINT;
    msg.body.point.x = 1;
    msg.body.point.y = 2;
    expect_encode("policy union flat", &msg, &msg_reflect, &msg_flat_json,
                  "{\"kind\":\"MSG_POINT\",\"point\":{\"x\":1,\"y\":2}}");
    {
        msg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy union flat decode",
                      decode_into("{\"point\":{\"x\":5,\"y\":6},\"kind\":\"MSG_POINT\"}",
                                  &target, &msg_reflect, &msg_flat_json, NULL,
                                  NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("policy union flat value",
                    (MSG_POINT == target.kind) && (5 == target.body.point.x));
        expect_eq_int("policy union flat free",
                      shrewd_basic_json_free(&target, &msg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* F2 unwrap: a single-field variant carries the value directly. */
    msg.kind = MSG_TEXT;
    msg.body.text = "hello";
    expect_encode("policy union unwrap", &msg, &msg_reflect, &msg_unwrap_json,
                  "{\"kind\":\"MSG_TEXT\",\"body\":\"hello\"}");
    {
        msg_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("policy union unwrap decode",
                      decode_into("{\"kind\":\"MSG_TEXT\",\"body\":\"hi\"}",
                                  &target, &msg_reflect, &msg_unwrap_json, NULL,
                                  NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("policy union unwrap value",
                    (NULL != target.body.text)
                    && (0 == strcmp(target.body.text, "hi")));
        expect_eq_int("policy union unwrap free",
                      shrewd_basic_json_free(&target, &msg_reflect),
                      SHREWD_BASIC_ERR_OK);
    }

    /* The default stays F1 nested when no style is configured. */
    msg.kind = MSG_POINT;
    msg.body.point.x = 1;
    msg.body.point.y = 2;
    expect_encode("policy union default", &msg, &msg_reflect, NULL,
                  "{\"kind\":\"MSG_POINT\",\"body\":{\"point\":{\"x\":1,\"y\":2}}}");
}

/* json_keys() capture. */
typedef struct key_capture {
    int count;
    char name_keys[4][32];
    size_t name_key_count;
    uint32_t name_flags;
    uint32_t secret_flags;
    int age_has_range;
    double age_min;
    double age_max;
} key_capture_t;

static int key_capture_cb(void *ctx, const shrewd_basic_json_key_info_t *key)
{
    key_capture_t *cap = (key_capture_t *)ctx;
    cap->count += 1;
    if (0 == strcmp(key->field, "name")) {
        cap->name_key_count = key->key_count;
        cap->name_flags = key->flags;
        for (size_t i = 0; i < key->key_count; ++i) {
            (void)snprintf(cap->name_keys[i], sizeof(cap->name_keys[i]), "%s",
                           key->keys[i]);
        }
    }
    if (0 == strcmp(key->field, "secret")) {
        cap->secret_flags = key->flags;
    }
    if (0 == strcmp(key->field, "age")) {
        cap->age_has_range = (0 != (key->flags & SHREWD_BASIC_JSON_F_RANGE));
        cap->age_min = key->min;
        cap->age_max = key->max;
    }
    return 0;
}

static void test_keys_export(void)
{
    key_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    expect_eq_int("policy keys",
                  shrewd_basic_json_keys(&cfg_reflect, &cfg_json, key_capture_cb,
                                         &cap),
                  SHREWD_BASIC_ERR_OK);
    expect_true("policy keys count", 4 == cap.count);
    expect_true("policy keys aliases",
                ((size_t)3 == cap.name_key_count)
                && (0 == strcmp(cap.name_keys[0], "name_id"))
                && (0 == strcmp(cap.name_keys[2], "NameID")));
    expect_true("policy keys flags",
                (0 != (cap.name_flags & SHREWD_BASIC_JSON_F_REQUIRED))
                && (0 != (cap.secret_flags & SHREWD_BASIC_JSON_F_HIDDEN)));
    expect_true("policy keys range",
                (0 != cap.age_has_range) && (0.0 == cap.age_min)
                && (150.0 == cap.age_max));

    /* Plain metadata view without policy. */
    memset(&cap, 0, sizeof(cap));
    expect_eq_int("policy keys plain",
                  shrewd_basic_json_keys(&cfg_reflect, NULL, key_capture_cb, &cap),
                  SHREWD_BASIC_ERR_OK);
    expect_true("policy keys plain view",
                (4 == cap.count) && ((size_t)1 == cap.name_key_count));

    /* A mismatched info is rejected. */
    expect_eq_int("policy keys mismatch",
                  shrewd_basic_json_keys(&msg_reflect, &cfg_json, key_capture_cb,
                                         &cap),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    {
        char *text = NULL;
        cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.name = "x";
        expect_eq_int("policy encode mismatch",
                      shrewd_basic_json_encode(&cfg, &cfg_reflect, &msg_flat_json,
                                               &text, NULL, NULL),
                      SHREWD_BASIC_ERR_ARG_INVALID);
    }
}

int main(void)
{
    test_key_aliases();
    test_required_range_check();
    test_union_styles();
    test_keys_export();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

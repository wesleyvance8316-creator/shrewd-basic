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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &cfg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &msg_reflect, NULL),
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
                      shrewd_basic_json_free(&target, &msg_reflect, NULL),
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

/* --- custom pointer-target fixture ------------------------------------ */

typedef struct {
    int32_t *handle;
} ext_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(ext_reflect, ext_t,
    SHREWD_BASIC_REFLECT_FIELD_PTR(ext_t, handle, shrewd_basic_reflect_int32));

static int g_handle_destroys = 0;

static int handle_encode(const void *value, shrewd_basic_json_value_t **out)
{
    int32_t number = 0;
    memcpy(&number, value, sizeof(number));
    return shrewd_basic_json_new_i64(number, out);
}

static int handle_decode(const shrewd_basic_json_value_t *value, void *slot)
{
    int64_t wire = 0;
    void *target = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &wire)) {
        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
    }
    if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_alloc(&target, sizeof(int32_t))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    *(int32_t *)target = (int32_t)wire;
    memcpy(slot, &target, sizeof(target));
    return SHREWD_BASIC_ERR_OK;
}

static void handle_destroy(void *target)
{
    ++g_handle_destroys;
    (void)shrewd_basic_platform_free(&target);
}

static const shrewd_basic_json_ops_t g_handle_json_ops = {
    .encode = handle_encode,
    .decode = handle_decode,
    .destroy = handle_destroy };

SHREWD_BASIC_JSON_DEFINE(ext_json, ext_reflect,
    SHREWD_BASIC_JSON_OPS(handle, g_handle_json_ops));

/* Loose-key collision: name_id and nameID are the same under LOOSE. */
typedef struct {
    int32_t name_id;
    int32_t nameID;
} collide_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(collide_reflect, collide_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(collide_t, name_id),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(collide_t, nameID));

/* --- use_defaults / BORROWED fixture ---------------------------------- */

typedef struct {
    int32_t x;
    int32_t y;
    const char *note;
} dpos_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(dpos_reflect, dpos_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dpos_t, x),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dpos_t, y),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dpos_t, note));

typedef struct {
    int32_t level;
    double ratio;
    const char *label;
    const char *unit; /* BORROWED: application-owned */
    dpos_t pos;
    int32_t scores[3];
} dfl_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(dfl_reflect, dfl_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dfl_t, level),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dfl_t, ratio),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dfl_t, label),
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(dfl_t, unit),
    SHREWD_BASIC_REFLECT_FIELD_STRUCT(dfl_t, pos, dpos_reflect),
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(dfl_t, scores, shrewd_basic_reflect_int32));

SHREWD_BASIC_JSON_DEFINE(dfl_json, dfl_reflect,
    SHREWD_BASIC_JSON_DEFAULT(level, 7),
    SHREWD_BASIC_JSON_DEFAULT(ratio, 0.5),
    SHREWD_BASIC_JSON_DEFAULT_STR(label, "guest"),
    SHREWD_BASIC_JSON_REQUIRED(label),
    SHREWD_BASIC_JSON_BORROWED(unit),
    SHREWD_BASIC_JSON_DEFAULT_JSON(pos, "{\"x\":1,\"y\":2,\"note\":\"hi\"}"),
    SHREWD_BASIC_JSON_DEFAULT_JSON(scores, "[7,8,9]"));

/* Broken default text: must surface as a syntax error at decode. */
typedef struct {
    int32_t v;
} badfl_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(badfl_reflect, badfl_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(badfl_t, v));

SHREWD_BASIC_JSON_DEFINE(badfl_json, badfl_reflect,
    SHREWD_BASIC_JSON_DEFAULT_JSON(v, "{oops"));

static void test_use_defaults(void)
{
    shrewd_basic_json_options_t options;
    dfl_t target;
    memset(&options, 0, sizeof(options));
    options.use_defaults = 1;

    /* Missing keys materialize the policy defaults (decode allocates
       the string); REQUIRED is satisfied by the default. */
    memset(&target, 0, sizeof(target));
    expect_eq_int("defaults decode",
                  decode_into("{}", &target, &dfl_reflect, &dfl_json,
                              &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("defaults num", 7 == target.level);
    expect_true("defaults float", 0.5 == target.ratio);
    expect_streq("defaults str", target.label, "guest");
    expect_eq_int("defaults free",
                  shrewd_basic_json_free(&target, &dfl_reflect, &dfl_json),
                  SHREWD_BASIC_ERR_OK);
    expect_true("defaults str released", NULL == target.label);

    /* A present key wins over the default; explicit null is present. */
    memset(&target, 0, sizeof(target));
    expect_eq_int("defaults override",
                  decode_into("{\"level\":3,\"label\":null}", &target,
                              &dfl_reflect, &dfl_json, &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("defaults override level", 3 == target.level);
    expect_true("defaults override null", NULL == target.label);
    expect_eq_int("defaults override free",
                  shrewd_basic_json_free(&target, &dfl_reflect, &dfl_json),
                  SHREWD_BASIC_ERR_OK);

    /* use_defaults off: missing keys stay zero, missing REQUIRED errors. */
    memset(&options, 0, sizeof(options));
    memset(&target, 0, sizeof(target));
    expect_eq_int("defaults off required",
                  decode_into("{}", &target, &dfl_reflect, &dfl_json,
                              &options, NULL),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    expect_eq_int("defaults off free",
                  shrewd_basic_json_free(&target, &dfl_reflect, &dfl_json),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("defaults off decode",
                  decode_into("{\"level\":3}", &target, &dfl_reflect, &dfl_json,
                              &options, NULL),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND); /* label still required */

    /* Duplicate keys: refill frees the earlier copy, last wins. */
    options.use_defaults = 1;
    memset(&target, 0, sizeof(target));
    expect_eq_int("defaults refill",
                  decode_into("{\"label\":\"a\",\"label\":\"b\"}", &target,
                              &dfl_reflect, &dfl_json, &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_streq("defaults refill last wins", target.label, "b");
    expect_eq_int("defaults refill free",
                  shrewd_basic_json_free(&target, &dfl_reflect, &dfl_json),
                  SHREWD_BASIC_ERR_OK);

    /* BORROWED: decode leaves it NULL, json_free never touches it. */
    memset(&target, 0, sizeof(target));
    expect_eq_int("borrowed decode",
                  decode_into("{\"label\":\"x\"}", &target, &dfl_reflect,
                              &dfl_json, &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("borrowed untouched by decode", NULL == target.unit);
    target.unit = "kg"; /* static literal, installed after decode */
    expect_eq_int("borrowed free",
                  shrewd_basic_json_free(&target, &dfl_reflect, &dfl_json),
                  SHREWD_BASIC_ERR_OK);
    expect_true("borrowed survives free",
                (NULL != target.unit) && (0 == strcmp(target.unit, "kg")));
}

static void test_defaults_json(void)
{
    shrewd_basic_json_options_t options;
    dfl_t target;
    memset(&options, 0, sizeof(options));
    options.use_defaults = 1;

    /* Struct and array defaults materialize from JSON text; strings
       inside them are decode-allocated and freed by json_free. */
    memset(&target, 0, sizeof(target));
    expect_eq_int("defaults_json decode",
                  decode_into("{}", &target, &dfl_reflect, &dfl_json,
                              &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("defaults_json struct",
                (1 == target.pos.x) && (2 == target.pos.y));
    expect_streq("defaults_json nested str", target.pos.note, "hi");
    expect_true("defaults_json array",
                (7 == target.scores[0]) && (8 == target.scores[1])
                && (9 == target.scores[2]));
    expect_eq_int("defaults_json free",
                  shrewd_basic_json_free(&target, &dfl_reflect, &dfl_json),
                  SHREWD_BASIC_ERR_OK);
    expect_true("defaults_json nested str released", NULL == target.pos.note);

    /* A present key wins over the default entirely. */
    memset(&target, 0, sizeof(target));
    expect_eq_int("defaults_json override",
                  decode_into("{\"pos\":{\"x\":5}}", &target, &dfl_reflect,
                              &dfl_json, &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("defaults_json override pos",
                (5 == target.pos.x) && (0 == target.pos.y)
                && (NULL == target.pos.note));
    expect_true("defaults_json array kept", 7 == target.scores[0]);
    expect_eq_int("defaults_json override free",
                  shrewd_basic_json_free(&target, &dfl_reflect, &dfl_json),
                  SHREWD_BASIC_ERR_OK);

    /* Invalid default text surfaces as a syntax error. */
    {
        shrewd_basic_json_options_t zero;
        badfl_t bad;
        memset(&zero, 0, sizeof(zero));
        zero.use_defaults = 1;
        memset(&bad, 0, sizeof(bad));
        expect_eq_int("defaults_json bad text",
                      decode_into("{}", &bad, &badfl_reflect, &badfl_json,
                                  &zero, NULL),
                      SHREWD_BASIC_ERR_JSON_SYNTAX);
        expect_eq_int("defaults_json bad free",
                      shrewd_basic_json_free(&bad, &badfl_reflect, &badfl_json),
                      SHREWD_BASIC_ERR_OK);
    }
}

static void test_loose_keys(void)
{
    shrewd_basic_json_options_t options;
    cfg_t target;
    memset(&options, 0, sizeof(options));
    options.key_match = SHREWD_BASIC_JSON_KEY_MATCH_LOOSE;
    memset(&target, 0, sizeof(target));

    /* Case and underscores are ignored: AGE, NameID, SCORE all hit. */
    expect_eq_int("loose decode",
                  decode_into("{\"AGE\":7,\"NameID\":\"x\",\"score\":3}",
                              &target, &cfg_reflect, &cfg_json, &options, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("loose values",
                (7 == target.age) && (0 == strcmp(target.name, "x"))
                && (2 == target.score));
    expect_eq_int("loose free",
                  shrewd_basic_json_free(&target, &cfg_reflect, &cfg_json),
                  SHREWD_BASIC_ERR_OK);

    /* Colliding members under loose matching are refused outright. */
    {
        collide_t collide;
        memset(&collide, 0, sizeof(collide));
        expect_eq_int("loose collision",
                      decode_into("{\"name_id\":1}", &collide, &collide_reflect,
                                  NULL, &options, NULL),
                      SHREWD_BASIC_ERR_JSON_UNSUPPORTED);
    }
}

static void test_ops_targets(void)
{
    ext_t probe;
    void *block = NULL;
    int32_t storage = 41;

    /* Encode reads the pointee through the hook. */
    probe.handle = &storage;
    expect_encode("ops target encode", &probe, &ext_reflect, &ext_json,
                  "{\"handle\":41}");

    /* Decode lets the hook allocate the target; json_free releases it
       through ops->destroy. */
    g_handle_destroys = 0;
    (void)shrewd_basic_platform_calloc(&block, 1, sizeof(ext_t));
    expect_eq_int("ops target decode",
                  decode_into("{\"handle\":7}", block, &ext_reflect, &ext_json,
                              NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("ops target value",
                (NULL != ((ext_t *)block)->handle)
                && (7 == *((ext_t *)block)->handle));
    expect_eq_int("ops target free",
                  shrewd_basic_json_free(block, &ext_reflect, &ext_json),
                  SHREWD_BASIC_ERR_OK);
    expect_true("ops target destroy called", 1 == g_handle_destroys);
    (void)shrewd_basic_platform_free(&block);
}

/* Self-referential fixture for schema $ref cycles. */
typedef struct node {
    int32_t data;
    struct node *next;
} node_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(node_reflect, node_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(node_t, data),
    SHREWD_BASIC_REFLECT_FIELD_PTR(node_t, next, node_reflect));

/* --- schema + sizing tests -------------------------------------------- */

static void test_schema_export(void)
{
    char *text = NULL;
    expect_eq_int("schema build",
                  shrewd_basic_json_schema(&cfg_reflect, &cfg_json, &text, NULL,
                                           NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("schema doc",
                (NULL != text)
                && (NULL != strstr(text, "\"title\":\"cfg_t\""))
                && (NULL != strstr(text, "\"$defs\"")));
    expect_true("schema keys",
                (NULL != strstr(text, "\"name_id\""))
                && (NULL != strstr(text, "\"x-aliases\""))
                && (NULL != strstr(text, "\"NameID\"")));
    expect_true("schema required", NULL != strstr(text, "\"required\""));
    expect_true("schema bounds",
                (NULL != strstr(text, "\"minimum\""))
                && (NULL != strstr(text, "\"maximum\"")));
    expect_true("schema hidden skipped", NULL == strstr(text, "secret"));
    shrewd_basic_json_text_free(text);

    /* Nested structs become $defs references; unions become oneOf. */
    text = NULL;
    expect_eq_int("schema nested",
                  shrewd_basic_json_schema(&msg_reflect, NULL, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("schema nested refs",
                (NULL != text)
                && (NULL != strstr(text, "\"$defs\""))
                && (NULL != strstr(text, "point_t")));
    expect_true("schema union oneOf", NULL != strstr(text, "oneOf"));
    shrewd_basic_json_text_free(text);

    /* Self-referential types stay finite (node schema through $ref). */
    {
        char *cycle = NULL;
        expect_eq_int("schema cycle",
                      shrewd_basic_json_schema(&node_reflect, NULL, &cycle, NULL,
                                               NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("schema cycle ref",
                    (NULL != cycle) && (NULL != strstr(cycle, "#/$defs/node_t")));
        shrewd_basic_json_text_free(cycle);
    }

    expect_eq_int("schema mismatch",
                  shrewd_basic_json_schema(&msg_reflect, &cfg_json, &text, NULL,
                                           NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

/* --- element-scope ops fixture ---------------------------------------- */

typedef struct {
    int32_t values[3];
    int32_t *handles[2];
} elem_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(elem_reflect, elem_t,
    SHREWD_BASIC_REFLECT_FIELD_ARRAY(elem_t, values, shrewd_basic_reflect_int32),
    SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY(elem_t, handles, shrewd_basic_reflect_int32));

SHREWD_BASIC_JSON_DEFINE(elem_json, elem_reflect,
    SHREWD_BASIC_JSON_OPS_ELEM(values, g_score_ops),
    SHREWD_BASIC_JSON_OPS_ELEM(handles, g_handle_json_ops));

static void test_elem_ops(void)
{
    elem_t source;
    int32_t h1 = 10;
    int32_t h2 = 20;
    char *text = NULL;
    memset(&source, 0, sizeof(source));
    source.values[0] = 1;
    source.values[1] = 2;
    source.values[2] = 3;
    source.handles[0] = &h1;
    source.handles[1] = &h2;

    /* Encode runs the hooks per element. */
    expect_encode("elem ops encode", &source, &elem_reflect, &elem_json,
                  "{\"values\":[2,3,4],\"handles\":[10,20]}");

    /* Decode runs them per element; handles allocate targets. */
    g_handle_destroys = 0;
    {
        elem_t *target = NULL;
        void *block = NULL;
        (void)shrewd_basic_platform_calloc(&block, 1, sizeof(elem_t));
        target = block;
        expect_eq_int("elem ops decode",
                      decode_into("{\"values\":[2,3,4],\"handles\":[7,8]}", target,
                                  &elem_reflect, &elem_json, NULL, NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("elem ops decoded",
                    (1 == target->values[0]) && (3 == target->values[2])
                    && (NULL != target->handles[0])
                    && (7 == *target->handles[0])
                    && (8 == *target->handles[1]));
        expect_eq_int("elem ops free",
                      shrewd_basic_json_free(target, &elem_reflect, &elem_json),
                      SHREWD_BASIC_ERR_OK);
        expect_true("elem ops destroy x2", 2 == g_handle_destroys);
        (void)shrewd_basic_platform_free(&block);
    }

    /* Per-element check rejects bad elements with the index in the path. */
    {
        shrewd_basic_json_error_t error;
        elem_t target;
        memset(&target, 0, sizeof(target));
        expect_eq_int("elem ops check",
                      decode_into("{\"values\":[1,2,-2]}", &target, &elem_reflect,
                                  &elem_json, NULL, &error),
                      SHREWD_BASIC_ERR_ARG_INVALID);
        expect_true("elem ops check msg", NULL != strstr(error.message, "element"));
    }
    (void)text;
}

/* --- schema sync + decode_pointer ------------------------------------- */

static void test_schema_sync(void)
{
    char *text = NULL;
    shrewd_basic_json_info_t probe;

    /* FORMAT/PATTERN rows land in the schema as annotations. */
    {
        /* Build a one-off policy over cfg_reflect on the stack: the
           DEFINE macros are for globals, so exercise the rows through
           the existing cfg_json plus pattern/format in a tiny static. */
        static const shrewd_basic_json_field_policy_t extras[] = {
            SHREWD_BASIC_JSON_FORMAT(age, "int32"),
            SHREWD_BASIC_JSON_PATTERN(name, "^[a-z]+$") };
        memset(&probe, 0, sizeof(probe));
        probe.type = &cfg_reflect;
        probe.fields = extras;
        probe.field_count = sizeof(extras) / sizeof(extras[0]);
        expect_eq_int("schema annotations",
                      shrewd_basic_json_schema(&cfg_reflect, &probe, &text, NULL,
                                               NULL),
                      SHREWD_BASIC_ERR_OK);
        expect_true("schema annotation keys",
                    (NULL != strstr(text, "\"format\":\"int32\""))
                    && (NULL != strstr(text, "\"pattern\":\"^[a-z]+$\"")));
        shrewd_basic_json_text_free(text);
    }

    /* Union styles are recorded and UNWRAP emits bare values. */
    text = NULL;
    expect_eq_int("schema unwrap",
                  shrewd_basic_json_schema(&msg_reflect, &msg_unwrap_json, &text,
                                           NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("schema unwrap annotation",
                (NULL != strstr(text, "\"x-union-style\":\"unwrap\""))
                && (NULL != strstr(text, "oneOf")));
    shrewd_basic_json_text_free(text);
    text = NULL;
    expect_eq_int("schema flat",
                  shrewd_basic_json_schema(&msg_reflect, &msg_flat_json, &text,
                                           NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_true("schema flat annotation",
                NULL != strstr(text, "\"x-union-style\":\"flat\""));
    shrewd_basic_json_text_free(text);
}

static void test_decode_pointer(void)
{
    const char *doc =
            "{\"header\":{\"id\":5},\"items\":[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}]}";
    shrewd_basic_json_value_t *root = NULL;
    shrewd_basic_json_error_t error;
    point_t target;
    int32_t id = 0;

    expect_eq_int("decode pointer parse",
                  shrewd_basic_json_parse(doc, strlen(doc), &root, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    memset(&target, 0, sizeof(target));
    expect_eq_int("decode pointer sub",
                  shrewd_basic_json_decode_pointer(root, "/items/1", &target,
                                                   &point_reflect, NULL, NULL,
                                                   &error),
                  SHREWD_BASIC_ERR_OK);
    expect_true("decode pointer value", (3 == target.x) && (4 == target.y));
    /* Sub-tree decode allocates nothing for pure values; the header has
       no strings here either. */
    expect_eq_int("decode pointer scalar",
                  shrewd_basic_json_decode_pointer(root, "/header/id", &id,
                                                   &shrewd_basic_reflect_int32,
                                                   NULL, NULL, &error),
                  SHREWD_BASIC_ERR_OK);
    expect_true("decode pointer scalar value", 5 == id);
    expect_eq_int("decode pointer missing",
                  shrewd_basic_json_decode_pointer(root, "/zz", &target,
                                                   &point_reflect, NULL, NULL,
                                                   &error),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);
    shrewd_basic_json_value_free(root);
}

/* --- schema validator -------------------------------------------------- */

static void test_schema_check(void)
{
    char *text = NULL;
    shrewd_basic_json_value_t *schema = NULL;
    shrewd_basic_json_value_t *doc = NULL;
    shrewd_basic_json_error_t error;

    /* Export cfg's schema and re-parse it (the self-consistency loop). */
    expect_eq_int("check schema export",
                  shrewd_basic_json_schema(&cfg_reflect, &cfg_json, &text, NULL,
                                           NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check schema parse",
                  shrewd_basic_json_parse(text, strlen(text), &schema, NULL,
                                          NULL),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_json_text_free(text);

    /* A conforming document passes. */
    expect_eq_int("check doc parse",
                  shrewd_basic_json_parse("{\"age\":20,\"name\":\"ab\"}", strlen("{\"age\":20,\"name\":\"ab\"}"),
                                          &doc, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check ok",
                  shrewd_basic_json_schema_check(doc, schema, &error),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_json_value_free(doc);

    /* Wrong type. */
    expect_eq_int("check bad type parse",
                  shrewd_basic_json_parse("{\"age\":\"x\",\"name\":\"ab\"}", strlen("{\"age\":\"x\",\"name\":\"ab\"}"),
                                          &doc, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check bad type",
                  shrewd_basic_json_schema_check(doc, schema, &error),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    expect_streq("check bad type path", error.path, "/age");
    shrewd_basic_json_value_free(doc);

    /* Bounds and required. */
    expect_eq_int("check bounds parse",
                  shrewd_basic_json_parse("{\"age\":200,\"name\":\"ab\"}", strlen("{\"age\":200,\"name\":\"ab\"}"),
                                          &doc, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check bounds",
                  shrewd_basic_json_schema_check(doc, schema, &error),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    expect_true("check bounds msg",
                NULL != strstr(error.message, "maximum"));
    shrewd_basic_json_value_free(doc);
    expect_eq_int("check required parse",
                  shrewd_basic_json_parse("{\"age\":1}", strlen("{\"age\":1}"), &doc, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check required",
                  shrewd_basic_json_schema_check(doc, schema, &error),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    expect_streq("check required path", error.path, "/name_id");
    shrewd_basic_json_value_free(doc);
    shrewd_basic_json_value_free(schema);

    /* Union bodies discriminate through oneOf (+ required per variant). */
    expect_eq_int("check union schema",
                  shrewd_basic_json_schema(&msg_reflect, NULL, &text, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check union parse",
                  shrewd_basic_json_parse(text, strlen(text), &schema, NULL,
                                          NULL),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_json_text_free(text);
    expect_eq_int("check union doc",
                  shrewd_basic_json_parse("{\"kind\":\"MSG_POINT\",\"body\":{\"point\":{\"x\":1,\"y\":2}}}", strlen("{\"kind\":\"MSG_POINT\",\"body\":{\"point\":{\"x\":1,\"y\":2}}}"), &doc, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check union ok",
                  shrewd_basic_json_schema_check(doc, schema, &error),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_json_value_free(doc);
    expect_eq_int("check union bad parse",
                  shrewd_basic_json_parse("{\"kind\":\"MSG_POINT\",\"body\":{\"nope\":1}}", strlen("{\"kind\":\"MSG_POINT\",\"body\":{\"nope\":1}}"), &doc, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("check union bad",
                  shrewd_basic_json_schema_check(doc, schema, &error),
                  SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH);
    shrewd_basic_json_value_free(doc);
    shrewd_basic_json_value_free(schema);
}

int main(void)
{
    test_key_aliases();
    test_use_defaults();
    test_defaults_json();
    test_loose_keys();
    test_ops_targets();
    test_elem_ops();
    test_required_range_check();
    test_union_styles();
    test_keys_export();
    test_schema_export();
    test_schema_sync();
    test_decode_pointer();
    test_schema_check();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

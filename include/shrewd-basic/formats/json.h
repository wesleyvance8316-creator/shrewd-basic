#ifndef SHREWD_BASIC_JSON_JSON_H
#define SHREWD_BASIC_JSON_JSON_H

/**
 * @file json.h
 * @brief JSON value model, parser and serializer (RFC 8259).
 *
 * The json module is layered:
 *
 *   - this layer: a general JSON value model (DOM) - parse text into a
 *     value tree, navigate/build/modify it, serialize it back;
 *   - the mapping layer (added on top): reflect-described structs
 *     encode to and decode from these values / text.
 *
 * Numbers keep their exact source token ("1.0" stays "1.0"), so parse
 * then serialize is text-identical; typed accessors parse on demand.
 * Created numbers are rendered canonically (shortest form that round-
 * trips for doubles). Strings are stored decoded (UTF-8) and re-escaped
 * on output.
 *
 * Parsing follows RFC 8259 strictly by default: no comments, no trailing
 * commas, no single quotes; options allow relaxing some of that. An
 * object preserves its member order (and even duplicate keys, which
 * get() resolves to the first match). Nesting is capped (32 by default).
 *
 * Ownership: every value created or parsed belongs to the caller and is
 * released with shrewd_basic_json_value_free() (frees the whole tree).
 * shrewd_basic_json_set/append take ownership of the item they store;
 * shrewd_basic_json_serialize allocates the text, released with
 * shrewd_basic_json_text_free().
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Value kinds. */
typedef enum shrewd_basic_json_kind {
    SHREWD_BASIC_JSON_NULL = 0,
    SHREWD_BASIC_JSON_BOOL,
    SHREWD_BASIC_JSON_NUMBER,
    SHREWD_BASIC_JSON_STRING,
    SHREWD_BASIC_JSON_ARRAY,
    SHREWD_BASIC_JSON_OBJECT
} shrewd_basic_json_kind_t;

/** One node of a JSON value tree (opaque). */
typedef struct shrewd_basic_json_value shrewd_basic_json_value_t;

/** Per-type JSON policy (defined by SHREWD_BASIC_JSON_DEFINE; optional). */
typedef struct shrewd_basic_json_info shrewd_basic_json_info_t;

/** Default nesting limit for parse/serialize. */
#define SHREWD_BASIC_JSON_DEPTH_DEFAULT 32

/**
 * Options for parse/serialize. Every zero field means the documented
 * default, so a zeroed struct (or NULL) gives strict RFC 8259 and
 * compact output.
 */
typedef struct shrewd_basic_json_options {
    int pretty;                /**< serialize: indented output. */
    int indent;                /**< serialize: spaces per level (0 = 2). */
    int max_depth;             /**< parse/serialize: nesting limit (0 = 32). */
    int allow_comments;        /**< parse: accept // line and /_ block comments. */
    int allow_trailing_commas; /**< parse: accept [1,] and {"a":1,}. */
    int allow_nan;             /**< encode: NaN/inf become null (default: error). */
    int allow_unknown_union;   /**< encode: unknown union tag -> null body. */
    int strict;                /**< decode: unknown keys and alias conflicts error. */
    int use_defaults;          /**< decode: missing keys materialize DEFAULT values. */
    int key_match;             /**< decode: SHREWD_BASIC_JSON_KEY_MATCH_*. */
    int escape_unicode;        /**< serialize: non-ASCII as \\uXXXX escapes. */
    int validate_utf8;         /**< parse: reject invalid UTF-8 in strings. */
    int skip_bom;              /**< parse: ignore one leading UTF-8 BOM (text sources). */
} shrewd_basic_json_options_t;

/** Key matching strategies for decode. */
#define SHREWD_BASIC_JSON_KEY_MATCH_EXACT 0 /**< Accepted keys only (default). */
#define SHREWD_BASIC_JSON_KEY_MATCH_LOOSE 1 /**< Also ignore case and '_'. */

/**
 * Fault detail for parse/serialize calls: @c offset is the byte offset
 * of the problem in the input, @c message a short description. The
 * mapping layer also fills @c path with the JSON path of the value at
 * fault.
 */
typedef struct shrewd_basic_json_error {
    size_t offset;
    char path[256];
    char message[128];
} shrewd_basic_json_error_t;

/* --- value model ------------------------------------------------------ */

/** Create a null/array/object value (@p kind must be one of those three). */
int shrewd_basic_json_new(shrewd_basic_json_kind_t kind, shrewd_basic_json_value_t **out);

/** Create a boolean value. */
int shrewd_basic_json_new_bool(bool value, shrewd_basic_json_value_t **out);

/** Create an integer number (rendered without fraction). */
int shrewd_basic_json_new_i64(int64_t value, shrewd_basic_json_value_t **out);

/** Create an unsigned number (full 64-bit digits). */
int shrewd_basic_json_new_u64(uint64_t value, shrewd_basic_json_value_t **out);

/**
 * Create a floating number. Non-finite values (NaN, infinities) have no
 * JSON representation and fail with SHREWD_BASIC_ERR_ARG_INVALID.
 */
int shrewd_basic_json_new_f64(double value, shrewd_basic_json_value_t **out);

/** Create a string value (copies @p text; NULL becomes ""). */
int shrewd_basic_json_new_string(const char *text, shrewd_basic_json_value_t **out);

/** Create a string value from @p n bytes (copies; no NUL needed). */
int shrewd_basic_json_new_string_n(const char *text, size_t n, shrewd_basic_json_value_t **out);

/** Release a value tree (NULL is accepted). */
void shrewd_basic_json_value_free(shrewd_basic_json_value_t *value);

/**
 * Deep copy @p value into a new tree. The clone is text-faithful:
 * numbers keep their exact token text (no re-canonicalization) and
 * objects keep member order including duplicate keys. @p out receives
 * ownership - release with shrewd_basic_json_value_free().
 */
int shrewd_basic_json_value_clone(const shrewd_basic_json_value_t *value,
                                  shrewd_basic_json_value_t **out);

/* --- inspection ------------------------------------------------------- */

/** Kind of @p value (SHREWD_BASIC_JSON_NULL for NULL). */
shrewd_basic_json_kind_t shrewd_basic_json_kind(const shrewd_basic_json_value_t *value);

/** Members of an object or elements of an array (0 otherwise). */
size_t shrewd_basic_json_count(const shrewd_basic_json_value_t *value);

/** Element/member value at @p index, or NULL out of range. */
shrewd_basic_json_value_t *shrewd_basic_json_at(const shrewd_basic_json_value_t *value,
                                                size_t index);

/** Key of the object member at @p index, or NULL out of range. */
const char *shrewd_basic_json_key_at(const shrewd_basic_json_value_t *value, size_t index);

/** First object member named @p key, or NULL (duplicate keys: first wins). */
shrewd_basic_json_value_t *shrewd_basic_json_get(const shrewd_basic_json_value_t *value,
                                                 const char *key);

/**
 * Typed views. The to_i64/to_u64/to_f64 readers accept any number
 * token whose value fits the target (a 1.0 token reads as integer 1);
 * they fail with SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH on non-numbers and
 * out-of-range values. to_string returns the content (NULL unless the
 * value is a string). number_text returns the exact token.
 */
int shrewd_basic_json_to_bool(const shrewd_basic_json_value_t *value, bool *out);
int shrewd_basic_json_to_i64(const shrewd_basic_json_value_t *value, int64_t *out);
int shrewd_basic_json_to_u64(const shrewd_basic_json_value_t *value, uint64_t *out);
int shrewd_basic_json_to_f64(const shrewd_basic_json_value_t *value, double *out);
const char *shrewd_basic_json_to_string(const shrewd_basic_json_value_t *value);
const char *shrewd_basic_json_number_text(const shrewd_basic_json_value_t *value);

/* --- mutation --------------------------------------------------------- */

/**
 * Store @p item in @p object under @p key (ownership passes to the
 * object). An existing first member with the key is replaced (its old
 * value is freed).
 */
int shrewd_basic_json_set(shrewd_basic_json_value_t *object,
                          const char *key,
                          shrewd_basic_json_value_t *item);

/** Append @p item to @p array (ownership passes to the array). */
int shrewd_basic_json_append(shrewd_basic_json_value_t *array, shrewd_basic_json_value_t *item);

/** Remove the first object member named @p key (frees its value). */
int shrewd_basic_json_remove(shrewd_basic_json_value_t *value, const char *key);

/** Remove the element/member at @p index (frees its value). */
int shrewd_basic_json_remove_at(shrewd_basic_json_value_t *value, size_t index);

/**
 * Replace the array element (or object member) at @p index with @p item,
 * freeing the previous value. Ownership of @p item passes in on success.
 */
int shrewd_basic_json_replace_at(shrewd_basic_json_value_t *value,
                                 size_t index,
                                 shrewd_basic_json_value_t *item);

/* --- text I/O --------------------------------------------------------- */

/**
 * Parse JSON text into a new value tree. Strict RFC 8259 unless the
 * options relax it. Returns SHREWD_BASIC_ERR_JSON_SYNTAX for malformed
 * text and SHREWD_BASIC_ERR_JSON_DEPTH past the nesting limit; @p error
 * (optional) receives the byte offset and a description.
 */
int shrewd_basic_json_parse(const char *text,
                            size_t length,
                            shrewd_basic_json_value_t **out,
                            const shrewd_basic_json_options_t *options,
                            shrewd_basic_json_error_t *error);

/** Sink callback for streaming output; return non-zero on I/O failure. */
typedef int (*shrewd_basic_json_write_fn)(void *ctx, const char *data, size_t length);

/**
 * Serialize @p value through a writer callback (the streaming core:
 * files, sockets and collectors all sit behind it). The text/FILE
 * wrappers below are conveniences over this.
 */
int shrewd_basic_json_serialize_to(const shrewd_basic_json_value_t *value,
                                   shrewd_basic_json_write_fn write,
                                   void *ctx,
                                   const shrewd_basic_json_options_t *options,
                                   shrewd_basic_json_error_t *error);

/** Encode a described object straight through a writer callback. */
int shrewd_basic_json_encode_to(const void *object,
                                const shrewd_basic_reflect_type_t *type,
                                const shrewd_basic_json_info_t *info,
                                shrewd_basic_json_write_fn write,
                                void *ctx,
                                const shrewd_basic_json_options_t *options,
                                shrewd_basic_json_error_t *error);

/** shrewd_basic_json_write_fn that writes into a FILE (@c ctx). */
int shrewd_basic_json_write_file(void *ctx, const char *data, size_t length);

/**
 * Serialize @p value to a newly allocated NUL-terminated string
 * (compact unless options say pretty). The only failure besides
 * allocation is SHREWD_BASIC_ERR_JSON_DEPTH past the nesting limit;
 * release the text with shrewd_basic_json_text_free().
 */
int shrewd_basic_json_serialize(const shrewd_basic_json_value_t *value,
                                char **out_text,
                                const shrewd_basic_json_options_t *options,
                                shrewd_basic_json_error_t *error);

/** Release text produced by shrewd_basic_json_serialize(). */
void shrewd_basic_json_text_free(char *text);

/* --- pull tokenizer (the streaming parse core) ------------------------ */

/*
 * The grammar lives in one place: shrewd_basic_json_parse() is this
 * pull parser plus a DOM builder, and applications drive the same
 * tokenizer directly for documents too large to materialize. A token's
 * text stays valid until the next shrewd_basic_json_pull_next() /
 * _capture() call.
 */
typedef struct shrewd_basic_json_pull shrewd_basic_json_pull_t;

typedef enum shrewd_basic_json_token_kind {
    SHREWD_BASIC_JSON_TOKEN_END = 0, /**< No more tokens. */
    SHREWD_BASIC_JSON_TOKEN_OBJECT_BEGIN,
    SHREWD_BASIC_JSON_TOKEN_OBJECT_END,
    SHREWD_BASIC_JSON_TOKEN_ARRAY_BEGIN,
    SHREWD_BASIC_JSON_TOKEN_ARRAY_END,
    SHREWD_BASIC_JSON_TOKEN_KEY,  /**< @c text = the key. */
    SHREWD_BASIC_JSON_TOKEN_VALUE /**< A scalar: see @c value_kind. */
} shrewd_basic_json_token_kind_t;

typedef struct shrewd_basic_json_token {
    shrewd_basic_json_token_kind_t kind;
    shrewd_basic_json_kind_t value_kind; /**< For TOKEN_VALUE. */
    const char *text;                    /**< Key / string / number token. */
    size_t length;
} shrewd_basic_json_token_t;

/**
 * Streaming source: fill up to @p capacity bytes into @p buf; report
 * the count in @p out_read (0 means end of input).
 */
typedef int (*shrewd_basic_json_read_fn)(void *ctx, char *buf, size_t capacity, size_t *out_read);

/** Open a pull parser over a complete text buffer. */
int shrewd_basic_json_pull_open(shrewd_basic_json_pull_t **out,
                                const char *text,
                                size_t length,
                                const shrewd_basic_json_options_t *options,
                                shrewd_basic_json_error_t *error);

/** Open a pull parser over a streaming reader source. */
int shrewd_basic_json_pull_open_reader(shrewd_basic_json_pull_t **out,
                                       shrewd_basic_json_read_fn read,
                                       void *ctx,
                                       const shrewd_basic_json_options_t *options,
                                       shrewd_basic_json_error_t *error);

/** Fetch the next token (SHREWD_BASIC_JSON_TOKEN_END at the end). */
int shrewd_basic_json_pull_next(shrewd_basic_json_pull_t *pull,
                                shrewd_basic_json_token_t *out_token);

/**
 * Capture the value that starts at the current position into a new DOM
 * subtree (the parser consumes exactly that value's tokens).
 */
int shrewd_basic_json_pull_capture(shrewd_basic_json_pull_t *pull, shrewd_basic_json_value_t **out);

/** Release a pull parser. */
void shrewd_basic_json_pull_close(shrewd_basic_json_pull_t *pull);

/* --- struct mapping (reflect-described graphs) ------------------------ */

/*
 * Wire format for described structs (see docs/json.md):
 *   - keys are member names, emitted in descriptor order;
 *   - enums render as constant names (numbers accepted both ways);
 *   - bit groups render as their extracted number;
 *   - char members render as one-character strings;
 *   - all array shapes render as JSON arrays; multi-dimension rows nest;
 *   - pointers are followed (NULL -> null); cyclic graphs fail with
 *     SHREWD_BASIC_ERR_JSON_UNSUPPORTED (JSON cannot express cycles);
 *   - a tagged union renders as F1 "nest-preserving" form: the tag
 *     member (a described field) is its own key, the union member is an
 *     object with the active variant's fields as keys
 *     ("body": {"point": {...}}). Unknown tags fail unless
 *     options.allow_unknown_union (then the body is null);
 *   - opaque targets render through ops->render as strings, else fail.
 */

/**
 * Encode a described object into a new value tree. Errors carry the
 * JSON path of the fault in @p error->path (e.g. "body.point.x").
 */
int shrewd_basic_json_encode_value(const void *object,
                                   const shrewd_basic_reflect_type_t *type,
                                   const shrewd_basic_json_info_t *info,
                                   shrewd_basic_json_value_t **out,
                                   const shrewd_basic_json_options_t *options,
                                   shrewd_basic_json_error_t *error);

/** Encode a described object straight to JSON text. */
int shrewd_basic_json_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_info_t *info,
                             char **out_text,
                             const shrewd_basic_json_options_t *options,
                             shrewd_basic_json_error_t *error);

/*
 * Decode rules (see docs/json.md): @p object is zeroed first (missing
 * keys keep zero defaults), then JSON members fill fields. Arrays may
 * be shorter than a fixed slot (the tail stays zero) but not longer;
 * count members are derived from array lengths (JSON count keys are
 * ignored). Strings and pointer targets are allocated through
 * platform_alloc; shrewd_basic_json_free() releases exactly what decode
 * allocated - every string, pointer target and dynamic table. The
 * storage of @p object itself belongs to the caller (decode never
 * allocates it). Flexible-tailed roots must be sized by the caller for
 * the JSON tail: sizeof(T) + n * sizeof(elem).
 */
int shrewd_basic_json_decode_value(const shrewd_basic_json_value_t *value,
                                   void *object,
                                   const shrewd_basic_reflect_type_t *type,
                                   const shrewd_basic_json_info_t *info,
                                   const shrewd_basic_json_options_t *options,
                                   shrewd_basic_json_error_t *error);

/** Parse text and decode it into @p object (zeroed first). */
int shrewd_basic_json_decode(const char *text,
                             size_t length,
                             void *object,
                             const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_info_t *info,
                             const shrewd_basic_json_options_t *options,
                             shrewd_basic_json_error_t *error);

/**
 * Release everything shrewd_basic_json_decode() allocated inside @p
 * object (strings, pointer targets, dynamic tables). The storage of
 * @p object itself belongs to the caller and is not freed. @p info (may
 * be NULL) supplies the policy so ops->destroy hooks can release their
 * custom pointer targets.
 */
int shrewd_basic_json_free(void *object,
                           const shrewd_basic_reflect_type_t *type,
                           const shrewd_basic_json_info_t *info);

/* --- policy layer (optional, per-type) ------------------------------- */

/*
 * Go-struct-tag style configuration for the mapping layer, authored
 * with the macros below and passed to encode/decode as @c info (NULL
 * means "plain reflect metadata"). Policy rows are keyed by member
 * name and merge per aspect (the later row wins); the structural truth
 * always stays in the reflect descriptor - policy only shapes text.
 */

/** Union body styles for SHREWD_BASIC_JSON_UNION_STYLE(). */
#define SHREWD_BASIC_JSON_UNION_NESTED 0 /**< F1: body = {"variant": {...}}. */
#define SHREWD_BASIC_JSON_UNION_FLAT 1   /**< F4: variant fields at the parent. */
#define SHREWD_BASIC_JSON_UNION_UNWRAP 2 /**< F2: single-field variant unwraps. */

/** Field policy flags (combined across rows). */
#define SHREWD_BASIC_JSON_F_OMIT_EMPTY ((uint32_t)1u) /**< encode: skip zero values. */
#define SHREWD_BASIC_JSON_F_REQUIRED ((uint32_t)2u)   /**< decode: missing key errors. */
#define SHREWD_BASIC_JSON_F_HIDDEN ((uint32_t)4u)     /**< never encoded or decoded. */
#define SHREWD_BASIC_JSON_F_RANGE ((uint32_t)8u)      /**< value must sit in [min, max]. */
#define SHREWD_BASIC_JSON_F_BORROWED                                                               \
    ((uint32_t)16u) /**< pointer is app-owned: never decoded or freed. */

/** How a DEFAULT row stores its value. */
#define SHREWD_BASIC_JSON_DEF_NONE 0 /**< No default declared. */
#define SHREWD_BASIC_JSON_DEF_NUM 1  /**< Numeric (def_u bits + def_d value). */
#define SHREWD_BASIC_JSON_DEF_STR 2  /**< String literal (def_s). */
#define SHREWD_BASIC_JSON_DEF_JSON 3 /**< JSON text (def_s), any shape. */

/**
 * Application callbacks for one field (JSON-flavored counterpart of the
 * reflect ops). encode/decode replace the metadata-driven mapping of
 * that field's value; check runs after decode (and before encode).
 */
typedef struct shrewd_basic_json_ops {
    int (*encode)(const void *value, shrewd_basic_json_value_t **out);
    /**
     * Decode one value. For value fields @p target is the field storage;
     * for pointer fields it is the pointer slot (the ops allocates the
     * target and writes its address, mirroring encode's pointee view).
     */
    int (*decode)(const shrewd_basic_json_value_t *value, void *target);
    int (*check)(const void *value);
    /** Release a pointer target produced by @c decode (json_free uses it). */
    void (*destroy)(void *target);
} shrewd_basic_json_ops_t;

/** One merged policy view of a field (also the json_keys() callback unit). */
typedef struct shrewd_basic_json_key_info {
    const char *field;       /**< Member name. */
    const char *const *keys; /**< Accepted keys, canonical first. */
    size_t key_count;        /**< Length of @c keys (>= 1). */
    uint32_t flags;          /**< SHREWD_BASIC_JSON_F_*. */
    double min;              /**< RANGE bounds (with F_RANGE). */
    double max;
    const shrewd_basic_json_ops_t *ops; /**< Custom hooks, or NULL. */
    int elem_ops;                       /**< The hooks apply per element (arrays). */
    int union_style;                    /**< SHREWD_BASIC_JSON_UNION_*. */
    const char *format;                 /**< Schema "format" annotation, or NULL. */
    const char *pattern;                /**< Schema "pattern" annotation, or NULL. */
    int def_kind;                       /**< SHREWD_BASIC_JSON_DEF_*. */
    uint64_t def_u;                     /**< DEF_NUM: integer bit pattern. */
    double def_d;                       /**< DEF_NUM: floating value. */
    const char *def_s;                  /**< DEF_STR: the literal. */
} shrewd_basic_json_key_info_t;

/** Per-field policy row (produced by the macros below). */
typedef struct shrewd_basic_json_field_policy {
    const char *field;
    const char *const *keys;
    size_t key_count;
    uint32_t flags;
    double min;
    double max;
    const shrewd_basic_json_ops_t *ops;
    int elem_ops;
    int union_style;
    int has_union_style;
    const char *format;
    const char *pattern;
    int def_kind;
    uint64_t def_u;
    double def_d;
    const char *def_s;
} shrewd_basic_json_field_policy_t;

/** Per-type policy: what SHREWD_BASIC_JSON_DEFINE() produces. */
struct shrewd_basic_json_info {
    const shrewd_basic_reflect_type_t *type;
    const shrewd_basic_json_field_policy_t *fields;
    size_t field_count;
};

/*
 * Register a type's JSON policy: `var` embeds `type_ref` and the rows
 * configure keys/flags/ranges/hooks per member. Define once per module,
 * import with SHREWD_BASIC_REFLECT_TYPE_DECLARE-style extern (the
 * object is extern like the reflect descriptors).
 */
#define SHREWD_BASIC_JSON_DEFINE(var, type_ref, ...)                                               \
    extern const shrewd_basic_json_info_t var;                                                     \
    static const shrewd_basic_json_field_policy_t var##_policies[] = {__VA_ARGS__};                \
    const shrewd_basic_json_info_t var                                                             \
        = {.type = &(type_ref),                                                                    \
           .fields = var##_policies,                                                               \
           .field_count = sizeof(var##_policies) / sizeof(var##_policies[0])}

/** Import a policy defined by SHREWD_BASIC_JSON_DEFINE() elsewhere. */
#define SHREWD_BASIC_JSON_DECLARE(var) extern const shrewd_basic_json_info_t var

/*
 * Key aliases: the first name is the canonical key (encode emits it),
 * the rest are accepted on decode. Later KEY rows for the same member
 * replace earlier ones.
 */
#define SHREWD_BASIC_JSON_KEY(member, ...)                                                         \
    {.field = #member,                                                                             \
     .keys = (const char *const[]){__VA_ARGS__, NULL},                                             \
     .key_count = (sizeof((const char *const[]){__VA_ARGS__, NULL}) / sizeof(const char *)) - 1,   \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/** Skip a zero-valued field on encode (false/0/""/NULL/empty containers). */
#define SHREWD_BASIC_JSON_OMIT_EMPTY(member)                                                       \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = SHREWD_BASIC_JSON_F_OMIT_EMPTY,                                                      \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/**
 * The key must be present on decode - or materialized from a DEFAULT row
 * when use_defaults is on.
 */
#define SHREWD_BASIC_JSON_REQUIRED(member)                                                         \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = SHREWD_BASIC_JSON_F_REQUIRED,                                                        \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/**
 * Default value materialized when the key is missing and the decode
 * runs with use_defaults (integers, bools, chars, enums and floats via
 * one numeric slot; signed values keep their bit pattern). Scalars
 * only - struct/array defaults use SHREWD_BASIC_JSON_DEFAULT_JSON().
 * A materialized default satisfies REQUIRED.
 */
#define SHREWD_BASIC_JSON_DEFAULT(member, value)                                                   \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL,                                                                              \
     .def_kind = SHREWD_BASIC_JSON_DEF_NUM,                                                        \
     .def_u = (uint64_t)(value),                                                                   \
     .def_d = (double)(value),                                                                     \
     .def_s = NULL}

/**
 * String default (a C literal) materialized on missing keys under
 * use_defaults: decode allocates and copies the text, so json_free
 * releases it like any decoded string.
 */
#define SHREWD_BASIC_JSON_DEFAULT_STR(member, text)                                                \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL,                                                                              \
     .def_kind = SHREWD_BASIC_JSON_DEF_STR,                                                        \
     .def_u = 0,                                                                                   \
     .def_d = 0.0,                                                                                 \
     .def_s = (text)}

/**
 * Default as raw JSON text (a C literal), materialized on missing keys
 * under use_defaults: decode parses the text and runs the field's normal
 * mapping over it, so any shape works - struct/array defaults included
 * (phase two). Strings and pointer targets inside the default are
 * decode-allocated and json_free releases them like decoded values.
 * Flexible-tail (FAM) fields are skipped (the caller sizes the root for
 * the JSON that is present, not for defaults).
 */
#define SHREWD_BASIC_JSON_DEFAULT_JSON(member, text)                                               \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL,                                                                              \
     .def_kind = SHREWD_BASIC_JSON_DEF_JSON,                                                       \
     .def_u = 0,                                                                                   \
     .def_d = 0.0,                                                                                 \
     .def_s = (text)}

/**
 * The pointer field is application-owned: decode never writes it and
 * json_free never releases it. Assign it after decoding (decode zeroes
 * the target first) and hand it whatever lifetime you like - a literal,
 * a borrowed buffer, your own allocation.
 */
#define SHREWD_BASIC_JSON_BORROWED(member)                                                         \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = SHREWD_BASIC_JSON_F_BORROWED,                                                        \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/** The field never appears in JSON (neither direction). */
#define SHREWD_BASIC_JSON_HIDDEN(member)                                                           \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = SHREWD_BASIC_JSON_F_HIDDEN,                                                          \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/** Numeric bounds checked on encode and decode. */
#define SHREWD_BASIC_JSON_RANGE(member, lo, hi)                                                    \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = SHREWD_BASIC_JSON_F_RANGE,                                                           \
     .min = (double)(lo),                                                                          \
     .max = (double)(hi),                                                                          \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/** Custom encode/decode/check hooks for one field. */
#define SHREWD_BASIC_JSON_OPS(member, ops_ref)                                                     \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = &(ops_ref),                                                                            \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0}

/** Schema "format" annotation (e.g. "date-time"); annotation only. */
#define SHREWD_BASIC_JSON_FORMAT(member, text)                                                     \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = (text),                                                                             \
     .pattern = NULL}

/** Schema "pattern" annotation; annotation only (no regex engine). */
#define SHREWD_BASIC_JSON_PATTERN(member, text)                                                    \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = (text)}

/** Custom hooks applied PER ELEMENT of an array-shaped field. */
#define SHREWD_BASIC_JSON_OPS_ELEM(member, ops_ref)                                                \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = &(ops_ref),                                                                            \
     .elem_ops = 1,                                                                                \
     .union_style = 0,                                                                             \
     .has_union_style = 0,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/** Pick the union body style for one union field. */
#define SHREWD_BASIC_JSON_UNION_STYLE(member, style)                                               \
    {.field = #member,                                                                             \
     .keys = NULL,                                                                                 \
     .key_count = 0,                                                                               \
     .flags = 0,                                                                                   \
     .min = 0.0,                                                                                   \
     .max = 0.0,                                                                                   \
     .ops = NULL,                                                                                  \
     .elem_ops = 0,                                                                                \
     .union_style = (int)(style),                                                                  \
     .has_union_style = 1,                                                                         \
     .format = NULL,                                                                               \
     .pattern = NULL}

/**
 * Query a value by RFC 6901 JSON Pointer ("/a/b/0"; "" selects the root,
 * "~0"/"~1" escape '~' and '/'). Array segments index, object segments
 * match keys. Returns SHREWD_BASIC_ERR_KEY_NOTFOUND for a missing step
 * and SHREWD_BASIC_ERR_ARG_INVALID for a malformed pointer.
 */
int shrewd_basic_json_pointer(const shrewd_basic_json_value_t *root,
                              const char *pointer,
                              const shrewd_basic_json_value_t **out);

/**
 * Store @p item where @p pointer leads (RFC 6901 syntax; "-" appends to
 * an array). The intermediate steps must already exist; the last step
 * replaces an existing member/element or appends to an object. On
 * failure @p item stays owned by the caller.
 */
int shrewd_basic_json_pointer_set(shrewd_basic_json_value_t *root,
                                  const char *pointer,
                                  shrewd_basic_json_value_t *item);

/**
 * Decode the sub-document at @p pointer into @p object (the mapping
 * rules of shrewd_basic_json_decode_value() apply to that subtree).
 */
int shrewd_basic_json_decode_pointer(const shrewd_basic_json_value_t *value,
                                     const char *pointer,
                                     void *object,
                                     const shrewd_basic_reflect_type_t *type,
                                     const shrewd_basic_json_info_t *info,
                                     const shrewd_basic_json_options_t *options,
                                     shrewd_basic_json_error_t *error);

/**
 * Export a JSON Schema (draft-07 subset) for a described type: object
 * properties from the fields, "required" from REQUIRED policy, bounds
 * from RANGE, "x-aliases" from KEY rows, enums from ENUM descriptors,
 * nested structs as $defs references, unions as oneOf per variant.
 * Release the text with shrewd_basic_json_text_free().
 */
int shrewd_basic_json_schema(const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_info_t *info,
                             char **out_text,
                             const shrewd_basic_json_options_t *options,
                             shrewd_basic_json_error_t *error);

/**
 * Validate a document against a schema (as produced by
 * shrewd_basic_json_schema()): type/enum/minimum/maximum/maxLength/
 * maxItems/required/properties/items/oneOf/anyOf/$ref into $defs. The
 * error carries the document path of the first violation. format and
 * pattern pass through unvalidated (no regex engine).
 */
int shrewd_basic_json_schema_check(const shrewd_basic_json_value_t *document,
                                   const shrewd_basic_json_value_t *schema,
                                   shrewd_basic_json_error_t *error);

/**
 * Allocation size a decode target needs: the struct header plus the
 * flexible tails the JSON carries (the FAM sizing helper).
 */
int shrewd_basic_json_value_size(const shrewd_basic_json_value_t *value,
                                 const shrewd_basic_reflect_type_t *type,
                                 size_t *out_size);

/** Callback of shrewd_basic_json_keys(); return non-zero to stop early. */
typedef int (*shrewd_basic_json_keys_fn)(void *ctx, const shrewd_basic_json_key_info_t *key);

/**
 * Enumerate the JSON keys of a type in descriptor order (the schema
 * export): each callback receives one field's merged policy view -
 * key aliases, flags, range and hooks. @p info may be NULL.
 */
int shrewd_basic_json_keys(const shrewd_basic_reflect_type_t *type,
                           const shrewd_basic_json_info_t *info,
                           shrewd_basic_json_keys_fn fn,
                           void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_JSON_JSON_H */

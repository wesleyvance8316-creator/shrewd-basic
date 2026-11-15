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

/** Default nesting limit for parse/serialize. */
#define SHREWD_BASIC_JSON_DEPTH_DEFAULT 32

/**
 * Options for parse/serialize. Every zero field means the documented
 * default, so a zeroed struct (or NULL) gives strict RFC 8259 and
 * compact output.
 */
typedef struct shrewd_basic_json_options {
    int pretty;               /**< serialize: indented output. */
    int indent;               /**< serialize: spaces per level (0 = 2). */
    int max_depth;            /**< parse/serialize: nesting limit (0 = 32). */
    int allow_comments;       /**< parse: accept // line and /_ block comments. */
    int allow_trailing_commas;/**< parse: accept [1,] and {"a":1,}. */
} shrewd_basic_json_options_t;

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
int shrewd_basic_json_new(shrewd_basic_json_kind_t kind,
                          shrewd_basic_json_value_t **out);

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
int shrewd_basic_json_new_string(const char *text,
                                 shrewd_basic_json_value_t **out);

/** Create a string value from @p n bytes (copies; no NUL needed). */
int shrewd_basic_json_new_string_n(const char *text, size_t n,
                                   shrewd_basic_json_value_t **out);

/** Release a value tree (NULL is accepted). */
void shrewd_basic_json_value_free(shrewd_basic_json_value_t *value);

/* --- inspection ------------------------------------------------------- */

/** Kind of @p value (SHREWD_BASIC_JSON_NULL for NULL). */
shrewd_basic_json_kind_t shrewd_basic_json_kind(
        const shrewd_basic_json_value_t *value);

/** Members of an object or elements of an array (0 otherwise). */
size_t shrewd_basic_json_count(const shrewd_basic_json_value_t *value);

/** Element/member value at @p index, or NULL out of range. */
shrewd_basic_json_value_t *shrewd_basic_json_at(
        const shrewd_basic_json_value_t *value, size_t index);

/** Key of the object member at @p index, or NULL out of range. */
const char *shrewd_basic_json_key_at(const shrewd_basic_json_value_t *value,
                                     size_t index);

/** First object member named @p key, or NULL (duplicate keys: first wins). */
shrewd_basic_json_value_t *shrewd_basic_json_get(
        const shrewd_basic_json_value_t *value, const char *key);

/**
 * Typed views. The to_i64/to_u64/to_f64 readers accept any number
 * token whose value fits the target (a 1.0 token reads as integer 1);
 * they fail with SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH on non-numbers and
 * out-of-range values. to_string returns the content (NULL unless the
 * value is a string). number_text returns the exact token.
 */
int shrewd_basic_json_to_bool(const shrewd_basic_json_value_t *value,
                              bool *out);
int shrewd_basic_json_to_i64(const shrewd_basic_json_value_t *value,
                             int64_t *out);
int shrewd_basic_json_to_u64(const shrewd_basic_json_value_t *value,
                             uint64_t *out);
int shrewd_basic_json_to_f64(const shrewd_basic_json_value_t *value,
                             double *out);
const char *shrewd_basic_json_to_string(const shrewd_basic_json_value_t *value);
const char *shrewd_basic_json_number_text(const shrewd_basic_json_value_t *value);

/* --- mutation --------------------------------------------------------- */

/**
 * Store @p item in @p object under @p key (ownership passes to the
 * object). An existing first member with the key is replaced (its old
 * value is freed).
 */
int shrewd_basic_json_set(shrewd_basic_json_value_t *object, const char *key,
                          shrewd_basic_json_value_t *item);

/** Append @p item to @p array (ownership passes to the array). */
int shrewd_basic_json_append(shrewd_basic_json_value_t *array,
                             shrewd_basic_json_value_t *item);

/** Remove the first object member named @p key (frees its value). */
int shrewd_basic_json_remove(shrewd_basic_json_value_t *value, const char *key);

/** Remove the element/member at @p index (frees its value). */
int shrewd_basic_json_remove_at(shrewd_basic_json_value_t *value, size_t index);

/* --- text I/O --------------------------------------------------------- */

/**
 * Parse JSON text into a new value tree. Strict RFC 8259 unless the
 * options relax it. Returns SHREWD_BASIC_ERR_JSON_SYNTAX for malformed
 * text and SHREWD_BASIC_ERR_JSON_DEPTH past the nesting limit; @p error
 * (optional) receives the byte offset and a description.
 */
int shrewd_basic_json_parse(const char *text, size_t length,
                            shrewd_basic_json_value_t **out,
                            const shrewd_basic_json_options_t *options,
                            shrewd_basic_json_error_t *error);

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

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_JSON_JSON_H */

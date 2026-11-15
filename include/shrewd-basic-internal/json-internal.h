#ifndef SHREWD_BASIC_JSON_INTERNAL_H
#define SHREWD_BASIC_JSON_INTERNAL_H

/**
 * @file json-internal.h
 * @brief DOM representation and small helpers shared by the json
 * translation units only. Not part of the public API and not installed.
 *
 * Numbers keep their exact source token (or a canonical rendering for
 * created values), so serialize round-trips bit-perfect and the typed
 * accessors parse on demand.
 */

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "shrewd-basic/json/json.h"

typedef struct shrewd_basic_json_entry {
    char *key;
    shrewd_basic_json_value_t *value;
} shrewd_basic_json_entry_t;

struct shrewd_basic_json_value {
    shrewd_basic_json_kind_t kind;
    union {
        bool boolean;
        struct {
            char *raw; /**< Exact token text; canonical for created values. */
        } number;
        char *string;
        struct {
            shrewd_basic_json_value_t **items;
            size_t count;
            size_t cap;
        } array;
        struct {
            shrewd_basic_json_entry_t *entries;
            size_t count;
            size_t cap;
        } object;
    } u;
};

/* Effective option values (0 means "the documented default"). */

/* Create a number from its exact token text (copies @p n bytes). */
int json_value_new_number(const char *raw, size_t n,
                          shrewd_basic_json_value_t **out);

/*
 * Append an object member unconditionally (duplicate keys are kept,
 * preserving document order - what parse needs for text fidelity).
 * The public shrewd_basic_json_set() is the replace-or-append view.
 */
int json_object_add(shrewd_basic_json_value_t *object, const char *key,
                    shrewd_basic_json_value_t *item);

static inline int json_opt_depth(const shrewd_basic_json_options_t *options)
{
    return ((NULL != options) && (0 < options->max_depth))
            ? options->max_depth : SHREWD_BASIC_JSON_DEPTH_DEFAULT;
}

static inline int json_opt_indent(const shrewd_basic_json_options_t *options)
{
    return ((NULL != options) && (0 < options->indent)) ? options->indent : 2;
}

/* Bounded strlen for counted buffers (strnlen is POSIX, not C11). */
static inline size_t json_strn_len(const char *text, size_t max)
{
    size_t n = 0;
    while ((n < max) && ('\0' != text[n])) {
        ++n;
    }
    return n;
}

/*
 * Growable output buffer for serialize (same shape as the dump body
 * buffer): doubles on demand, always NUL-terminated.
 */
typedef struct json_buf {
    char *data;
    size_t len;
    size_t cap;
    int nomem;
} json_buf_t;

int json_buf_init(json_buf_t *buf);
void json_buf_release(json_buf_t *buf);
int json_buf_append(json_buf_t *buf, const char *text, size_t n);
int json_buf_printf(json_buf_t *buf, const char *fmt, ...);
int json_buf_string(json_buf_t *buf, const char *text); /* NUL-terminated */

#endif /* SHREWD_BASIC_JSON_INTERNAL_H */

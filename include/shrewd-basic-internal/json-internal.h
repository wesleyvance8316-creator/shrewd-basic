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

#include "shrewd-basic/formats/json.h"

typedef struct shrewd_basic_json_entry {
    char *key;
    shrewd_basic_json_value_t *value;
} shrewd_basic_json_entry_t;

struct shrewd_basic_json_value {
    shrewd_basic_json_kind_t kind;
    /*
     * Inline storage, shared across kinds: short number tokens and
     * strings live here (SSO), and so do the first two members of
     * containers (the slots of a container and the token of a scalar
     * never coexist). The union gives it the alignment entry pointers
     * need.
     */
    union {
        char bytes[32];
        shrewd_basic_json_entry_t entry;
        shrewd_basic_json_value_t *slot;
    } local;
    union {
        bool boolean;
        struct {
            char *raw; /**< Exact token text; canonical for created values. */
        } number;
        char *string;
        struct {
            shrewd_basic_json_value_t **items;
            size_t count;
            size_t cap; /* 0 = the inline slots are the storage */
        } array;
        struct {
            shrewd_basic_json_entry_t *entries;
            size_t count;
            size_t cap; /* 0 = the inline slots are the storage */
        } object;
    } u;
};

/* Effective option values (0 means "the documented default"). */

/* Create a number from its exact token text (copies @p n bytes). */
int json_value_new_number(const char *raw, size_t n, shrewd_basic_json_value_t **out);

/*
 * Append an object member unconditionally (duplicate keys are kept,
 * preserving document order - what parse needs for text fidelity).
 * The public shrewd_basic_json_set() is the replace-or-append view.
 */
int json_object_add(shrewd_basic_json_value_t *object,
                    const char *key,
                    shrewd_basic_json_value_t *item);
/* Like json_object_add but takes ownership of @p key (no copy). */
int json_object_add_steal(shrewd_basic_json_value_t *object,
                          char *key,
                          shrewd_basic_json_value_t *item);

/*
 * Decode allocation registry: blocks allocated during decode are
 * tracked per decoded root, so cleanup frees exactly what decode
 * created (pre-set defaults - static literals or caller allocations -
 * are never touched). Not synchronized; see docs/json.md.
 */
/* Decode allocation tracking is gone: targets start zeroed and every
 * pointer decode writes is decode-owned (policy BORROWED fields excepted),
 * so release is a plain recursive walk. */

/*
 * Merged policy view of one field: the identity view (member name as
 * the only key, no flags) when @p info is NULL or has no rows for it.
 */
void json_policy_view(const shrewd_basic_json_info_t *info,
                      const shrewd_basic_reflect_field_t *field,
                      shrewd_basic_json_key_info_t *out);

static inline int json_opt_depth(const shrewd_basic_json_options_t *options)
{
    return ((NULL != options) && (0 < options->max_depth)) ? options->max_depth
                                                           : SHREWD_BASIC_JSON_DEPTH_DEFAULT;
}

static inline int json_opt_indent(const shrewd_basic_json_options_t *options)
{
    return ((NULL != options) && (0 < options->indent)) ? options->indent : 2;
}

/*
 * Decode one UTF-8 sequence (at most @p n bytes): returns the bytes
 * consumed, or 0 for an invalid sequence (overlong, surrogate and
 * out-of-range forms are rejected).
 */
static inline size_t json_utf8_decode(const unsigned char *text, size_t n, unsigned *out)
{
    unsigned c = text[0];
    size_t need = 0;
    unsigned cp = 0;
    if (0x80u > c) {
        *out = c;
        return 1;
    }
    if ((0xc0u <= c) && (0xe0u > c)) {
        need = 2;
        cp = c & 0x1fu;
    } else if ((0xe0u <= c) && (0xf0u > c)) {
        need = 3;
        cp = c & 0x0fu;
    } else if ((0xf0u <= c) && (0xf8u > c)) {
        need = 4;
        cp = c & 0x07u;
    } else {
        return 0;
    }
    if (n < need) {
        return 0;
    }
    for (size_t i = 1; i < need; ++i) {
        if (0x80u != (text[i] & 0xc0u)) {
            return 0;
        }
        cp = (cp << 6) | (text[i] & 0x3fu);
    }
    if (((2 == need) && (0x80u > cp)) || ((3 == need) && (0x800u > cp))
        || ((4 == need) && (0x10000u > cp))) {
        return 0; /* overlong */
    }
    if ((0xd800u <= cp) && (0xdfffu >= cp)) {
        return 0; /* surrogate half */
    }
    if (0x10ffffu < cp) {
        return 0;
    }
    *out = cp;
    return need;
}

/* Is @p n bytes of @p text well-formed UTF-8? */
static inline int json_utf8_valid(const char *text, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned cp = 0;
        size_t used = json_utf8_decode((const unsigned char *)text + i, n - i, &cp);
        if (0 == used) {
            return 0;
        }
        i += used;
    }
    return 1;
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
int json_buf_string(json_buf_t *buf, const char *text);

/* Append one code point to @p buf as UTF-8. */
static inline int json_utf8_append(json_buf_t *buf, unsigned code)
{
    char bytes[4];
    size_t n = 0;
    if (0x80u > code) {
        bytes[0] = (char)code;
        n = 1;
    } else if (0x800u > code) {
        bytes[0] = (char)(0xc0u | (code >> 6));
        bytes[1] = (char)(0x80u | (code & 0x3fu));
        n = 2;
    } else if (0x10000u > code) {
        bytes[0] = (char)(0xe0u | (code >> 12));
        bytes[1] = (char)(0x80u | ((code >> 6) & 0x3fu));
        bytes[2] = (char)(0x80u | (code & 0x3fu));
        n = 3;
    } else {
        bytes[0] = (char)(0xf0u | (code >> 18));
        bytes[1] = (char)(0x80u | ((code >> 12) & 0x3fu));
        bytes[2] = (char)(0x80u | ((code >> 6) & 0x3fu));
        bytes[3] = (char)(0x80u | (code & 0x3fu));
        n = 4;
    }
    return json_buf_append(buf, bytes, n);
} /* NUL-terminated */

#endif /* SHREWD_BASIC_JSON_INTERNAL_H */

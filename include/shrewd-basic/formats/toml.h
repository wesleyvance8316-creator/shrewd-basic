#ifndef SHREWD_BASIC_TOML_H
#define SHREWD_BASIC_TOML_H

/**
 * @file toml.h
 * @brief TOML 1.0 subset mapping: struct <-> text (config files).
 *
 * A loud subset - never half-compatible. Supported: comments, blank
 * lines, `[table]` / `[a.b]` headers (nested structs), `key = value`,
 * basic/literal strings, integers (decimal / 0x hex), floats, booleans
 * and homogeneous scalar arrays. Rejected with a line number: datetime,
 * multi-line strings, inline tables, dotted keys, array tables, quoted
 * keys, mixed arrays and duplicate keys.
 *
 * Mapping: top-level keys fill root struct leaves; tables fill nested
 * struct fields; arrays fill fixed scalar arrays (element-wise scan).
 * Pointer / flexible / wide-string / union shapes are refused
 * (SHREWD_BASIC_ERR_JSON_UNSUPPORTED). Leaf conversion goes through the
 * shared text layer (shrewd_basic_reflect_scan_text / _render_text).
 *
 * Ownership: decode zeroes the target first and fills it (owned strings
 * allocate); release with shrewd_basic_toml_free(), which frees owned
 * content but NOT the root object (the caller owns the root). A failed
 * decode may leave a partial graph - toml_free() it before retrying.
 */

#include <stddef.h>

#include "shrewd-basic/reflect/reflect.h"

/** Fault detail: 1-based line of the problem (0 = not text-bound). */
typedef struct shrewd_basic_toml_error {
    size_t line;
    char message[128];
} shrewd_basic_toml_error_t;

/**
 * Encode @p object as TOML text (compact, human-readable). Scalars come
 * before `[table]` sections, floats always carry a fraction/exponent.
 * @p error may be NULL. Release the text with
 * shrewd_basic_toml_text_free().
 */
int shrewd_basic_toml_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             char **out_text,
                             shrewd_basic_toml_error_t *error);

/**
 * Decode TOML text into @p object (strict subset). The target is
 * zeroed first; unknown keys, duplicates and unsupported constructs are
 * errors (never silently dropped).
 */
int shrewd_basic_toml_decode(const char *text,
                             size_t length,
                             void *object,
                             const shrewd_basic_reflect_type_t *type,
                             shrewd_basic_toml_error_t *error);

/** Release text produced by shrewd_basic_toml_encode(). */
void shrewd_basic_toml_text_free(char *text);

/**
 * Release everything shrewd_basic_toml_decode() allocated inside
 * @p object (owned strings). The root object itself stays with the
 * caller.
 */
int shrewd_basic_toml_free(void *object, const shrewd_basic_reflect_type_t *type);

#endif /* SHREWD_BASIC_TOML_H */

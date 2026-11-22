#ifndef SHREWD_BASIC_TOML_H
#define SHREWD_BASIC_TOML_H

/**
 * @file toml.h
 * @brief TOML 1.0 subset mapping: struct <-> text (config files).
 *
 * A loud subset - never half-compatible. Supported: comments, blank
 * lines, `[table]` / `[a.b]` headers (nested structs), `key = value`,
 * basic/literal strings (single- and multi-line `"""` / `'''`, line-ending
 * backslash continuation, quote runs), integers (decimal / 0x hex),
 * floats, booleans, homogeneous scalar arrays, inline tables `{k = v}`
 * (single-line), dotted keys `a.b = v`, array tables `[[items]]`
 * (fixed or dynamic struct arrays; `[items.x]` descends into the last
 * element), quoted keys (`"a b"` / `'x'`) and datetime shapes
 * (offset/local date-time, bare date/time).
 *
 * Rejected with a line number: duplicate keys / tables, bad escapes,
 * lone surrogates, mixed-type arrays, arrays longer than the field,
 * keys after a table header, subtables of a leaf and unknown keys /
 * tables.
 *
 * Mapping: top-level keys fill root struct leaves; tables fill nested
 * struct fields; arrays fill fixed scalar arrays (element-wise scan) or
 * FIELD_DARRAY vectors (grown per element). Datetime values map to
 * string fields verbatim (re-encoded as quoted strings); a datetime
 * aimed at a non-string field is SHREWD_BASIC_ERR_JSON_UNSUPPORTED -
 * no implicit conversion. Pointer / flexible / wide-string / union
 * shapes are refused (SHREWD_BASIC_ERR_JSON_UNSUPPORTED). Leaf
 * conversion goes through the shared text layer
 * (shrewd_basic_reflect_scan_text / _render_text).
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
 * before `[table]` sections, struct arrays emit one `[[path]]` block per
 * element (root scalars first - later keys would fall into the last
 * table), floats always carry a fraction/exponent and owned strings must
 * be non-NULL (datetimes stored in string fields come back as quoted
 * strings). @p error may be NULL. Release the text with
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

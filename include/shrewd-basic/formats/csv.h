#ifndef SHREWD_BASIC_FORMATS_CSV_H
#define SHREWD_BASIC_FORMATS_CSV_H

/**
 * @file csv.h
 * @brief RFC 4180 CSV <-> reflect-described struct tables.
 *
 * One row maps to one struct; a table is a caller-provided (or decode
 * allocated) array of them. Columns are the leaf fields, flattened
 * through nested structs as dotted names ("origin.x"); arrays,
 * pointers, flexible tails, wide strings, unions and bit groups are
 * rejected loudly (SHREWD_BASIC_ERR_JSON_UNSUPPORTED). Strings must be
 * text buffers (FIELD_STRING_BUFFER) or owned (FIELD_STRING_OWNED) -
 * borrowed pointers cannot round-trip through the text layer.
 *
 * The header line carries the column names; decode matches by name
 * (order-independent): absent columns leave zero values, unknown or
 * duplicate columns are errors. Quoting follows RFC 4180 - fields with
 * comma, quote, CR or LF (and empty fields) are quoted with doubled
 * inner quotes; parse accepts LF and CRLF line endings and ignores
 * empty lines. Leaf conversion goes through the shared text layer
 * (enums by name, floats round-trip safe).
 *
 * Ownership: decode allocates the row array with the platform
 * allocator (rows are zeroed before filling; owned strings inside are
 * decode-owned) - release with shrewd_basic_csv_rows_free().
 * shrewd_basic_csv_encode() allocates the text, released with
 * shrewd_basic_csv_text_free().
 */

#include <stddef.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Fault detail: 1-based source line of the problem (0 = not textual). */
typedef struct shrewd_basic_csv_error {
    size_t line;
    char message[128];
} shrewd_basic_csv_error_t;

/**
 * Encode @p count structs at @p rows (each @c row_type->sb_refl_size
 * bytes) as CSV: a header line of column names, then one line per row.
 * @p error may be NULL.
 */
int shrewd_basic_csv_encode(const void *rows,
                            size_t count,
                            const shrewd_basic_reflect_type_t *row_type,
                            char **out_text,
                            shrewd_basic_csv_error_t *error);

/**
 * Decode CSV text into a freshly allocated array of structs (returned
 * through @p out_rows / @p out_count; release with
 * shrewd_basic_csv_rows_free()). Rows match the header width exactly;
 * columns absent from the header keep zero values.
 */
int shrewd_basic_csv_decode(const char *text,
                            size_t length,
                            void **out_rows,
                            size_t *out_count,
                            const shrewd_basic_reflect_type_t *row_type,
                            shrewd_basic_csv_error_t *error);

/**
 * Release a decode-produced row array: owned string contents per row,
 * then the array itself. No-op for (NULL, 0).
 */
int shrewd_basic_csv_rows_free(void *rows,
                               size_t count,
                               const shrewd_basic_reflect_type_t *row_type);

/** Release text produced by shrewd_basic_csv_encode(). */
void shrewd_basic_csv_text_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_FORMATS_CSV_H */

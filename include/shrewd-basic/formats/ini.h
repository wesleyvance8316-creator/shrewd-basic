#ifndef SHREWD_BASIC_FORMATS_INI_H
#define SHREWD_BASIC_FORMATS_INI_H

/**
 * @file ini.h
 * @brief struct <-> INI text: sections map nested structs, keys map
 *        leaf fields (the shared text layer does leaf conversion).
 *
 * Profile: line comments (`;` `#`), blank lines, `[section]` /
 * `[a.b]` path headers (dotted = nested structs), `key = value`
 * (whitespace around `=`, value trimmed). Whole-line comments only;
 * arrays, pointers and other non-leaf shapes are refused loudly.
 * Ownership follows the library-wide contract: decode zeroes the
 * target first and allocates owned strings; release with
 * shrewd_basic_ini_free() (contents only - the root object stays the
 * caller's).
 */

#include <stddef.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/** Options for encode/decode. Every zero field means the default. */
typedef struct shrewd_basic_ini_options {
    int strict; /**< decode: unknown keys error (default: skipped). */
} shrewd_basic_ini_options_t;

/** Fault detail: line of the input and a short description. */
typedef struct shrewd_basic_ini_error {
    size_t line;
    char message[128];
} shrewd_basic_ini_error_t;

/**
 * Encode @p object as INI text (root leaves first, then one section
 * per nested struct). Release @p out_text with
 * shrewd_basic_ini_text_free().
 */
int shrewd_basic_ini_encode(const void *object,
                            const shrewd_basic_reflect_type_t *type,
                            char **out_text,
                            const shrewd_basic_ini_options_t *options,
                            shrewd_basic_ini_error_t *error);

/**
 * Decode INI text into @p object (zeroed first). Unknown keys skip
 * unless @c strict; duplicate keys/sections and non-leaf targets error.
 */
int shrewd_basic_ini_decode(const char *text,
                            size_t length,
                            void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_ini_options_t *options,
                            shrewd_basic_ini_error_t *error);

/** Release text produced by shrewd_basic_ini_encode(). */
void shrewd_basic_ini_text_free(char *text);

/** Release everything ini_decode() allocated inside @p object. */
int shrewd_basic_ini_free(void *object, const shrewd_basic_reflect_type_t *type);

#endif /* SHREWD_BASIC_FORMATS_INI_H */

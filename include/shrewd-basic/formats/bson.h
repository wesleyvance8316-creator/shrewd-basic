#ifndef SHREWD_BASIC_FORMATS_BSON_H
#define SHREWD_BASIC_FORMATS_BSON_H

/**
 * @file bson.h
 * @brief struct <-> BSON (Binary JSON wire format), the same shape
 *        mapping as the msgpack/CBOR layers.
 *
 * Wire: a document is int32 total length + elements + trailing 0x00;
 * an element is a type byte, a NUL-terminated cstring key and a value.
 * Used types: double(0x01), string(0x02, int32 length including the
 * trailing NUL), document(0x03), array(0x04 - a document keyed "0",
 * "1", ...), bool(0x08), null(0x0A), int32(0x10), int64(0x12).
 * Integers encode into the smallest of int32/int64 that holds them.
 *
 * Shape mapping (identical to msgpack): struct -> document (cstring
 * keys = field names); char -> 1-character string (decode also accepts
 * integers); enums -> constant name (decode name or integer); signed/
 * unsigned fields -> int32/int64 with strict range checks (values
 * beyond INT64_MAX are refused: BSON has no unsigned 64-bit); floats
 * -> double (decode refuses values that do not land exactly in a
 * narrower float); long double -> double with the precision guard;
 * complex -> [re, im] array; wide strings <-> string via the strict
 * UTF converters; pointers -> null or a nested document; fixed arrays
 * -> array.
 *
 * Loud refusals (SHREWD_BASIC_ERR_BIN_UNSUPPORTED): bit groups,
 * unions, flexible/dynamic/sentinel arrays, multi-dimension arrays,
 * BIT_INT, opaque and ops-hooked fields; on decode also the
 * BSON-specific types (binary, ObjectId, datetime, regex, JS/symbol,
 * timestamp, decimal128, ...). Malformed streams (truncation, trailing
 * bytes, bad array indices, repeated field keys) are
 * SHREWD_BASIC_ERR_BIN_FORMAT.
 *
 * Ownership: decode zeroes the target first and allocates string and
 * pointer targets; release with shrewd_basic_bson_free() (contents
 * only - the root stays the caller's; freed pointer slots are zeroed).
 */

#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/**
 * Encode @p object as BSON. @p out_bytes receives a platform-allocated
 * buffer (@p out_size its length); release with
 * shrewd_basic_bson_bytes_free().
 */
int shrewd_basic_bson_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             void **out_bytes,
                             size_t *out_size);

/**
 * Decode BSON into @p object. The input must be one well-formed
 * document, fully consumed (trailing bytes are
 * SHREWD_BASIC_ERR_BIN_FORMAT).
 */
int shrewd_basic_bson_decode(const void *bytes,
                             size_t size,
                             void *object,
                             const shrewd_basic_reflect_type_t *type);

/** Release bytes produced by shrewd_basic_bson_encode(). */
void shrewd_basic_bson_bytes_free(void *bytes);

/** Release everything shrewd_basic_bson_decode() allocated inside @p object. */
int shrewd_basic_bson_free(void *object, const shrewd_basic_reflect_type_t *type);

#endif /* SHREWD_BASIC_FORMATS_BSON_H */

#ifndef SHREWD_BASIC_FORMATS_CBOR_H
#define SHREWD_BASIC_FORMATS_CBOR_H

/**
 * @file cbor.h
 * @brief struct <-> CBOR (RFC 8949), the same shape mapping as the
 *        json/msgpack layers over the shared binary skeleton.
 *
 * Wire subset (encode and decode): definite-length arrays/maps, text
 * strings, integers in the minimal form, f32/f64 floats, true/false/
 * null. Loud refusals (SHREWD_BASIC_ERR_BIN_UNSUPPORTED): indefinite
 * lengths, tags, bytes items, undefined/simple extras, f16 is accepted
 * on decode (widened to f32) but never emitted. Shapes refused like
 * msgpack: bit groups, unions, flexible/dynamic/sentinel arrays,
 * BIT_INT, opaque and ops-hooked fields.
 *
 * Mapping: struct -> map{text keys = field names}; char -> 1-character
 * text (decode also accepts integers); enums -> constant name (decode
 * also accepts integers); long double -> f64 with the precision guard
 * (narrowing refuses); complex -> [re, im]; wide strings -> text via
 * the strict UTF converters; pointers -> null or the nested target.
 *
 * Ownership: decode zeroes the target first and allocates pointer
 * targets; release with shrewd_basic_cbor_free() (contents only - the
 * root object stays the caller's). Integers narrow strictly
 * (SHREWD_BASIC_ERR_REFLECT_MISMATCH outside the field's range).
 */

#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/**
 * Encode @p object as CBOR. @p out_bytes receives a platform-allocated
 * buffer (@p out_size its length); release with
 * shrewd_basic_cbor_bytes_free().
 */
int shrewd_basic_cbor_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             void **out_bytes,
                             size_t *out_size);

/**
 * Decode CBOR into @p object. The input must be fully consumed
 * (trailing bytes are SHREWD_BASIC_ERR_BIN_FORMAT).
 */
int shrewd_basic_cbor_decode(const void *bytes,
                             size_t size,
                             void *object,
                             const shrewd_basic_reflect_type_t *type);

/** Release bytes produced by shrewd_basic_cbor_encode(). */
void shrewd_basic_cbor_bytes_free(void *bytes);

/** Release everything shrewd_basic_cbor_decode() allocated inside @p object. */
int shrewd_basic_cbor_free(void *object, const shrewd_basic_reflect_type_t *type);

#endif /* SHREWD_BASIC_FORMATS_CBOR_H */

#ifndef SHREWD_BASIC_MSGPACK_H
#define SHREWD_BASIC_MSGPACK_H

/**
 * @file msgpack.h
 * @brief MessagePack mapping of reflect-described structs (formats layer).
 *
 * struct <-> bytes mapping driven by shrewd-basic reflect descriptors -
 * the same single source of truth the json and bin formats use. The
 * wire is the MessagePack spec (multi-byte numbers and floats are
 * big-endian, integers use the minimal encoding):
 *
 *   struct              map{str key = member name, value}
 *   bool                true / false
 *   char                1-char str (decode also accepts an integer)
 *   int8..int64/uint*   minimal int/uint encoding (fixint, u/int8..64)
 *   enum                constant name str (unknown values fall back to
 *                       the bare integer; decode accepts either)
 *   float/double        float32 / float64
 *   long double         float64 with a precision guard (a value that
 *                       does not survive the narrowing is refused)
 *   complex             [re, im] (two float64; same precision guard)
 *   string (char *)     str (or nil when NULL); decode allocates a
 *                       NUL-terminated copy it owns
 *   char[N] buffer      str of the content up to the first NUL
 *   wide strings        str via strict UTF-8 conversion (code-unit
 *                       sequences and surrogate pairs handled; invalid
 *                       content errors with REFLECT_ENCODING)
 *   fixed array         array of elements
 *   pointer             nil or the followed pointee value (struct,
 *                       array or scalar targets; decode allocates the
 *                       target block)
 *   nested struct       map
 *
 * Not in v1 (loud SHREWD_BASIC_ERR_BIN_UNSUPPORTED): bit groups,
 * tagged unions, flexible tails, dynamic/sentinel arrays, bit-precise
 * integers and opaque targets. Structs are map-encoded in member
 * order; on decode extra keys are ignored and missing keys stay zero
 * (first duplicate key wins). The decode target is zeroed first, every
 * pointer the decode writes is decode-owned, and
 * shrewd_basic_msgpack_free() releases them all without touching the
 * root object.
 */

#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/**
 * Serialize @p object (an instance of the struct @p type describes) into
 * a freshly allocated MessagePack buffer. Returns SHREWD_BASIC_ERR_OK,
 * SHREWD_BASIC_ERR_BIN_UNSUPPORTED (v1 gaps, ops-hooked fields, deep
 * nesting), SHREWD_BASIC_ERR_REFLECT_ENCODING (invalid wide string
 * content), SHREWD_BASIC_ERR_MEM_NOMEM or SHREWD_BASIC_ERR_ARG_INVALID.
 * Release the block with shrewd_basic_msgpack_bytes_free().
 */
int shrewd_basic_msgpack_encode(const void *object,
                                const shrewd_basic_reflect_type_t *type,
                                void **out_bytes,
                                size_t *out_size);

/**
 * Parse MessagePack bytes into @p object (zeroed first - never decode
 * over live allocations; free first). Returns SHREWD_BASIC_ERR_OK,
 * SHREWD_BASIC_ERR_BIN_FORMAT (truncated, wrong wire type for the
 * field, trailing bytes), SHREWD_BASIC_ERR_REFLECT_MISMATCH (integer
 * out of the field's range, string longer than the buffer),
 * SHREWD_BASIC_ERR_REFLECT_ENCODING (invalid wide string content),
 * SHREWD_BASIC_ERR_BIN_UNSUPPORTED or SHREWD_BASIC_ERR_MEM_NOMEM.
 * Trailing bytes after the value are an error.
 */
int shrewd_basic_msgpack_decode(const void *bytes,
                                size_t size,
                                void *object,
                                const shrewd_basic_reflect_type_t *type);

/** Release a buffer produced by shrewd_basic_msgpack_encode(). */
void shrewd_basic_msgpack_bytes_free(void *bytes);

/**
 * Release every pointer shrewd_basic_msgpack_decode() wrote inside
 * @p object (strings, pointer targets, their internals) - the mirror
 * of decode, like shrewd_basic_json_free(). The root object itself is
 * not freed.
 */
int shrewd_basic_msgpack_free(void *object, const shrewd_basic_reflect_type_t *type);

#endif /* SHREWD_BASIC_MSGPACK_H */

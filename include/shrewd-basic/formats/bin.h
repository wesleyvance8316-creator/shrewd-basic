#ifndef SHREWD_BASIC_BIN_H
#define SHREWD_BASIC_BIN_H

/**
 * @file bin.h
 * @brief Binary serialization of reflect-described structs (formats layer).
 *
 * struct <-> bytes mapping driven by shrewd-basic reflect descriptors -
 * the same single source of truth the json format uses. The wire format
 * is self-describing enough to decode without the encoder's options:
 *
 *   header (12 bytes, always little-endian):
 *     [0..3]  magic "SBIN"
 *     [4]     format version (1)
 *     [5]     flags: bit0 = big-endian body numbers, bit1 = natural
 *             alignment padding
 *     [6..7]  reserved (0)
 *     [8..11] user version (shrewd_basic_bin_options_t::version)
 *
 *   body values (canonical widths; the C member width never leaks into
 *   the wire - a long on ILP32 still encodes as 8 bytes of int64):
 *     bool/char           1 byte; int8..int64/uint*  canonical 1/2/4/8
 *     enum                8 bytes (signed)
 *     float/double        IEEE binary32 / binary64
 *     string              u8 presence, then u32 length + bytes (no NUL);
 *                         decode allocates a NUL-terminated copy
 *     char[N] buffer      u32 length + bytes (content up to the first
 *                         NUL), NUL-terminated on decode
 *     fixed array         elements in order (length is the descriptor's)
 *     sentinel array      u32 count + elements; decode fills the NULL
 *                         terminator slot
 *     flexible tail (FAM) u32 count + elements; decode writes the count
 *                         member - size the root with
 *                         shrewd_basic_bin_value_size() first
 *     dynamic array       u32 count + elements; decode allocates the
 *                         table (plus sentinel slot) and the count
 *                         member
 *     pointer             u8 presence, then the pointee value; decode
 *                         allocates the target block
 *     tagged union        8-byte tag (signed) + the active variant's
 *                         fields
 *     bit group           extracted bits as unsigned, in the unit's
 *                         canonical width
 *
 * With natural alignment every multi-byte value is padded to its wire
 * width relative to the body start (pad bytes are zero, skipped on
 * decode); the default is compact (no padding). Structural limits and
 * contracts mirror the json mapping: the decode target is zeroed first
 * (never decode over live allocations - free first), every pointer the
 * decode writes is decode-owned, and shrewd_basic_bin_free() releases
 * them all. Cycles are not encoded (they error); shared targets expand
 * as trees. Ops-hooked and opaque fields have no wire form yet.
 */

#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/** Wire number endianness. */
#define SHREWD_BASIC_BIN_ENDIAN_LITTLE 0 /**< Little-endian body (default). */
#define SHREWD_BASIC_BIN_ENDIAN_BIG 1    /**< Big-endian body. */
#define SHREWD_BASIC_BIN_ENDIAN_NATIVE 2 /**< The host's endianness. */

/** Encode/decode options; NULL selects the defaults in the brackets. */
typedef struct shrewd_basic_bin_options {
    int endian;        /**< SHREWD_BASIC_BIN_ENDIAN_* [LITTLE]. */
    int natural_align; /**< Pad values to their wire width [0 = compact]. */
    uint32_t version;  /**< Stamped on encode; decode checks it when non-zero [0]. */
} shrewd_basic_bin_options_t;

/**
 * Serialize @p object (an instance of the struct @p type describes) into
 * a freshly allocated byte block. Returns SHREWD_BASIC_ERR_OK,
 * SHREWD_BASIC_ERR_BIN_UNSUPPORTED (ops-hooked/opaque fields, cycles),
 * SHREWD_BASIC_ERR_MEM_NOMEM or SHREWD_BASIC_ERR_ARG_INVALID. Release
 * the block with shrewd_basic_bin_bytes_free().
 */
int shrewd_basic_bin_encode(const void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_bin_options_t *options,
                            void **out_bytes,
                            size_t *out_size);

/**
 * Parse a block produced by shrewd_basic_bin_encode() into @p object.
 * The target is zeroed first; strings, tables and pointer targets are
 * allocated and owned by the object until shrewd_basic_bin_free(). On
 * failure the partially filled object still frees cleanly. Returns
 * SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_BIN_FORMAT (bad magic, truncated
 * input, unknown union tag, version mismatch),
 * SHREWD_BASIC_ERR_BIN_UNSUPPORTED, SHREWD_BASIC_ERR_MEM_NOMEM or
 * SHREWD_BASIC_ERR_ARG_INVALID.
 */
int shrewd_basic_bin_decode(const void *bytes,
                            size_t length,
                            void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_bin_options_t *options);

/**
 * Byte size the root allocation needs for the stream (sizeof(type) plus
 * its flexible tails), so a FAM-bearing root can be sized before
 * decoding - the shrewd_basic_json_value_size() counterpart. Returns
 * SHREWD_BASIC_ERR_OK or the decode errors for a malformed stream.
 */
int shrewd_basic_bin_value_size(const void *bytes,
                                size_t length,
                                const shrewd_basic_reflect_type_t *type,
                                size_t *out_size);

/**
 * Release a decoded object and everything the decode allocated inside
 * it - the exact inverse of shrewd_basic_bin_decode(). The root storage
 * itself belongs to the caller (only its contents are released).
 */
int shrewd_basic_bin_free(void *object, const shrewd_basic_reflect_type_t *type);

/** Release a block from shrewd_basic_bin_encode(). */
void shrewd_basic_bin_bytes_free(void *bytes);

#endif /* SHREWD_BASIC_BIN_H */

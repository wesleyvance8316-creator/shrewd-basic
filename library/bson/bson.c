/**
 * @file bson.c
 * @brief struct <-> BSON over the shared binary skeleton
 *        (bin-internal.h). Shape mapping identical to msgpack/CBOR;
 *        only the wire encodings differ (length-prefixed documents,
 *        cstring keys).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic-internal/bin-internal.h"
#include "shrewd-basic-internal/reflect-internal.h"
#include "shrewd-basic/formats/bson.h"
#include "shrewd-basic/platform/memory.h"

#define BS_MAX_DEPTH 64

#define BS_T_DOUBLE 0x01u
#define BS_T_STRING 0x02u
#define BS_T_DOC 0x03u
#define BS_T_ARRAY 0x04u
#define BS_T_BOOL 0x08u
#define BS_T_NULL 0x0au
#define BS_T_INT32 0x10u
#define BS_T_INT64 0x12u

/* --- wire order ---------------------------------------------------------- */

/*
 * BSON numbers are little-endian (the spec's byte order - MongoDB
 * interop depends on it). The shared skeleton writes big-endian for
 * msgpack/CBOR, so the order is done locally over its primitives.
 */
static int bs_w_le(binio_buf_t *b, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) {
        int status = binio_w_u8(b, (uint8_t)((value >> (8u * i)) & 0xffu));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static int bs_r_le(binio_rd_t *r, size_t width, uint64_t *out) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) {
        uint8_t byte = 0;
        int status = binio_r_u8(r, &byte);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        value |= ((uint64_t)byte) << (8u * i);
    }
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

/* --- shared value helpers ------------------------------------------------- */

static void bs_int_bounds(shrewd_basic_reflect_kind_t kind, int64_t *lo, int64_t *hi) {
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_INT8:
        *lo = INT8_MIN;
        *hi = INT8_MAX;
        break;
    case SHREWD_BASIC_REFLECT_KIND_INT16:
        *lo = INT16_MIN;
        *hi = INT16_MAX;
        break;
    case SHREWD_BASIC_REFLECT_KIND_INT32:
        *lo = INT32_MIN;
        *hi = INT32_MAX;
        break;
    default:
        *lo = INT64_MIN;
        *hi = INT64_MAX;
        break;
    }
}

static uint64_t bs_uint_limit(shrewd_basic_reflect_kind_t kind) {
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
        return UINT8_MAX;
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
        return UINT16_MAX;
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
        return UINT32_MAX;
    default:
        return UINT64_MAX;
    }
}

/* int32/int64 wire item -> checked value. */
static int bs_wire_int(binio_rd_t *r, uint8_t tag, int64_t lo, int64_t hi, int64_t *out) {
    uint64_t raw = 0;
    int64_t value = 0;
    if (BS_T_INT32 == tag) {
        int status = bs_r_le(r, 4, &raw);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        value = (int64_t)(int32_t)(uint32_t)raw;
    } else if (BS_T_INT64 == tag) {
        int status = bs_r_le(r, 8, &raw);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        value = (int64_t)raw;
    } else {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if ((value < lo) || (value > hi)) {
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

/* Store a numeric value into a leaf of the given width, refusing any
   silent narrowing (mirrors the msgpack/cbor rule). */
static int bs_store_number(long double number,
                           shrewd_basic_reflect_kind_t kind,
                           unsigned char *target,
                           size_t nbytes) {
    if (SHREWD_BASIC_REFLECT_KIND_FLOAT == kind) {
        float single = (float)number;
        if ((long double)single != number) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        memcpy(target, &single, sizeof(single));
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_DOUBLE == kind) {
        double real = (double)number;
        if ((long double)real != number) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        memcpy(target, &real, sizeof(real));
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE == kind) {
        memcpy(target, &number, sizeof(number));
        return SHREWD_BASIC_ERR_OK;
    }
    (void)nbytes;
    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
}

/* --- refusals ------------------------------------------------------------ */

static int bs_refused(const shrewd_basic_reflect_field_t *field) {
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_BITS | SHREWD_BASIC_REFLECT_FIELD_F_UNION
               | SHREWD_BASIC_REFLECT_FIELD_F_FLEX | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
               | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL))) {
        return 1;
    }
    if ((NULL != field->sb_refl_ops)
        && ((NULL != field->sb_refl_ops->get) || (NULL != field->sb_refl_ops->set))) {
        return 1;
    }
    return 0;
}

static int bs_multidim(const shrewd_basic_reflect_type_t *type, uint32_t flags) {
    return ((0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY))
            && (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind))
               ? 1
               : 0;
}

/* --- encode --------------------------------------------------------------- */

static int bs_encode_fields(binio_buf_t *b,
                            const shrewd_basic_reflect_type_t *type,
                            const unsigned char *object,
                            size_t depth);

static int bs_encode_element(binio_buf_t *b,
                             const char *key,
                             const shrewd_basic_reflect_type_t *type,
                             uint32_t flags,
                             const unsigned char *value,
                             size_t nbytes,
                             size_t depth);

static int bs_doc_begin(binio_buf_t *b, size_t *mark) {
    *mark = b->len;
    return bs_w_le(b, 0, 4); /* length placeholder, backfilled later */
}

static int bs_doc_end(binio_buf_t *b, size_t mark) {
    int status = binio_w_u8(b, 0x00);
    if (SHREWD_BASIC_ERR_OK == status) {
        size_t total = b->len - mark;
        b->data[mark + 0] = (unsigned char)(total & 0xffu);
        b->data[mark + 1] = (unsigned char)((total >> 8) & 0xffu);
        b->data[mark + 2] = (unsigned char)((total >> 16) & 0xffu);
        b->data[mark + 3] = (unsigned char)((total >> 24) & 0xffu);
    }
    return status;
}

static int bs_cstring(binio_buf_t *b, const char *key) {
    int status = binio_w_bytes(b, key, strlen(key));
    return (SHREWD_BASIC_ERR_OK == status) ? binio_w_u8(b, 0x00) : status;
}

static int bs_string_payload(binio_buf_t *b, const char *text, size_t n) {
    /* int32 length includes the trailing NUL. */
    int status = bs_w_le(b, (uint64_t)(n + 1), 4);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = binio_w_bytes(b, text, n);
    }
    return (SHREWD_BASIC_ERR_OK == status) ? binio_w_u8(b, 0x00) : status;
}

static int bs_ld_number(binio_buf_t *b, long double value) {
    double narrowed = (double)value;
    uint64_t bits = 0;
    if ((long double)narrowed != value) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* never silently narrow */
    }
    memcpy(&bits, &narrowed, sizeof(bits));
    return bs_w_le(b, bits, 8);
}

static int bs_wide_text(binio_buf_t *b, const void *text, size_t unit, size_t max_units) {
    size_t len = 0;
    char stack[256];
    char *heap = NULL;
    char *out = stack;
    int status = reflect_utf8_from_units(text, unit, max_units, NULL, 0, &len);
    if ((SHREWD_BASIC_ERR_OK == status) && (len >= sizeof(stack))) {
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&heap, len + 1)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        out = heap;
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = reflect_utf8_from_units(text, unit, max_units, out, len + 1, &len);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = bs_string_payload(b, out, len);
    }
    if (NULL != heap) {
        void *old = heap;
        (void)shrewd_basic_platform_free(&old);
    }
    return status;
}

static int bs_encode_value_payload(binio_buf_t *b,
                                   const shrewd_basic_reflect_type_t *type,
                                   const unsigned char *value,
                                   size_t nbytes,
                                   size_t depth) {
    int status = 0;
    if (BS_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return binio_w_u8(b, (0 != reflect_load_u64(value, nbytes)) ? 1u : 0u);
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        memcpy(&c, value, 1);
        return bs_string_payload(b, &c, 1);
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64: {
        int64_t number = reflect_load_i64(value, nbytes);
        return ((number >= INT32_MIN) && (number <= INT32_MAX))
                   ? bs_w_le(b, (uint64_t)(uint32_t)(int32_t)number, 4)
                   : bs_w_le(b, (uint64_t)number, 8);
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64: {
        uint64_t raw = reflect_load_u64(value, nbytes);
        int64_t number = 0;
        if (raw > (uint64_t)INT64_MAX) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* BSON has no uint64 */
        }
        number = (int64_t)raw;
        return ((number >= INT32_MIN) && (number <= INT32_MAX))
                   ? bs_w_le(b, (uint64_t)(uint32_t)(int32_t)number, 4)
                   : bs_w_le(b, (uint64_t)number, 8);
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = reflect_load_i64(value, nbytes);
        for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
            if (type->sb_refl_enum[i].sb_refl_value == number) {
                const char *name = type->sb_refl_enum[i].sb_refl_name;
                return bs_string_payload(b, name, strlen(name));
            }
        }
        return ((number >= INT32_MIN) && (number <= INT32_MAX))
                   ? bs_w_le(b, (uint64_t)(uint32_t)(int32_t)number, 4)
                   : bs_w_le(b, (uint64_t)number, 8);
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        long double number = (SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE == type->sb_refl_kind)
                                 ? reflect_load_ld(value, nbytes)
                                 : (long double)reflect_load_f64(value, nbytes);
        return bs_ld_number(b, number);
    }
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX: {
        /* [re, im] as an array document of two full elements. */
        size_t half = nbytes / 2;
        size_t mark = 0;
        const shrewd_basic_reflect_type_t *part = (4 == half)   ? &shrewd_basic_reflect_float
                                                  : (8 == half) ? &shrewd_basic_reflect_double
                                                                : &shrewd_basic_reflect_long_double;
        status = bs_doc_begin(b, &mark);
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < 2); ++i) {
            status = bs_encode_element(
                b, (0 == i) ? "0" : "1", part, 0, value + (i * half), half, depth + 1);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = bs_doc_end(b, mark);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = reflect_load_str(value);
        return bs_string_payload(b, (NULL != text) ? text : "", (NULL != text) ? strlen(text) : 0);
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32: {
        const void *text = reflect_load_ptr(value);
        size_t unit = reflect_string_unit(type->sb_refl_kind);
        if (NULL == text) {
            return bs_string_payload(b, "", 0);
        }
        return bs_wide_text(b, text, unit, SIZE_MAX);
    }
    case SHREWD_BASIC_REFLECT_KIND_STRUCT:
        return bs_encode_fields(b, type, value, depth);
    default:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
}

/* Element = type byte + cstring key + payload. */
static int bs_encode_element(binio_buf_t *b,
                             const char *key,
                             const shrewd_basic_reflect_type_t *type,
                             uint32_t flags,
                             const unsigned char *value,
                             size_t nbytes,
                             size_t depth) {
    uint8_t tag = 0;
    int status = 0;
    if (BS_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        const void *target = reflect_load_ptr(value);
        if (NULL == target) {
            status = binio_w_u8(b, BS_T_NULL);
            return (SHREWD_BASIC_ERR_OK == status) ? bs_cstring(b, key) : status;
        }
        return bs_encode_element(b,
                                 key,
                                 type,
                                 flags & ~(uint32_t)SHREWD_BASIC_REFLECT_FIELD_F_PTR,
                                 (const unsigned char *)target,
                                 type->sb_refl_size,
                                 depth);
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        tag = BS_T_BOOL;
        break;
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
    case SHREWD_BASIC_REFLECT_KIND_STRING:
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32:
        tag = BS_T_STRING;
        break;
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = 0;
        if (SHREWD_BASIC_REFLECT_KIND_ENUM == type->sb_refl_kind) {
            int named = 0;
            number = reflect_load_i64(value, nbytes);
            for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
                if (type->sb_refl_enum[i].sb_refl_value == number) {
                    named = 1;
                    break;
                }
            }
            if (0 != named) {
                tag = BS_T_STRING;
                break;
            }
        } else {
            number = reflect_load_i64(value, nbytes);
            if ((SHREWD_BASIC_REFLECT_KIND_UINT8 <= type->sb_refl_kind)
                && (reflect_load_u64(value, nbytes) > (uint64_t)INT64_MAX)) {
                return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* no uint64 wire */
            }
        }
        tag = ((number >= INT32_MIN) && (number <= INT32_MAX)) ? BS_T_INT32 : BS_T_INT64;
        break;
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE:
        tag = BS_T_DOUBLE;
        break;
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX:
        tag = BS_T_ARRAY;
        break;
    case SHREWD_BASIC_REFLECT_KIND_STRUCT:
        tag = BS_T_DOC;
        break;
    default:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    status = binio_w_u8(b, tag);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = bs_cstring(b, key);
    }
    return (SHREWD_BASIC_ERR_OK == status) ? bs_encode_value_payload(b, type, value, nbytes, depth)
                                           : status;
}

static int bs_encode_fields(binio_buf_t *b,
                            const shrewd_basic_reflect_type_t *type,
                            const unsigned char *object,
                            size_t depth) {
    size_t mark = 0;
    int status = 0;
    if (BS_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    status = bs_doc_begin(b, &mark);
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_field_count); ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const unsigned char *at = object + field->sb_refl_offset;
        uint32_t flags = field->sb_refl_flags;
        if ((0 != bs_refused(field)) || (0 != bs_multidim(field->sb_refl_type, flags))) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
        }
        if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            size_t unit = field->sb_refl_stride;
            status = binio_w_u8(b, BS_T_STRING);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = bs_cstring(b, field->sb_refl_name);
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                if (1 == unit) {
                    const char *text = (const char *)at;
                    status
                        = bs_string_payload(b, text, reflect_strn_len(text, field->sb_refl_count));
                } else {
                    status = bs_wide_text(b, at, unit, field->sb_refl_count);
                }
            }
            continue;
        }
        if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
            /* Fixed array -> array document keyed "0", "1", ... */
            size_t mark2 = 0;
            char idx[24];
            size_t stride = field->sb_refl_stride;
            size_t count = (0 != stride) ? (field->sb_refl_size / stride) : 0;
            status = binio_w_u8(b, BS_T_ARRAY);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = bs_cstring(b, field->sb_refl_name);
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                status = bs_doc_begin(b, &mark2);
            }
            for (size_t j = 0; (SHREWD_BASIC_ERR_OK == status) && (j < count); ++j) {
                snprintf(idx, sizeof(idx), "%zu", j);
                status = bs_encode_element(b,
                                           idx,
                                           field->sb_refl_type,
                                           flags & (uint32_t)SHREWD_BASIC_REFLECT_FIELD_F_PTR,
                                           at + (j * stride),
                                           stride,
                                           depth + 1);
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                status = bs_doc_end(b, mark2);
            }
            continue;
        }
        status = bs_encode_element(
            b, field->sb_refl_name, field->sb_refl_type, flags, at, field->sb_refl_size, depth + 1);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = bs_doc_end(b, mark);
    }
    return status;
}

int shrewd_basic_bson_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             void **out_bytes,
                             size_t *out_size) {
    binio_buf_t buf;
    int status = 0;
    if ((NULL == object) || (NULL == type) || (NULL == out_bytes) || (NULL == out_size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&buf, 0, sizeof(buf));
    status = bs_encode_fields(&buf, type, (const unsigned char *)object, 0);
    if (SHREWD_BASIC_ERR_OK != status) {
        binio_buf_free(&buf);
        return status;
    }
    *out_bytes = buf.data;
    *out_size = buf.len;
    return SHREWD_BASIC_ERR_OK;
}

/* --- decode --------------------------------------------------------------- */

static int bs_decode_fields(binio_rd_t *r,
                            const shrewd_basic_reflect_type_t *type,
                            unsigned char *object,
                            size_t depth);

static int bs_read_cstring(binio_rd_t *r, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        uint8_t byte = 0;
        int status = binio_r_u8(r, &byte);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (0 == byte) {
            out[n] = '\0';
            return SHREWD_BASIC_ERR_OK;
        }
        if ((n + 1) >= cap) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        out[n] = (char)byte;
        ++n;
    }
}

/* Skip one value (unknown keys / duplicates); exotic tags are refused. */
static int bs_skip_value(binio_rd_t *r, uint8_t tag) {
    uint64_t n = 0;
    const unsigned char *dump = NULL;
    switch (tag) {
    case BS_T_NULL:
        return SHREWD_BASIC_ERR_OK;
    case BS_T_BOOL: {
        uint8_t byte = 0;
        return binio_r_u8(r, &byte);
    }
    case BS_T_INT32:
        return bs_r_le(r, 4, &n);
    case BS_T_INT64:
    case BS_T_DOUBLE:
        return bs_r_le(r, 8, &n);
    case BS_T_STRING:
    case BS_T_DOC:
    case BS_T_ARRAY: {
        int status = bs_r_le(r, 4, &n);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (n > (uint64_t)binio_r_left(r)) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        return binio_r_bytes(r, (size_t)n, &dump);
    }
    default:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* exotic BSON types */
    }
}

static int bs_decode_payload(binio_rd_t *r,
                             uint8_t tag,
                             const shrewd_basic_reflect_type_t *type,
                             uint32_t flags,
                             unsigned char *target,
                             size_t nbytes,
                             size_t stride,
                             size_t depth);

/* Array document -> count slots at @p stride (index keys 0..count-1). */
static int bs_decode_array(binio_rd_t *r,
                           const shrewd_basic_reflect_type_t *type,
                           uint32_t flags,
                           unsigned char *target,
                           size_t count,
                           size_t stride,
                           size_t depth) {
    uint64_t total = 0;
    size_t start = 0;
    size_t end = 0;
    size_t index = 0;
    int status = 0;
    if (BS_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    status = bs_r_le(r, 4, &total);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if ((5 > total) || ((total - 4) > (uint64_t)binio_r_left(r))) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    start = r->pos - 4;
    end = start + (size_t)total;
    while ((r->pos + 1) < end) {
        uint8_t tag = 0;
        char key[32];
        char want[32];
        status = binio_r_u8(r, &tag);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (0 == tag) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = bs_read_cstring(r, key, sizeof(key));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        snprintf(want, sizeof(want), "%zu", index);
        if (0 != strcmp(key, want)) {
            return SHREWD_BASIC_ERR_BIN_FORMAT; /* index sequence broken */
        }
        if (index >= count) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        status = bs_decode_payload(
            r, tag, type, flags, target + (index * stride), stride, stride, depth);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        ++index;
    }
    if ((r->pos + 1) != end) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    {
        uint8_t term = 0;
        status = binio_r_u8(r, &term);
        if ((SHREWD_BASIC_ERR_OK != status) || (0 != term)) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static int bs_decode_payload(binio_rd_t *r,
                             uint8_t tag,
                             const shrewd_basic_reflect_type_t *type,
                             uint32_t flags,
                             unsigned char *target,
                             size_t nbytes,
                             size_t stride,
                             size_t depth) {
    int status = 0;
    if (BS_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        if (BS_T_NULL == tag) {
            return bs_skip_value(r, tag);
        }
        {
            void *block = NULL;
            if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_calloc(&block, 1, type->sb_refl_size)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            status = bs_decode_payload(r,
                                       tag,
                                       type,
                                       flags & ~(uint32_t)SHREWD_BASIC_REFLECT_FIELD_F_PTR,
                                       (unsigned char *)block,
                                       type->sb_refl_size,
                                       type->sb_refl_size,
                                       depth);
            if (SHREWD_BASIC_ERR_OK == status) {
                memcpy(target, &block, sizeof(block));
            } else {
                void *old = block;
                (void)shrewd_basic_platform_free(&old);
            }
            return status;
        }
    }
    if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        uint64_t n = 0;
        const unsigned char *bytes = NULL;
        size_t unit = stride;
        if (BS_T_NULL == tag) {
            memset(target, 0, nbytes);
            return SHREWD_BASIC_ERR_OK;
        }
        if (BS_T_STRING != tag) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = bs_r_le(r, 4, &n);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if ((0 == n) || (n > (uint64_t)binio_r_left(r))) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = binio_r_bytes(r, (size_t)n, &bytes);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        {
            size_t len = (size_t)n - 1; /* drop trailing NUL */
            if (1 == unit) {
                if (len > nbytes) {
                    return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                }
                memcpy(target, bytes, len);
                if (len < nbytes) {
                    target[len] = '\0';
                }
                return SHREWD_BASIC_ERR_OK;
            }
            {
                size_t needed = 0;
                status = reflect_units_from_utf8((const char *)bytes, len, unit, NULL, 0, &needed);
                if ((SHREWD_BASIC_ERR_OK == status) && (needed > (nbytes / unit))) {
                    return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                }
                if (SHREWD_BASIC_ERR_OK == status) {
                    memset(target, 0, nbytes);
                    status = reflect_units_from_utf8(
                        (const char *)bytes, len, unit, target, nbytes / unit, &needed);
                }
                return status;
            }
        }
    }
    if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
        if (BS_T_ARRAY != tag) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        /* Elements carry only the pointer shape flag (mirrors encode). */
        return bs_decode_array(r,
                               type,
                               flags & (uint32_t)SHREWD_BASIC_REFLECT_FIELD_F_PTR,
                               target,
                               nbytes / stride,
                               stride,
                               depth);
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL: {
        uint8_t byte = 0;
        if (BS_T_BOOL != tag) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = binio_r_u8(r, &byte);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(target, nbytes, (0 != byte) ? 1u : 0u);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        if (BS_T_STRING == tag) {
            uint64_t n = 0;
            const unsigned char *bytes = NULL;
            status = bs_r_le(r, 4, &n);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = binio_r_bytes(r, (size_t)n, &bytes);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (2 != n) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            memcpy(target, bytes, 1);
            return SHREWD_BASIC_ERR_OK;
        }
        {
            int64_t number = 0;
            status = bs_wire_int(r, tag, INT8_MIN, INT8_MAX, &number);
            if (SHREWD_BASIC_ERR_OK == status) {
                char c = (char)number;
                memcpy(target, &c, 1);
            }
            return status;
        }
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64: {
        int64_t lo = 0;
        int64_t hi = 0;
        int64_t number = 0;
        bs_int_bounds(type->sb_refl_kind, &lo, &hi);
        status = bs_wire_int(r, tag, lo, hi, &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(target, nbytes, (uint64_t)number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64: {
        int64_t number = 0;
        uint64_t hi = bs_uint_limit(type->sb_refl_kind);
        status = bs_wire_int(
            r, tag, 0, (int64_t)((hi > (uint64_t)INT64_MAX) ? (uint64_t)INT64_MAX : hi), &number);
        if ((SHREWD_BASIC_ERR_OK == status) && ((uint64_t)number > hi)) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(target, nbytes, (uint64_t)number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        if (BS_T_STRING == tag) {
            uint64_t n = 0;
            const unsigned char *bytes = NULL;
            status = bs_r_le(r, 4, &n);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if ((0 == n) || (n > (uint64_t)binio_r_left(r)) || (n > 128u)) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            status = binio_r_bytes(r, (size_t)n, &bytes);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            {
                char name[128];
                memcpy(name, bytes, (size_t)n - 1);
                name[n - 1] = '\0';
                for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
                    if (0 == strcmp(type->sb_refl_enum[i].sb_refl_name, name)) {
                        reflect_store_u64(
                            target, nbytes, (uint64_t)type->sb_refl_enum[i].sb_refl_value);
                        return SHREWD_BASIC_ERR_OK;
                    }
                }
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
        }
        {
            int64_t number = 0;
            status = bs_wire_int(r, tag, INT64_MIN, INT64_MAX, &number);
            if (SHREWD_BASIC_ERR_OK == status) {
                reflect_store_u64(target, nbytes, (uint64_t)number);
            }
            return status;
        }
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        long double number = 0.0L;
        if (BS_T_DOUBLE == tag) {
            uint64_t bits = 0;
            double real = 0.0;
            status = bs_r_le(r, 8, &bits);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            memcpy(&real, &bits, sizeof(real));
            number = (long double)real;
        } else {
            int64_t whole = 0;
            status = bs_wire_int(r, tag, INT64_MIN, INT64_MAX, &whole);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            number = (long double)whole;
        }
        return bs_store_number(number, type->sb_refl_kind, target, nbytes);
    }
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX: {
        size_t half = nbytes / 2;
        const shrewd_basic_reflect_type_t *part = (4 == half)   ? &shrewd_basic_reflect_float
                                                  : (8 == half) ? &shrewd_basic_reflect_double
                                                                : &shrewd_basic_reflect_long_double;
        if (BS_T_ARRAY != tag) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        return bs_decode_array(r, part, 0, target, 2, half, depth + 1);
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING:
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32: {
        uint64_t n = 0;
        const unsigned char *bytes = NULL;
        void *block = NULL;
        if (BS_T_NULL == tag) {
            memset(target, 0, nbytes);
            return SHREWD_BASIC_ERR_OK;
        }
        if (BS_T_STRING != tag) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = bs_r_le(r, 4, &n);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if ((0 == n) || (n > (uint64_t)binio_r_left(r))) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = binio_r_bytes(r, (size_t)n, &bytes);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        {
            size_t len = (size_t)n - 1;
            if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
                if (SHREWD_BASIC_ERR_OK
                    != shrewd_basic_platform_alloc(&block, (0 < len) ? (len + 1) : 1)) {
                    return SHREWD_BASIC_ERR_MEM_NOMEM;
                }
                memcpy(block, bytes, len);
                ((char *)block)[len] = '\0';
                memcpy(target, &block, sizeof(block));
                return SHREWD_BASIC_ERR_OK;
            }
            {
                size_t unit = reflect_string_unit(type->sb_refl_kind);
                size_t needed = 0;
                status = reflect_units_from_utf8((const char *)bytes, len, unit, NULL, 0, &needed);
                if ((SHREWD_BASIC_ERR_OK == status)
                    && (SHREWD_BASIC_ERR_OK
                        != shrewd_basic_platform_alloc(&block, (needed + 1) * unit))) {
                    status = SHREWD_BASIC_ERR_MEM_NOMEM;
                }
                if (SHREWD_BASIC_ERR_OK == status) {
                    memset(block, 0, (needed + 1) * unit);
                    status = reflect_units_from_utf8(
                        (const char *)bytes, len, unit, block, needed + 1, &needed);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    void *old = block;
                    (void)shrewd_basic_platform_free(&old);
                    return status;
                }
                memcpy(target, &block, sizeof(block));
                return SHREWD_BASIC_ERR_OK;
            }
        }
    }
    case SHREWD_BASIC_REFLECT_KIND_STRUCT:
        if (BS_T_DOC != tag) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        return bs_decode_fields(r, type, target, depth);
    default:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
}

static int bs_decode_fields(binio_rd_t *r,
                            const shrewd_basic_reflect_type_t *type,
                            unsigned char *object,
                            size_t depth) {
    uint64_t total = 0;
    size_t start = 0;
    size_t end = 0;
    unsigned char *seen = NULL;
    int status = 0;
    if (BS_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    status = bs_r_le(r, 4, &total);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if ((5 > total) || ((total - 4) > (uint64_t)binio_r_left(r))) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    start = r->pos - 4;
    end = start + (size_t)total;
    if ((0 < type->sb_refl_field_count)
        && (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_calloc((void **)&seen, type->sb_refl_field_count, 1))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    while ((r->pos + 1) < end) {
        uint8_t tag = 0;
        char key[256];
        const shrewd_basic_reflect_field_t *field = NULL;
        status = binio_r_u8(r, &tag);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        if (0 == tag) {
            status = SHREWD_BASIC_ERR_BIN_FORMAT;
            break;
        }
        status = bs_read_cstring(r, key, sizeof(key));
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        if ((SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_type_find_field(type, key, &field))
            && (0 == bs_refused(field))
            && (0 == bs_multidim(field->sb_refl_type, field->sb_refl_flags))) {
            size_t slot = (size_t)(field - type->sb_refl_fields);
            if (0 == seen[slot]) {
                seen[slot] = 1;
                status = bs_decode_payload(r,
                                           tag,
                                           field->sb_refl_type,
                                           field->sb_refl_flags,
                                           object + field->sb_refl_offset,
                                           field->sb_refl_size,
                                           field->sb_refl_stride,
                                           depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    break;
                }
                continue;
            }
        }
        /* unknown key, duplicate key (first wins) or refused shape */
        status = bs_skip_value(r, tag);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
    }
    {
        void *old = seen;
        (void)shrewd_basic_platform_free(&old);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if ((r->pos + 1) != end) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    {
        uint8_t term = 0;
        status = binio_r_u8(r, &term);
        if ((SHREWD_BASIC_ERR_OK != status) || (0 != term)) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_bson_decode(const void *bytes,
                             size_t size,
                             void *object,
                             const shrewd_basic_reflect_type_t *type) {
    binio_rd_t r;
    int status = 0;
    if ((NULL == bytes) || (NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(object, 0, type->sb_refl_size);
    binio_rd_init(&r, bytes, size);
    status = bs_decode_fields(&r, type, (unsigned char *)object, 0);
    if ((SHREWD_BASIC_ERR_OK == status) && (0 != binio_r_left(&r))) {
        status = SHREWD_BASIC_ERR_BIN_FORMAT; /* trailing bytes */
    }
    return status;
}

void shrewd_basic_bson_bytes_free(void *bytes) {
    if (NULL != bytes) {
        (void)shrewd_basic_platform_free(&bytes);
    }
}

/* --- free (contents only - the root stays the caller's) ------------------- */

static void bs_free_fields(const shrewd_basic_reflect_type_t *type, void *object);

static void bs_free_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth) {
    if (BS_MAX_DEPTH <= depth) {
        return;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind)
        || (SHREWD_BASIC_REFLECT_KIND_STRING16 == type->sb_refl_kind)
        || (SHREWD_BASIC_REFLECT_KIND_STRING32 == type->sb_refl_kind)) {
        void *pointer = NULL;
        memcpy(&pointer, value, sizeof(pointer));
        if (NULL != pointer) {
            (void)shrewd_basic_platform_free(&pointer);
            memset(value, 0, sizeof(void *));
        }
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        bs_free_fields(type, value);
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            bs_free_value(type->sb_refl_elem,
                          (unsigned char *)value + (i * type->sb_refl_elem_stride),
                          depth + 1);
        }
    }
}

static void bs_free_fields(const shrewd_basic_reflect_type_t *type, void *object) {
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        unsigned char *at = (unsigned char *)object + field->sb_refl_offset;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            continue; /* inline buffer */
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            void *pointer = NULL;
            memcpy(&pointer, at, sizeof(pointer));
            if (NULL != pointer) {
                bs_free_value(field->sb_refl_type, pointer, 0);
                (void)shrewd_basic_platform_free(&pointer);
                memset(at, 0, sizeof(void *));
            }
            continue;
        }
        bs_free_value(field->sb_refl_type, at, 0);
    }
}

int shrewd_basic_bson_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    bs_free_fields(type, object);
    return SHREWD_BASIC_ERR_OK;
}

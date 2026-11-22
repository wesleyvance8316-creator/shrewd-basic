/**
 * @file cbor.c
 * @brief struct <-> CBOR (RFC 8949 subset) over the shared binary
 *        skeleton (bin-internal.h). Shape mapping identical to the
 *        json/msgpack layers; only the wire encodings differ.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic-internal/bin-internal.h"
#include "shrewd-basic-internal/reflect-internal.h"
#include "shrewd-basic/formats/cbor.h"
#include "shrewd-basic/platform/memory.h"

#define CB_MAX_DEPTH 64

/* --- wire encodings (encode) -------------------------------------------- */

/* Initial byte + minimal argument: major<<5 | ai (RFC 8949 §3). */
static int cb_head(binio_buf_t *b, unsigned major, uint64_t n) {
    uint8_t lead = (uint8_t)((major << 5) & 0xe0u);
    if (n < 24u) {
        return binio_w_u8(b, (uint8_t)(lead | n));
    }
    if (n <= UINT8_MAX) {
        int status = binio_w_u8(b, (uint8_t)(lead | 24u));
        return (SHREWD_BASIC_ERR_OK == status) ? binio_w_u8(b, (uint8_t)n) : status;
    }
    if (n <= UINT16_MAX) {
        int status = binio_w_u8(b, (uint8_t)(lead | 25u));
        return (SHREWD_BASIC_ERR_OK == status) ? binio_w_be(b, n, 2) : status;
    }
    if (n <= UINT32_MAX) {
        int status = binio_w_u8(b, (uint8_t)(lead | 26u));
        return (SHREWD_BASIC_ERR_OK == status) ? binio_w_be(b, n, 4) : status;
    }
    {
        int status = binio_w_u8(b, (uint8_t)(lead | 27u));
        return (SHREWD_BASIC_ERR_OK == status) ? binio_w_be(b, n, 8) : status;
    }
}

static int cb_int(binio_buf_t *b, int64_t value) {
    if (0 <= value) {
        return cb_head(b, 0, (uint64_t)value);
    }
    /* Negative: major 1 carries -1 - value. */
    return cb_head(b, 1, (uint64_t)(-1 - value));
}

static int cb_f32(binio_buf_t *b, float value) {
    uint32_t bits = 0;
    int status = binio_w_u8(b, 0xfau);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(&bits, &value, sizeof(bits));
    return binio_w_be(b, (uint64_t)bits, 4);
}

static int cb_f64(binio_buf_t *b, double value) {
    uint64_t bits = 0;
    int status = binio_w_u8(b, 0xfbu);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(&bits, &value, sizeof(bits));
    return binio_w_be(b, bits, 8);
}

/* long double -> f64 with the precision guard (never silently narrow). */
static int cb_ld_number(binio_buf_t *b, long double value) {
    double narrowed = (double)value;
    if ((long double)narrowed != value) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    return cb_f64(b, narrowed);
}

static int cb_text(binio_buf_t *b, const char *text, size_t n) {
    int status = cb_head(b, 3, (uint64_t)n);
    return (SHREWD_BASIC_ERR_OK == status) ? binio_w_bytes(b, text, n) : status;
}

/* --- refusals ------------------------------------------------------------ */

static int cb_refused(const shrewd_basic_reflect_field_t *field) {
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

/* --- encode -------------------------------------------------------------- */

static int cb_encode_value(binio_buf_t *b,
                           const shrewd_basic_reflect_type_t *type,
                           const unsigned char *value,
                           size_t nbytes,
                           size_t depth);

static int cb_encode_fields(binio_buf_t *b,
                            const shrewd_basic_reflect_type_t *type,
                            const unsigned char *object,
                            size_t depth) {
    int status = cb_head(b, 5, (uint64_t)type->sb_refl_field_count);
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_field_count); ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const unsigned char *at = object + field->sb_refl_offset;
        if (0 != cb_refused(field)) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
        }
        status = cb_text(b, field->sb_refl_name, strlen(field->sb_refl_name));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            size_t unit = field->sb_refl_stride;
            if (1 == unit) {
                status = cb_text(
                    b, (const char *)at, reflect_strn_len((const char *)at, field->sb_refl_count));
            } else {
                /* Wide buffer -> UTF-8 text (strict). */
                size_t len = 0;
                char stack[256];
                char *heap = NULL;
                char *out = stack;
                status = reflect_utf8_from_units(at, unit, field->sb_refl_count, NULL, 0, &len);
                if ((SHREWD_BASIC_ERR_OK == status) && (len >= sizeof(stack))) {
                    if (SHREWD_BASIC_ERR_OK
                        != shrewd_basic_platform_alloc((void **)&heap, len + 1)) {
                        return SHREWD_BASIC_ERR_MEM_NOMEM;
                    }
                    out = heap;
                }
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = reflect_utf8_from_units(
                        at, unit, field->sb_refl_count, out, len + 1, &len);
                }
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = cb_text(b, out, len);
                }
                if (NULL != heap) {
                    void *old = heap;
                    (void)shrewd_basic_platform_free(&old);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                continue;
            }
        } else if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            const void *target = reflect_load_ptr(at);
            if (NULL == target) {
                status = binio_w_u8(b, 0xf6u); /* null */
                continue;
            }
            status = cb_encode_value(
                b, field->sb_refl_type, target, field->sb_refl_type->sb_refl_size, depth + 1);
            continue;
        } else if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
            status = cb_head(b, 4, (uint64_t)field->sb_refl_count);
            for (size_t j = 0; (SHREWD_BASIC_ERR_OK == status) && (j < field->sb_refl_count); ++j) {
                const unsigned char *elem = at + (j * field->sb_refl_stride);
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    const void *target = reflect_load_ptr(elem);
                    if (NULL == target) {
                        status = binio_w_u8(b, 0xf6u);
                    } else {
                        status = cb_encode_value(b,
                                                 field->sb_refl_type,
                                                 target,
                                                 field->sb_refl_type->sb_refl_size,
                                                 depth + 1);
                    }
                } else {
                    status = cb_encode_value(
                        b, field->sb_refl_type, elem, field->sb_refl_stride, depth + 1);
                }
            }
            continue;
        } else {
            status = cb_encode_value(b, field->sb_refl_type, at, field->sb_refl_stride, depth + 1);
            continue;
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return status;
}

static int cb_encode_value(binio_buf_t *b,
                           const shrewd_basic_reflect_type_t *type,
                           const unsigned char *value,
                           size_t nbytes,
                           size_t depth) {
    if (CB_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return binio_w_u8(b, (0 != reflect_load_u64(value, nbytes)) ? 0xf5u : 0xf4u);
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        memcpy(&c, value, 1);
        return cb_text(b, &c, 1);
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        return cb_int(b, reflect_load_i64(value, nbytes));
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        return cb_head(b, 0, reflect_load_u64(value, nbytes));
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = reflect_load_i64(value, nbytes);
        for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
            if (type->sb_refl_enum[i].sb_refl_value == number) {
                const char *name = type->sb_refl_enum[i].sb_refl_name;
                return cb_text(b, name, strlen(name));
            }
        }
        return cb_int(b, number);
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
        return cb_f32(b, (float)reflect_load_f64(value, nbytes));
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        return cb_f64(b, reflect_load_f64(value, nbytes));
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE:
        return cb_ld_number(b, reflect_load_ld(value, nbytes));
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX: {
        size_t half = nbytes / 2;
        int status = cb_head(b, 4, 2);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = cb_ld_number(b, reflect_load_cpart(value, half));
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = cb_ld_number(b, reflect_load_cpart(value + half, half));
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = reflect_load_str(value);
        return cb_text(b, (NULL != text) ? text : "", (NULL != text) ? strlen(text) : 0);
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32: {
        const void *text = reflect_load_ptr(value);
        size_t unit = reflect_string_unit(type->sb_refl_kind);
        size_t len = 0;
        char stack[256];
        char *heap = NULL;
        char *out = stack;
        int status = 0;
        if (NULL == text) {
            return cb_text(b, "", 0);
        }
        status = reflect_utf8_from_units(text, unit, SIZE_MAX, NULL, 0, &len);
        if ((SHREWD_BASIC_ERR_OK == status) && (len >= sizeof(stack))) {
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&heap, len + 1)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            out = heap;
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = reflect_utf8_from_units(text, unit, SIZE_MAX, out, len + 1, &len);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = cb_text(b, out, len);
        }
        if (NULL != heap) {
            void *old = heap;
            (void)shrewd_basic_platform_free(&old);
        }
        return status;
    }
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        int status = cb_head(b, 4, (uint64_t)type->sb_refl_elem_count);
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_elem_count); ++i) {
            status = cb_encode_value(b,
                                     type->sb_refl_elem,
                                     value + (i * type->sb_refl_elem_stride),
                                     type->sb_refl_elem_stride,
                                     depth + 1);
        }
        return status;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        return cb_encode_fields(b, type, value, depth);
    }
    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
}

int shrewd_basic_cbor_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             void **out_bytes,
                             size_t *out_size) {
    binio_buf_t buf;
    int status = 0;
    if ((NULL == object) || (NULL == type) || (NULL == out_bytes) || (NULL == out_size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&buf, 0, sizeof(buf));
    status = cb_encode_fields(&buf, type, (const unsigned char *)object, 0);
    if (SHREWD_BASIC_ERR_OK != status) {
        binio_buf_free(&buf);
        return status;
    }
    *out_bytes = buf.data;
    *out_size = buf.len;
    return SHREWD_BASIC_ERR_OK;
}

/* --- decode -------------------------------------------------------------- */

/* One CBOR item head: major + minimal argument. Indefinite (ai 31),
   reserved ai 28..30 and tags are refused here. */
typedef struct {
    unsigned major;
    uint8_t ai;
    uint64_t aux;
} cb_wire_t;

/*
 * One item head: the argument (aux) is already consumed for ai 24..27 -
 * for major 7 the argument IS the float payload. Tags (major 6),
 * indefinite lengths (ai 31) and simple extras (major 7, ai 24) are
 * refused at the item level.
 */
static int cb_read_item(binio_rd_t *r, cb_wire_t *out) {
    uint8_t lead = 0;
    uint8_t ai = 0;
    int status = binio_r_u8(r, &lead);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    out->major = (unsigned)(lead >> 5);
    ai = (uint8_t)(lead & 0x1fu);
    out->ai = ai;
    if (6u == out->major) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* tags */
    }
    if (ai < 24u) {
        out->aux = ai;
        return SHREWD_BASIC_ERR_OK;
    }
    if (24u == ai) {
        uint8_t n = 0;
        status = binio_r_u8(r, &n);
        out->aux = n;
        if ((SHREWD_BASIC_ERR_OK == status) && (7u == out->major)) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* simple extras */
        }
        return status;
    }
    if (25u == ai) {
        return binio_r_be(r, 2, &out->aux);
    }
    if (26u == ai) {
        return binio_r_be(r, 4, &out->aux);
    }
    if (27u == ai) {
        return binio_r_be(r, 8, &out->aux);
    }
    if (31u == ai) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* indefinite lengths */
    }
    return SHREWD_BASIC_ERR_BIN_FORMAT; /* ai 28..30 reserved */
}

/* Consume any item (unknown keys / unused values). */
static int cb_skip(binio_rd_t *r, size_t depth) {
    cb_wire_t item = {0};
    int status = cb_read_item(r, &item);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (CB_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    switch (item.major) {
    case 0:
    case 1:
        return SHREWD_BASIC_ERR_OK;
    case 2:
    case 3: {
        const unsigned char *bytes = NULL;
        return binio_r_bytes(r, (size_t)item.aux, &bytes);
    }
    case 4:
        for (uint64_t i = 0; i < item.aux; ++i) {
            status = cb_skip(r, depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    case 5:
        for (uint64_t i = 0; i < item.aux; ++i) {
            status = cb_skip(r, depth + 1);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = cb_skip(r, depth + 1);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    default:
        /* major 7: simple values and floats carry no payload beyond the
           argument (f16/f32/f64 argument is inline in the head). */
        return SHREWD_BASIC_ERR_OK;
    }
}

/* Decode one value (as produced by cb_encode_value) into target. */
static int cb_decode_value(binio_rd_t *r,
                           const shrewd_basic_reflect_type_t *type,
                           unsigned char *target,
                           size_t nbytes,
                           size_t depth);

/* Text/bytes item into a freshly allocated NUL-terminated buffer. */
static int cb_read_text_alloc(binio_rd_t *r,
                              const cb_wire_t *item,
                              char **out_text,
                              size_t *out_len) {
    const unsigned char *bytes = NULL;
    void *block = NULL;
    int status = 0;
    if (3u != item->major) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    status = binio_r_bytes(r, (size_t)item->aux, &bytes);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, (size_t)item->aux + 1)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    memcpy(block, bytes, (size_t)item->aux);
    ((char *)block)[item->aux] = '\0';
    *out_text = block;
    *out_len = (size_t)item->aux;
    return SHREWD_BASIC_ERR_OK;
}

/* Integer item -> checked range (lo..hi), REFLECT_MISMATCH outside. */
static int cb_wire_int(const cb_wire_t *item, int64_t lo, int64_t hi, int64_t *out) {
    if (0u == item->major) {
        if (item->aux > (uint64_t)hi) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        *out = (int64_t)item->aux;
        return SHREWD_BASIC_ERR_OK;
    }
    if (1u == item->major) {
        /* payload n encodes the value -1 - n. */
        uint64_t n = item->aux;
        if (n > (uint64_t)INT64_MAX) {
            if (lo > INT64_MIN) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            *out = INT64_MIN;
            return (INT64_MIN < lo) ? SHREWD_BASIC_ERR_REFLECT_MISMATCH : SHREWD_BASIC_ERR_OK;
        }
        *out = -1 - (int64_t)n;
        return ((*out < lo) || (*out > hi)) ? SHREWD_BASIC_ERR_REFLECT_MISMATCH
                                            : SHREWD_BASIC_ERR_OK;
    }
    return SHREWD_BASIC_ERR_BIN_FORMAT;
}

static int cb_wire_uint(const cb_wire_t *item, uint64_t hi, uint64_t *out) {
    if (0u != item->major) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if (item->aux > hi) {
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    *out = item->aux;
    return SHREWD_BASIC_ERR_OK;
}

static void cb_int_bounds(shrewd_basic_reflect_kind_t kind, int64_t *lo, int64_t *hi) {
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

static uint64_t cb_uint_limit(shrewd_basic_reflect_kind_t kind) {
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

static int cb_decode_value(binio_rd_t *r,
                           const shrewd_basic_reflect_type_t *type,
                           unsigned char *target,
                           size_t nbytes,
                           size_t depth) {
    cb_wire_t item = {0};
    int status = 0;
    if (CB_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    status = cb_read_item(r, &item);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        if ((7u == item.major) && ((20u == item.ai) || (21u == item.ai))) {
            reflect_store_u64(target, nbytes, (21u == item.ai) ? 1u : 0u);
            return SHREWD_BASIC_ERR_OK;
        }
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char *text = NULL;
        size_t len = 0;
        if (3u == item.major) {
            status = cb_read_text_alloc(r, &item, &text, &len);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (1 != len) {
                void *old = text;
                (void)shrewd_basic_platform_free(&old);
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            memcpy(target, text, 1);
            {
                void *old = text;
                (void)shrewd_basic_platform_free(&old);
            }
            return SHREWD_BASIC_ERR_OK;
        }
        {
            int64_t number = 0;
            status = cb_wire_int(&item, INT8_MIN, INT8_MAX, &number);
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
        cb_int_bounds(type->sb_refl_kind, &lo, &hi);
        status = cb_wire_int(&item, lo, hi, &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(target, nbytes, (uint64_t)number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64: {
        uint64_t number = 0;
        status = cb_wire_uint(&item, cb_uint_limit(type->sb_refl_kind), &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(target, nbytes, number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        if (3u == item.major) {
            char *text = NULL;
            size_t len = 0;
            status = cb_read_text_alloc(r, &item, &text, &len);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
                if (0 == strcmp(type->sb_refl_enum[i].sb_refl_name, text)) {
                    reflect_store_u64(
                        target, nbytes, (uint64_t)type->sb_refl_enum[i].sb_refl_value);
                    {
                        void *old = text;
                        (void)shrewd_basic_platform_free(&old);
                    }
                    return SHREWD_BASIC_ERR_OK;
                }
            }
            {
                void *old = text;
                (void)shrewd_basic_platform_free(&old);
            }
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        {
            int64_t number = 0;
            status = cb_wire_int(&item, INT64_MIN, INT64_MAX, &number);
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
        if ((7u == item.major) && (27u == item.ai)) {
            /* The item argument IS the f64 payload. */
            uint64_t bits = item.aux;
            double real = 0.0;
            memcpy(&real, &bits, sizeof(real));
            number = (long double)real;
        } else if ((7u == item.major) && (26u == item.ai)) {
            uint32_t bits = (uint32_t)item.aux;
            float single = 0.0f;
            memcpy(&single, &bits, sizeof(single));
            number = (long double)single;
        } else if ((7u == item.major) && (25u == item.ai)) {
            {
                /* Half precision -> float (decode only; never emitted). */
                uint16_t half = (uint16_t)item.aux;
                uint32_t sign = ((uint32_t)half & 0x8000u) << 16;
                uint32_t exp = (uint32_t)((half >> 10) & 0x1fu);
                uint32_t frac = (uint32_t)(half & 0x3ffu);
                uint32_t bits = 0;
                float single = 0.0f;
                if (0u == exp) {
                    bits = sign; /* +-0 (subnormals flush to zero) */
                } else if (31u == exp) {
                    bits = sign | 0x7f800000u | (frac << 13);
                } else {
                    bits = sign | ((exp + 112u) << 23) | (frac << 13);
                }
                memcpy(&single, &bits, sizeof(single));
                number = (long double)single;
            }
        } else if ((0u == item.major) || (1u == item.major)) {
            int64_t whole = 0;
            status = cb_wire_int(&item, INT64_MIN, INT64_MAX, &whole);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            number = (long double)whole;
        } else {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        if (SHREWD_BASIC_REFLECT_KIND_FLOAT == type->sb_refl_kind) {
            float single = (float)number;
            if ((long double)single != number) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            memcpy(target, &single, sizeof(single));
        } else if (SHREWD_BASIC_REFLECT_KIND_DOUBLE == type->sb_refl_kind) {
            double real = (double)number;
            if ((long double)real != number) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            memcpy(target, &real, sizeof(real));
        } else {
            memcpy(target, &number, sizeof(number));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX: {
        size_t half = nbytes / 2;
        if ((4u != item.major) || (2u != item.aux)) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        for (size_t i = 0; i < 2; ++i) {
            /* Components encode as guarded numbers; store at half width. */
            status = cb_decode_value(r,
                                     (4 == half)   ? &shrewd_basic_reflect_float
                                     : (8 == half) ? &shrewd_basic_reflect_double
                                                   : &shrewd_basic_reflect_long_double,
                                     target + (i * half),
                                     half,
                                     depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING:
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32: {
        char *text = NULL;
        size_t len = 0;
        void *block = NULL;
        if ((7u == item.major) && (22u == item.ai)) {
            memset(target, 0, nbytes);
            return SHREWD_BASIC_ERR_OK;
        }
        status = cb_read_text_alloc(r, &item, &text, &len);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
            block = text;
            memcpy(target, &block, sizeof(block));
            return SHREWD_BASIC_ERR_OK;
        }
        {
            size_t unit = reflect_string_unit(type->sb_refl_kind);
            size_t needed = 0;
            status = reflect_units_from_utf8(text, len, unit, NULL, 0, &needed);
            if ((SHREWD_BASIC_ERR_OK == status)
                && (SHREWD_BASIC_ERR_OK
                    != shrewd_basic_platform_alloc(&block, (needed + 1) * unit))) {
                status = SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                memset(block, 0, (needed + 1) * unit);
                status = reflect_units_from_utf8(text, len, unit, block, needed + 1, &needed);
            }
            {
                void *old = text;
                (void)shrewd_basic_platform_free(&old);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                void *dead = block;
                (void)shrewd_basic_platform_free(&dead);
                return status;
            }
            memcpy(target, &block, sizeof(block));
            return SHREWD_BASIC_ERR_OK;
        }
    }
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        uint64_t count = 0;
        if (4u != item.major) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        count = item.aux;
        if (count > (uint64_t)type->sb_refl_elem_count) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        for (uint64_t i = 0; i < count; ++i) {
            status = cb_decode_value(r,
                                     type->sb_refl_elem,
                                     target + (i * type->sb_refl_elem_stride),
                                     type->sb_refl_elem_stride,
                                     depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        if (5u != item.major) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        for (uint64_t i = 0; i < item.aux; ++i) {
            cb_wire_t key = {0};
            char *name = NULL;
            size_t len = 0;
            const shrewd_basic_reflect_field_t *field = NULL;
            status = cb_read_item(r, &key);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = cb_read_text_alloc(r, &key, &name, &len);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_type_find_field(type, name, &field)) {
                if (0 != cb_refused(field)) {
                    void *old = name;
                    (void)shrewd_basic_platform_free(&old);
                    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
                }
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
                    /* Text into a buffer field. */
                    cb_wire_t item2 = {0};
                    char *text = NULL;
                    size_t tlen = 0;
                    status = cb_read_item(r, &item2);
                    if (SHREWD_BASIC_ERR_OK == status) {
                        status = cb_read_text_alloc(r, &item2, &text, &tlen);
                    }
                    if (SHREWD_BASIC_ERR_OK == status) {
                        size_t unit = field->sb_refl_stride;
                        unsigned char *at = target + field->sb_refl_offset;
                        if (1 == unit) {
                            if (tlen > field->sb_refl_count) {
                                status = SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                            } else {
                                memcpy(at, text, tlen);
                                if (tlen < field->sb_refl_count) {
                                    at[tlen] = '\0';
                                }
                            }
                        } else {
                            size_t needed = 0;
                            status = reflect_units_from_utf8(text, tlen, unit, NULL, 0, &needed);
                            if ((SHREWD_BASIC_ERR_OK == status)
                                && (needed > field->sb_refl_count)) {
                                status = SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                            }
                            if (SHREWD_BASIC_ERR_OK == status) {
                                memset(at, 0, field->sb_refl_count * unit);
                                status = reflect_units_from_utf8(
                                    text, tlen, unit, at, field->sb_refl_count, &needed);
                            }
                        }
                    }
                    if (NULL != text) {
                        void *old = text;
                        (void)shrewd_basic_platform_free(&old);
                    }
                } else if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    /* Peek the next item: null leaves the pointer zero. */
                    cb_wire_t item2 = {0};
                    size_t save = r->pos;
                    status = cb_read_item(r, &item2);
                    r->pos = save;
                    if (SHREWD_BASIC_ERR_OK != status) {
                        void *old = name;
                        (void)shrewd_basic_platform_free(&old);
                        return status;
                    }
                    if ((7u == item2.major) && (22u == item2.ai)) {
                        (void)cb_skip(r, depth + 1);
                    } else {
                        void *block = NULL;
                        size_t want = field->sb_refl_type->sb_refl_size;
                        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, want)) {
                            void *old = name;
                            (void)shrewd_basic_platform_free(&old);
                            return SHREWD_BASIC_ERR_MEM_NOMEM;
                        }
                        status = cb_decode_value(r, field->sb_refl_type, block, want, depth + 1);
                        if (SHREWD_BASIC_ERR_OK == status) {
                            memcpy(target + field->sb_refl_offset, &block, sizeof(block));
                        } else {
                            void *old = block;
                            (void)shrewd_basic_platform_free(&old);
                        }
                    }
                } else if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
                    cb_wire_t item2 = {0};
                    unsigned char *at = target + field->sb_refl_offset;
                    status = cb_read_item(r, &item2);
                    if ((SHREWD_BASIC_ERR_OK == status) && (4u != item2.major)) {
                        status = SHREWD_BASIC_ERR_BIN_FORMAT;
                    }
                    if ((SHREWD_BASIC_ERR_OK == status)
                        && (item2.aux > (uint64_t)field->sb_refl_count)) {
                        status = SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                    }
                    for (uint64_t j = 0; (SHREWD_BASIC_ERR_OK == status) && (j < item2.aux); ++j) {
                        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                            cb_wire_t item3 = {0};
                            size_t save = r->pos;
                            status = cb_read_item(r, &item3);
                            r->pos = save;
                            if (SHREWD_BASIC_ERR_OK != status) {
                                break;
                            }
                            if ((7u == item3.major) && (22u == item3.ai)) {
                                (void)cb_skip(r, depth + 1);
                            } else {
                                void *block = NULL;
                                size_t want = field->sb_refl_type->sb_refl_size;
                                if (SHREWD_BASIC_ERR_OK
                                    != shrewd_basic_platform_calloc(&block, 1, want)) {
                                    status = SHREWD_BASIC_ERR_MEM_NOMEM;
                                    break;
                                }
                                status = cb_decode_value(
                                    r, field->sb_refl_type, block, want, depth + 1);
                                if (SHREWD_BASIC_ERR_OK == status) {
                                    memcpy(at + (j * field->sb_refl_stride), &block, sizeof(block));
                                } else {
                                    void *old = block;
                                    (void)shrewd_basic_platform_free(&old);
                                }
                            }
                        } else {
                            status = cb_decode_value(r,
                                                     field->sb_refl_type,
                                                     at + (j * field->sb_refl_stride),
                                                     field->sb_refl_stride,
                                                     depth + 1);
                        }
                    }
                } else {
                    status = cb_decode_value(r,
                                             field->sb_refl_type,
                                             target + field->sb_refl_offset,
                                             field->sb_refl_stride,
                                             depth + 1);
                }
            } else {
                status = cb_skip(r, depth + 1); /* unknown keys are ignored */
            }
            {
                void *old = name;
                (void)shrewd_basic_platform_free(&old);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
}

int shrewd_basic_cbor_decode(const void *bytes,
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
    status = cb_decode_value(&r, type, (unsigned char *)object, type->sb_refl_size, 0);
    if ((SHREWD_BASIC_ERR_OK == status) && (0 != binio_r_left(&r))) {
        status = SHREWD_BASIC_ERR_BIN_FORMAT; /* trailing bytes */
    }
    return status;
}

void shrewd_basic_cbor_bytes_free(void *bytes) {
    if (NULL != bytes) {
        (void)shrewd_basic_platform_free(&bytes);
    }
}

/* --- free (contents only - the root stays the caller's) ------------------ */

static void cb_free_fields(const shrewd_basic_reflect_type_t *type, void *object);

static void cb_free_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth) {
    if (CB_MAX_DEPTH <= depth) {
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
        void *pointer = NULL;
        memcpy(&pointer, value, sizeof(pointer));
        if (NULL != pointer) {
            (void)shrewd_basic_platform_free(&pointer);
            memset(value, 0, sizeof(void *));
        }
        return;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_STRING16 == type->sb_refl_kind)
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
        cb_free_fields(type, value);
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            cb_free_value(type->sb_refl_elem,
                          (unsigned char *)value + (i * type->sb_refl_elem_stride),
                          depth + 1);
        }
    }
}

static void cb_free_fields(const shrewd_basic_reflect_type_t *type, void *object) {
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
                cb_free_value(field->sb_refl_type, pointer, 0);
                (void)shrewd_basic_platform_free(&pointer);
                memset(at, 0, sizeof(void *));
            }
            continue;
        }
        cb_free_value(field->sb_refl_type, at, 0);
    }
}

int shrewd_basic_cbor_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    cb_free_fields(type, object);
    return SHREWD_BASIC_ERR_OK;
}

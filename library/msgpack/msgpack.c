/**
 * @file msgpack.c
 * @brief MessagePack mapping over reflect descriptors (formats layer).
 *
 * Spec-faithful wire (minimal integer encodings, big-endian multi-byte
 * numbers), mapping shaped like the json layer: struct -> map of member
 * names, decode zeroes the target first and owns every pointer it
 * writes. v1 refuses the exotic shapes loudly (bit groups, unions,
 * flexible tails, dynamic arrays, bit-precise integers, opaque) instead
 * of guessing a wire for them.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "shrewd-basic/formats/msgpack.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic-internal/reflect-internal.h"

#define MP_MAX_DEPTH 64

/* --- writer -------------------------------------------------------------- */

typedef struct mp_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
} mp_buf_t;

static int mp_buf_reserve(mp_buf_t *b, size_t extra) {
    size_t want = 0;
    void *block = NULL;
    if (extra > (SIZE_MAX - b->len)) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    want = b->len + extra;
    if (want <= b->cap) {
        return SHREWD_BASIC_ERR_OK;
    }
    while (b->cap < want) {
        b->cap = (0 == b->cap) ? (size_t)256 : (b->cap * 2);
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, b->data, b->cap)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    b->data = block;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_w_u8(mp_buf_t *b, uint8_t byte) {
    int status = mp_buf_reserve(b, 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    b->data[b->len] = byte;
    b->len += 1;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_w_bytes(mp_buf_t *b, const void *bytes, size_t n) {
    int status = mp_buf_reserve(b, n);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 < n) {
        memcpy(b->data + b->len, bytes, n);
        b->len += n;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Big-endian multi-byte number (MessagePack wire order). */
static int mp_w_be(mp_buf_t *b, uint64_t value, size_t width) {
    int status = mp_buf_reserve(b, width);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (size_t i = 0; i < width; ++i) {
        b->data[b->len + i] = (uint8_t)((value >> (8u * (width - 1 - i))) & 0xffu);
    }
    b->len += width;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_w_uint(mp_buf_t *b, uint64_t value) {
    if (value < 0x80u) {
        return mp_w_u8(b, (uint8_t)value);
    }
    if (value <= UINT8_MAX) {
        int status = mp_w_u8(b, 0xccu);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_u8(b, (uint8_t)value) : status;
    }
    if (value <= UINT16_MAX) {
        int status = mp_w_u8(b, 0xcdu);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, value, 2) : status;
    }
    if (value <= UINT32_MAX) {
        int status = mp_w_u8(b, 0xceu);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, value, 4) : status;
    }
    {
        int status = mp_w_u8(b, 0xcfu);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, value, 8) : status;
    }
}

static int mp_w_int(mp_buf_t *b, int64_t value) {
    if (0 <= value) {
        return mp_w_uint(b, (uint64_t)value);
    }
    if (value >= -32) {
        return mp_w_u8(b, (uint8_t)(uint64_t)value);
    }
    if (value >= INT8_MIN) {
        int status = mp_w_u8(b, 0xd0u);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)value, 1) : status;
    }
    if (value >= INT16_MIN) {
        int status = mp_w_u8(b, 0xd1u);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)value, 2) : status;
    }
    if (value >= INT32_MIN) {
        int status = mp_w_u8(b, 0xd2u);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)value, 4) : status;
    }
    {
        int status = mp_w_u8(b, 0xd3u);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)value, 8) : status;
    }
}

static int mp_w_f32(mp_buf_t *b, float value) {
    uint32_t bits = 0;
    int status = mp_w_u8(b, 0xcau);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(&bits, &value, sizeof(bits));
    return mp_w_be(b, (uint64_t)bits, 4);
}

static int mp_w_f64(mp_buf_t *b, double value) {
    uint64_t bits = 0;
    int status = mp_w_u8(b, 0xcbu);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(&bits, &value, sizeof(bits));
    return mp_w_be(b, bits, 8);
}

static int mp_w_str_head(mp_buf_t *b, size_t n) {
    int status = SHREWD_BASIC_ERR_OK;
    if (n <= 31u) {
        return mp_w_u8(b, (uint8_t)(0xa0u | n));
    }
    if (n <= UINT8_MAX) {
        status = mp_w_u8(b, 0xd9u);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_u8(b, (uint8_t)n) : status;
    }
    if (n <= UINT16_MAX) {
        status = mp_w_u8(b, 0xdau);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)n, 2) : status;
    }
    if (n <= UINT32_MAX) {
        status = mp_w_u8(b, 0xdbu);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)n, 4) : status;
    }
    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
}

static int mp_w_str(mp_buf_t *b, const char *text, size_t n) {
    int status = mp_w_str_head(b, n);
    return (SHREWD_BASIC_ERR_OK == status) ? mp_w_bytes(b, text, n) : status;
}

static int mp_w_head(mp_buf_t *b, uint8_t fix_base, uint8_t wide16, uint8_t wide32, size_t n) {
    if (n <= 15u) {
        return mp_w_u8(b, (uint8_t)(fix_base | n));
    }
    if (n <= UINT16_MAX) {
        int status = mp_w_u8(b, wide16);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)n, 2) : status;
    }
    if (n <= UINT32_MAX) {
        int status = mp_w_u8(b, wide32);
        return (SHREWD_BASIC_ERR_OK == status) ? mp_w_be(b, (uint64_t)n, 4) : status;
    }
    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
}

/* --- reader -------------------------------------------------------------- */

typedef struct mp_rd {
    const unsigned char *data;
    size_t len;
    size_t pos;
} mp_rd_t;

static int mp_r_u8(mp_rd_t *r, uint8_t *out) {
    if ((r->len - r->pos) < 1u) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    *out = r->data[r->pos];
    r->pos += 1;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_r_be(mp_rd_t *r, size_t width, uint64_t *out) {
    uint64_t value = 0;
    if ((r->len - r->pos) < width) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    for (size_t i = 0; i < width; ++i) {
        value = (value << 8) | (uint64_t)r->data[r->pos + i];
    }
    r->pos += width;
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_r_bytes(mp_rd_t *r, size_t n, const unsigned char **out) {
    if ((r->len - r->pos) < n) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    *out = r->data + r->pos;
    r->pos += n;
    return SHREWD_BASIC_ERR_OK;
}

typedef enum mp_wire {
    MP_W_NIL,
    MP_W_BOOL,
    MP_W_INT,
    MP_W_UINT,
    MP_W_F32,
    MP_W_F64,
    MP_W_STR,
    MP_W_BIN,
    MP_W_EXT,
    MP_W_ARRAY,
    MP_W_MAP
} mp_wire_t;

/* Consume the type byte and its header; aux = count/length/value. */
static int mp_probe(mp_rd_t *r, mp_wire_t *kind, uint64_t *aux) {
    uint8_t byte = 0;
    int status = mp_r_u8(r, &byte);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (byte <= 0x7fu) {
        *kind = MP_W_UINT;
        *aux = (uint64_t)byte;
        return SHREWD_BASIC_ERR_OK;
    }
    if (byte >= 0xe0u) {
        *kind = MP_W_INT;
        *aux = (uint64_t)(int64_t)(int8_t)byte;
        return SHREWD_BASIC_ERR_OK;
    }
    if (0x80u == (byte & 0xf0u)) {
        *kind = MP_W_MAP;
        *aux = (uint64_t)(byte & 0x0fu);
        return SHREWD_BASIC_ERR_OK;
    }
    if (0x90u == (byte & 0xf0u)) {
        *kind = MP_W_ARRAY;
        *aux = (uint64_t)(byte & 0x0fu);
        return SHREWD_BASIC_ERR_OK;
    }
    if (0xa0u == (byte & 0xe0u)) {
        *kind = MP_W_STR;
        *aux = (uint64_t)(byte & 0x1fu);
        return SHREWD_BASIC_ERR_OK;
    }
    switch (byte) {
    case 0xc0u:
        *kind = MP_W_NIL;
        *aux = 0;
        return SHREWD_BASIC_ERR_OK;
    case 0xc2u:
    case 0xc3u:
        *kind = MP_W_BOOL;
        *aux = (uint64_t)(byte - 0xc2u);
        return SHREWD_BASIC_ERR_OK;
    case 0xc4u:
    case 0xc5u:
    case 0xc6u: {
        uint64_t n = 0;
        status = mp_r_be(r, (size_t)1u << (byte - 0xc4u), &n);
        *kind = MP_W_BIN;
        *aux = n;
        return status;
    }
    case 0xc7u:
    case 0xc8u:
    case 0xc9u: {
        uint64_t n = 0;
        status = mp_r_be(r, (size_t)1u << (byte - 0xc7u), &n);
        *kind = MP_W_EXT;
        *aux = n;
        return status;
    }
    case 0xcau:
        *kind = MP_W_F32;
        *aux = 0;
        return SHREWD_BASIC_ERR_OK;
    case 0xcbu:
        *kind = MP_W_F64;
        *aux = 0;
        return SHREWD_BASIC_ERR_OK;
    case 0xccu:
    case 0xcdu:
    case 0xceu:
    case 0xcfu: {
        status = mp_r_be(r, (size_t)1u << (byte - 0xccu), aux);
        *kind = MP_W_UINT;
        return status;
    }
    case 0xd0u:
    case 0xd1u:
    case 0xd2u:
    case 0xd3u: {
        size_t width = (size_t)1u << (byte - 0xd0u);
        status = mp_r_be(r, width, aux);
        *kind = MP_W_INT;
        if ((SHREWD_BASIC_ERR_OK == status) && (8u > width)) {
            /* Sign-extend the narrow wire value. */
            uint64_t sign = UINT64_C(1) << ((width * 8u) - 1u);
            if (0u != (*aux & sign)) {
                *aux |= ~((UINT64_C(1) << (width * 8u)) - 1u);
            }
        }
        return status;
    }
    case 0xd4u:
    case 0xd5u:
    case 0xd6u:
    case 0xd7u:
    case 0xd8u:
        *kind = MP_W_EXT;
        *aux = (uint64_t)1u << (byte - 0xd4u);
        return SHREWD_BASIC_ERR_OK;
    case 0xd9u:
    case 0xdau:
    case 0xdbu: {
        status = mp_r_be(r, (size_t)1u << (byte - 0xd9u), aux);
        *kind = MP_W_STR;
        return status;
    }
    case 0xdcu:
    case 0xddu: {
        status = mp_r_be(r, (size_t)2u << (byte - 0xdcu), aux);
        *kind = MP_W_ARRAY;
        return status;
    }
    case 0xdeu:
    case 0xdfu: {
        status = mp_r_be(r, (size_t)2u << (byte - 0xdeu), aux);
        *kind = MP_W_MAP;
        return status;
    }
    default:
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
}

/* Consume one wire value without decoding it. */
static int mp_skip_value(mp_rd_t *r, size_t depth) {
    mp_wire_t kind = MP_W_NIL;
    uint64_t aux = 0;
    const unsigned char *bytes = NULL;
    int status = 0;
    if (MP_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    status = mp_probe(r, &kind, &aux);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    switch (kind) {
    case MP_W_STR:
    case MP_W_BIN:
        return mp_r_bytes(r, (size_t)aux, &bytes);
    case MP_W_EXT:
        return mp_r_bytes(r, (size_t)aux + 1u, &bytes);
    case MP_W_ARRAY:
        for (uint64_t i = 0; i < aux; ++i) {
            status = mp_skip_value(r, depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    case MP_W_MAP:
        for (uint64_t i = 0; i < (aux * 2u); ++i) {
            status = mp_skip_value(r, depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    default:
        return SHREWD_BASIC_ERR_OK;
    }
}

/* --- encode -------------------------------------------------------------- */

static int mp_encode_value(mp_buf_t *b,
                           const shrewd_basic_reflect_type_t *type,
                           const unsigned char *value,
                           size_t nbytes,
                           size_t depth);

static int mp_refused(const shrewd_basic_reflect_field_t *field);
static int mp_check_fields(const shrewd_basic_reflect_field_t *fields, size_t count);

static int mp_refused(const shrewd_basic_reflect_field_t *field) {
    /* v1 gaps plus ops-hooked fields (their wire is the hook's own). */
    const shrewd_basic_reflect_ops_t *ops = reflect_field_ops(field);
    if ((NULL != ops) && ((NULL != ops->get) || (NULL != ops->set))) {
        return 1;
    }
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_UNION | SHREWD_BASIC_REFLECT_FIELD_F_BITS
               | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL
               | SHREWD_BASIC_REFLECT_FIELD_F_FLEX))) {
        return 1;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_BIT_INT == field->sb_refl_type->sb_refl_kind)
        || (SHREWD_BASIC_REFLECT_KIND_OPAQUE == field->sb_refl_type->sb_refl_kind)
        || (SHREWD_BASIC_REFLECT_KIND_UNION == field->sb_refl_type->sb_refl_kind)) {
        return 1;
    }
    return 0;
}

static int mp_check_fields(const shrewd_basic_reflect_field_t *fields, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (0 != mp_refused(&fields[i])) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Wide string content -> UTF-8 str on the wire (strict). */
static int mp_w_wstr(mp_buf_t *b, const void *text, size_t unit, size_t max_units) {
    size_t len = 0;
    int status = reflect_utf8_from_units(text, unit, max_units, NULL, 0, &len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = mp_w_str_head(b, len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = mp_buf_reserve(b, len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = reflect_utf8_from_units(
        text, unit, max_units, (char *)(void *)(b->data + b->len), b->cap - b->len, &len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    b->len += len;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_encode_fields(mp_buf_t *b,
                            const shrewd_basic_reflect_field_t *fields,
                            size_t count,
                            const unsigned char *object,
                            size_t depth) {
    int status = mp_w_head(b, 0x80u, 0xdeu, 0xdfu, count);
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < count); ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const unsigned char *at = object + field->sb_refl_offset;
        if (0 != mp_refused(field)) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
        }
        status = mp_w_str(b, field->sb_refl_name, strlen(field->sb_refl_name));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            size_t unit = field->sb_refl_stride;
            if (1 == unit) {
                status = mp_w_str(
                    b, (const char *)at, reflect_strn_len((const char *)at, field->sb_refl_count));
            } else {
                status = mp_w_wstr(b, at, unit, field->sb_refl_count);
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
            status = mp_w_head(b, 0x90u, 0xdcu, 0xddu, field->sb_refl_count);
            for (size_t j = 0; (SHREWD_BASIC_ERR_OK == status) && (j < field->sb_refl_count); ++j) {
                const unsigned char *elem = at + (j * field->sb_refl_stride);
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    const unsigned char *target = reflect_load_ptr(elem);
                    if (NULL == target) {
                        status = mp_w_u8(b, 0xc0u);
                    } else {
                        status = mp_encode_value(b,
                                                 field->sb_refl_type,
                                                 target,
                                                 field->sb_refl_type->sb_refl_size,
                                                 depth + 1);
                    }
                } else {
                    status = mp_encode_value(
                        b, field->sb_refl_type, elem, field->sb_refl_stride, depth + 1);
                }
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            const unsigned char *target = reflect_load_ptr(at);
            if (NULL == target) {
                status = mp_w_u8(b, 0xc0u);
            } else {
                status = mp_encode_value(
                    b, field->sb_refl_type, target, field->sb_refl_type->sb_refl_size, depth + 1);
            }
            continue;
        }
        status = mp_encode_value(b, field->sb_refl_type, at, field->sb_refl_stride, depth + 1);
    }
    return status;
}

static int mp_encode_value(mp_buf_t *b,
                           const shrewd_basic_reflect_type_t *type,
                           const unsigned char *value,
                           size_t nbytes,
                           size_t depth) {
    if (MP_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return mp_w_u8(b, (0 != reflect_load_u64(value, nbytes)) ? 0xc3u : 0xc2u);
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        memcpy(&c, value, 1);
        return mp_w_str(b, &c, 1);
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        return mp_w_int(b, reflect_load_i64(value, nbytes));
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        return mp_w_uint(b, reflect_load_u64(value, nbytes));
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = reflect_load_i64(value, nbytes);
        const char *name = NULL;
        if ((SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_enum_find_name(type, number, &name))
            && (NULL != name)) {
            return mp_w_str(b, name, strlen(name));
        }
        return mp_w_int(b, number);
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT: {
        float single = 0.0f;
        memcpy(&single, value, sizeof(single));
        return mp_w_f32(b, single);
    }
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double real = 0.0;
        memcpy(&real, value, sizeof(real));
        return mp_w_f64(b, real);
    }
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        long double wide = reflect_load_ld(value, nbytes);
        double real = (double)wide;
        if ((long double)real != wide) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* never narrow silently */
        }
        return mp_w_f64(b, real);
    }
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX: {
        size_t half = nbytes / 2;
        int status = mp_w_head(b, 0x90u, 0xdcu, 0xddu, 2);
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < 2); ++i) {
            long double part = reflect_load_cpart(value + (i * half), half);
            double real = (double)part;
            if ((long double)real != part) {
                return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
            }
            status = mp_w_f64(b, real);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING:
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32: {
        const void *text = reflect_load_ptr(value);
        size_t unit = reflect_string_unit(type->sb_refl_kind);
        if (NULL == text) {
            return mp_w_u8(b, 0xc0u);
        }
        if (1 == unit) {
            return mp_w_str(b, (const char *)text, strlen((const char *)text));
        }
        return mp_w_wstr(b, text, unit, SIZE_MAX);
    }
    case SHREWD_BASIC_REFLECT_KIND_BIT_INT:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        int status = mp_w_head(b, 0x90u, 0xdcu, 0xddu, type->sb_refl_elem_count);
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_elem_count); ++i) {
            status = mp_encode_value(b,
                                     type->sb_refl_elem,
                                     value + (i * type->sb_refl_elem_stride),
                                     type->sb_refl_elem_stride,
                                     depth + 1);
        }
        return status;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        return mp_encode_fields(
            b, type->sb_refl_fields, type->sb_refl_field_count, value, depth + 1);
    }
    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
}

int shrewd_basic_msgpack_encode(const void *object,
                                const shrewd_basic_reflect_type_t *type,
                                void **out_bytes,
                                size_t *out_size) {
    mp_buf_t b;
    int status = 0;
    if ((NULL == object) || (NULL == type) || (NULL == out_bytes) || (NULL == out_size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_bytes = NULL;
    *out_size = 0;
    b.data = NULL;
    b.len = 0;
    b.cap = 0;
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        status = mp_encode_fields(
            &b, type->sb_refl_fields, type->sb_refl_field_count, (const unsigned char *)object, 0);
    } else {
        status = mp_encode_value(&b, type, (const unsigned char *)object, type->sb_refl_size, 0);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        void *old = b.data;
        (void)shrewd_basic_platform_free(&old);
        return status;
    }
    *out_bytes = b.data;
    *out_size = b.len;
    return SHREWD_BASIC_ERR_OK;
}

/* --- decode -------------------------------------------------------------- */

static int mp_decode_value(
    mp_rd_t *r, const shrewd_basic_reflect_type_t *type, void *target, size_t nbytes, size_t depth);

/* Wire number -> signed/unsigned with the field's width limits. */
static int mp_wire_int(mp_wire_t kind, uint64_t aux, int64_t lo, int64_t hi, int64_t *out) {
    int64_t value = 0;
    if (MP_W_INT == kind) {
        value = (int64_t)aux;
    } else if (MP_W_UINT == kind) {
        if (aux > (uint64_t)INT64_MAX) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        value = (int64_t)aux;
    } else {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if ((value < lo) || (value > hi)) {
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_wire_uint(mp_wire_t kind, uint64_t aux, uint64_t hi, uint64_t *out) {
    if (MP_W_INT == kind) {
        int64_t value = (int64_t)aux;
        if (0 > value) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
    } else if (MP_W_UINT != kind) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if (aux > hi) {
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    *out = aux;
    return SHREWD_BASIC_ERR_OK;
}

static int mp_decode_fields(mp_rd_t *r,
                            const shrewd_basic_reflect_field_t *fields,
                            size_t count,
                            void *object,
                            size_t depth);

/* Pointer slot: nil zeroes it, anything else decodes into a fresh
   target block the slot then owns. */
static int mp_decode_pointer(mp_rd_t *r,
                             const shrewd_basic_reflect_type_t *type,
                             void *slot,
                             size_t depth) {
    mp_wire_t kind = MP_W_NIL;
    uint64_t aux = 0;
    size_t mark = r->pos;
    void *block = NULL;
    int status = mp_probe(r, &kind, &aux);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (MP_W_NIL == kind) {
        memset(slot, 0, sizeof(void *));
        return SHREWD_BASIC_ERR_OK;
    }
    r->pos = mark; /* re-read the value into the target block */
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, type->sb_refl_size)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    status = mp_decode_value(r, type, block, type->sb_refl_size, depth + 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        void *old = block;
        (void)shrewd_basic_platform_free(&old);
        return status;
    }
    memcpy(slot, &block, sizeof(block));
    return SHREWD_BASIC_ERR_OK;
}

/* One map entry's value into a supported field. */
static int mp_decode_field(mp_rd_t *r,
                           const shrewd_basic_reflect_field_t *field,
                           void *object,
                           size_t depth) {
    unsigned char *at = (unsigned char *)object + field->sb_refl_offset;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        mp_wire_t kind = MP_W_NIL;
        uint64_t aux = 0;
        const unsigned char *text = NULL;
        size_t unit = field->sb_refl_stride;
        int status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (MP_W_STR != kind) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = mp_r_bytes(r, (size_t)aux, &text);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (1 == unit) {
            if (aux > (uint64_t)field->sb_refl_count) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            memcpy(at, text, (size_t)aux);
            if (aux < (uint64_t)field->sb_refl_count) {
                at[aux] = '\0';
            }
            return SHREWD_BASIC_ERR_OK;
        }
        {
            size_t needed = 0;
            status
                = reflect_units_from_utf8((const char *)text, (size_t)aux, unit, NULL, 0, &needed);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (needed > field->sb_refl_count) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            status = reflect_units_from_utf8(
                (const char *)text, (size_t)aux, unit, at, field->sb_refl_count, &needed);
            if ((SHREWD_BASIC_ERR_OK == status) && (needed < field->sb_refl_count)) {
                memset(at + (needed * unit), 0, unit);
            }
            return status;
        }
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
        mp_wire_t kind = MP_W_NIL;
        uint64_t aux = 0;
        int status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (MP_W_ARRAY != kind) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        if (aux > (uint64_t)field->sb_refl_count) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        for (uint64_t i = 0; i < aux; ++i) {
            unsigned char *elem = at + ((size_t)i * field->sb_refl_stride);
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                status = mp_decode_pointer(r, field->sb_refl_type, elem, depth + 1);
            } else {
                status = mp_decode_value(
                    r, field->sb_refl_type, elem, field->sb_refl_stride, depth + 1);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        return mp_decode_pointer(r, field->sb_refl_type, at, depth + 1);
    }
    return mp_decode_value(r, field->sb_refl_type, at, field->sb_refl_stride, depth + 1);
}

static int mp_decode_fields(mp_rd_t *r,
                            const shrewd_basic_reflect_field_t *fields,
                            size_t count,
                            void *object,
                            size_t depth) {
    mp_wire_t kind = MP_W_NIL;
    uint64_t aux = 0;
    unsigned char *seen = NULL;
    int status = 0;
    if (MP_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    status = mp_probe(r, &kind, &aux);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (MP_W_MAP != kind) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    status = mp_check_fields(fields, count);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 < count) {
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, count)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        seen = block;
    }
    for (uint64_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < aux); ++i) {
        const unsigned char *key = NULL;
        uint64_t key_len = 0;
        size_t matched = count;
        status = mp_probe(r, &kind, &key_len);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        if (MP_W_STR != kind) {
            status = SHREWD_BASIC_ERR_BIN_FORMAT;
            break;
        }
        status = mp_r_bytes(r, (size_t)key_len, &key);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        for (size_t f = 0; f < count; ++f) {
            const char *name = fields[f].sb_refl_name;
            if ((strlen(name) == (size_t)key_len) && (0 == memcmp(name, key, (size_t)key_len))) {
                matched = f;
                break;
            }
        }
        if ((matched >= count) || (0 != seen[matched])) {
            /* Unknown key or duplicate: ignore the value (first wins). */
            status = mp_skip_value(r, depth + 1);
            continue;
        }
        seen[matched] = 1u;
        if (0 != mp_refused(&fields[matched])) {
            status = SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
            break;
        }
        status = mp_decode_field(r, &fields[matched], object, depth + 1);
    }
    {
        void *old = seen;
        (void)shrewd_basic_platform_free(&old);
    }
    return status;
}

static int mp_decode_value(mp_rd_t *r,
                           const shrewd_basic_reflect_type_t *type,
                           void *target,
                           size_t nbytes,
                           size_t depth) {
    mp_wire_t kind = MP_W_NIL;
    uint64_t aux = 0;
    const unsigned char *text = NULL;
    int status = 0;
    if (MP_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL: {
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (MP_W_BOOL != kind) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        reflect_store_u64(target, nbytes, aux);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (MP_W_STR == kind) {
            char c = '\0';
            if (1 != aux) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            status = mp_r_bytes(r, 1, &text);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            c = (char)text[0];
            memcpy(target, &c, 1);
            return SHREWD_BASIC_ERR_OK;
        }
        {
            int64_t number = 0;
            status = mp_wire_int(kind, aux, INT8_MIN, UINT8_MAX, &number);
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
        int64_t lo = INT64_MIN;
        int64_t hi = INT64_MAX;
        int64_t number = 0;
        if (SHREWD_BASIC_REFLECT_KIND_INT8 == type->sb_refl_kind) {
            lo = INT8_MIN;
            hi = INT8_MAX;
        } else if (SHREWD_BASIC_REFLECT_KIND_INT16 == type->sb_refl_kind) {
            lo = INT16_MIN;
            hi = INT16_MAX;
        } else if (SHREWD_BASIC_REFLECT_KIND_INT32 == type->sb_refl_kind) {
            lo = INT32_MIN;
            hi = INT32_MAX;
        }
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = mp_wire_int(kind, aux, lo, hi, &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(target, nbytes, (uint64_t)number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64: {
        uint64_t hi = UINT64_MAX;
        uint64_t number = 0;
        if (SHREWD_BASIC_REFLECT_KIND_UINT8 == type->sb_refl_kind) {
            hi = UINT8_MAX;
        } else if (SHREWD_BASIC_REFLECT_KIND_UINT16 == type->sb_refl_kind) {
            hi = UINT16_MAX;
        } else if (SHREWD_BASIC_REFLECT_KIND_UINT32 == type->sb_refl_kind) {
            hi = UINT32_MAX;
        }
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = mp_wire_uint(kind, aux, hi, &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(target, nbytes, number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (MP_W_STR == kind) {
            char name[128];
            int64_t number = 0;
            if (aux >= (uint64_t)sizeof(name)) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            status = mp_r_bytes(r, (size_t)aux, &text);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            memcpy(name, text, (size_t)aux);
            name[aux] = '\0';
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_enum_find_value(type, name, &number)) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            reflect_store_u64(target, nbytes, (uint64_t)number);
            return SHREWD_BASIC_ERR_OK;
        }
        {
            int64_t number = 0;
            status = mp_wire_int(kind, aux, INT64_MIN, INT64_MAX, &number);
            if (SHREWD_BASIC_ERR_OK == status) {
                reflect_store_u64(target, nbytes, (uint64_t)number);
            }
            return status;
        }
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        long double wide = 0.0L;
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if ((MP_W_F32 == kind) || (MP_W_F64 == kind)) {
            uint64_t bits = 0;
            size_t width = (MP_W_F32 == kind) ? 4u : 8u;
            status = mp_r_be(r, width, &bits);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (4 == width) {
                float single = 0.0f;
                uint32_t narrow = (uint32_t)bits;
                memcpy(&single, &narrow, sizeof(single));
                wide = (long double)single;
            } else {
                double real = 0.0;
                memcpy(&real, &bits, sizeof(real));
                wide = (long double)real;
            }
        } else if ((MP_W_INT == kind) || (MP_W_UINT == kind)) {
            wide = (MP_W_INT == kind) ? (long double)(int64_t)aux : (long double)aux;
        } else {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        if (SHREWD_BASIC_REFLECT_KIND_FLOAT == type->sb_refl_kind) {
            float single = (float)wide;
            if ((long double)single != wide) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            memcpy(target, &single, sizeof(single));
        } else if (SHREWD_BASIC_REFLECT_KIND_DOUBLE == type->sb_refl_kind) {
            double real = (double)wide;
            if ((long double)real != wide) {
                return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
            }
            memcpy(target, &real, sizeof(real));
        } else {
            memcpy(target, &wide, sizeof(wide));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX: {
        size_t half = nbytes / 2;
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if ((MP_W_ARRAY != kind) || (2 != aux)) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        for (size_t i = 0; i < 2; ++i) {
            long double part = 0.0L;
            status = mp_probe(r, &kind, &aux);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if ((MP_W_F32 == kind) || (MP_W_F64 == kind)) {
                uint64_t bits = 0;
                size_t width = (MP_W_F32 == kind) ? 4u : 8u;
                status = mp_r_be(r, width, &bits);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                if (4 == width) {
                    float single = 0.0f;
                    uint32_t narrow = (uint32_t)bits;
                    memcpy(&single, &narrow, sizeof(single));
                    part = (long double)single;
                } else {
                    double real = 0.0;
                    memcpy(&real, &bits, sizeof(real));
                    part = (long double)real;
                }
            } else if ((MP_W_INT == kind) || (MP_W_UINT == kind)) {
                part = (MP_W_INT == kind) ? (long double)(int64_t)aux : (long double)aux;
            } else {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            if (4 == half) {
                float single = (float)part;
                if ((long double)single != part) {
                    return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                }
                memcpy((unsigned char *)target + (i * half), &single, sizeof(single));
            } else if (8 == half) {
                double real = (double)part;
                if ((long double)real != part) {
                    return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
                }
                memcpy((unsigned char *)target + (i * half), &real, sizeof(real));
            } else {
                memcpy((unsigned char *)target + (i * half), &part, sizeof(part));
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING:
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
    case SHREWD_BASIC_REFLECT_KIND_STRING32: {
        size_t unit = reflect_string_unit(type->sb_refl_kind);
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (MP_W_NIL == kind) {
            memset(target, 0, sizeof(void *));
            return SHREWD_BASIC_ERR_OK;
        }
        if (MP_W_STR != kind) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        status = mp_r_bytes(r, (size_t)aux, &text);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (1 == unit) {
            void *block = NULL;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, (size_t)aux + 1u)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            memcpy(block, text, (size_t)aux);
            memcpy(target, &block, sizeof(block));
            return SHREWD_BASIC_ERR_OK;
        }
        {
            size_t needed = 0;
            void *block = NULL;
            status
                = reflect_units_from_utf8((const char *)text, (size_t)aux, unit, NULL, 0, &needed);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_calloc(&block, 1, (needed + 1u) * unit)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            status = reflect_units_from_utf8(
                (const char *)text, (size_t)aux, unit, block, needed + 1u, &needed);
            if (SHREWD_BASIC_ERR_OK != status) {
                void *old = block;
                (void)shrewd_basic_platform_free(&old);
                return status;
            }
            memcpy(target, &block, sizeof(block));
            return SHREWD_BASIC_ERR_OK;
        }
    }
    case SHREWD_BASIC_REFLECT_KIND_BIT_INT:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        status = mp_probe(r, &kind, &aux);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (MP_W_ARRAY != kind) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        if (aux > (uint64_t)type->sb_refl_elem_count) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        for (uint64_t i = 0; i < aux; ++i) {
            status
                = mp_decode_value(r,
                                  type->sb_refl_elem,
                                  (unsigned char *)target + ((size_t)i * type->sb_refl_elem_stride),
                                  type->sb_refl_elem_stride,
                                  depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        return mp_decode_fields(
            r, type->sb_refl_fields, type->sb_refl_field_count, target, depth + 1);
    }
    return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
}

int shrewd_basic_msgpack_decode(const void *bytes,
                                size_t size,
                                void *object,
                                const shrewd_basic_reflect_type_t *type) {
    mp_rd_t r;
    int status = 0;
    if ((NULL == bytes) || (NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    r.data = (const unsigned char *)bytes;
    r.len = size;
    r.pos = 0;
    memset(object, 0, type->sb_refl_size);
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        status = mp_decode_fields(&r, type->sb_refl_fields, type->sb_refl_field_count, object, 0);
    } else {
        status = mp_decode_value(&r, type, object, type->sb_refl_size, 0);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (r.pos != r.len) {
        return SHREWD_BASIC_ERR_BIN_FORMAT; /* trailing bytes */
    }
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_msgpack_bytes_free(void *bytes) {
    void *block = bytes;
    (void)shrewd_basic_platform_free(&block);
}

/* --- free (mirror of decode; the root object is not freed) ---------------- */

static void mp_release_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth);

static void mp_release_pointer(const shrewd_basic_reflect_type_t *type,
                               void *pointer,
                               size_t depth) {
    if (NULL == pointer) {
        return;
    }
    if (0 != reflect_kind_is_string(type->sb_refl_kind)) {
        void *text = NULL;
        memcpy(&text, pointer, sizeof(text));
        shrewd_basic_platform_free(&text);
    } else if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind)
               || (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind)) {
        mp_release_value(type, pointer, depth);
    }
    (void)shrewd_basic_platform_free(&pointer);
}

static void mp_release_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth) {
    if (MP_MAX_DEPTH <= depth) {
        return;
    }
    if (0 != reflect_kind_is_string(type->sb_refl_kind)) {
        void *text = NULL;
        memcpy(&text, value, sizeof(text));
        shrewd_basic_platform_free(&text);
        memset(value, 0, sizeof(void *));
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            mp_release_value(type->sb_refl_elem,
                             (unsigned char *)value + (i * type->sb_refl_elem_stride),
                             depth + 1);
        }
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        unsigned char *base = (unsigned char *)value;
        for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
            const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
            unsigned char *at = base + field->sb_refl_offset;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
                const shrewd_basic_reflect_variant_t *variant = reflect_union_variant(field, at);
                if (NULL != variant) {
                    for (size_t v = 0; v < variant->sb_refl_field_count; ++v) {
                        unsigned char *vat = at + variant->sb_refl_fields[v].sb_refl_offset;
                        mp_release_value(variant->sb_refl_fields[v].sb_refl_type, vat, depth + 1);
                    }
                }
                continue;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
                continue;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = NULL;
                memcpy(&pointer, at, sizeof(pointer));
                mp_release_pointer(field->sb_refl_type, pointer, depth + 1);
                memset(at, 0, sizeof(void *));
                continue;
            }
            if (0u
                != (field->sb_refl_flags
                    & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                       | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL
                       | SHREWD_BASIC_REFLECT_FIELD_F_FLEX))) {
                reflect_array_view_t view = reflect_array_view(base, field);
                for (size_t j = 0; j < view.count; ++j) {
                    unsigned char *elem = (unsigned char *)(uintptr_t)view.base + (j * view.stride);
                    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                        void *pointer = NULL;
                        memcpy(&pointer, elem, sizeof(pointer));
                        mp_release_pointer(field->sb_refl_type, pointer, depth + 1);
                        memset(elem, 0, sizeof(void *));
                    } else {
                        mp_release_value(field->sb_refl_type, elem, depth + 1);
                    }
                }
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                    void *table = NULL;
                    memcpy(&table, at, sizeof(table));
                    shrewd_basic_platform_free(&table);
                    memset(at, 0, sizeof(void *));
                }
                continue;
            }
            mp_release_value(field->sb_refl_type, at, depth + 1);
        }
        return;
    }
}

int shrewd_basic_msgpack_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        mp_release_value(type, object, 0);
    } else if (0 != reflect_kind_is_string(type->sb_refl_kind)) {
        void *text = NULL;
        memcpy(&text, object, sizeof(text));
        shrewd_basic_platform_free(&text);
        memset(object, 0, sizeof(void *));
    }
    return SHREWD_BASIC_ERR_OK;
}

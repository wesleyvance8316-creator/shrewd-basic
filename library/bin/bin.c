#include "shrewd-basic/formats/bin.h"

#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

SHREWD_BASIC_LOG_MODULE_DEFINE(shrewd_basic_bin_log, "bin", SHREWD_BASIC_LOG_LEVEL_ALL);

/*
 * Wire format is specified in shrewd-basic/formats/bin.h. Layout rules
 * enforced here: canonical widths per kind (member widths never leak
 * into the stream), optional natural-alignment padding before every
 * multi-byte token, and the ownership contract of the json mapping -
 * decode zeroes the target and everything it writes stays owned until
 * shrewd_basic_bin_free() releases it.
 */
#define BIN_MAGIC "SBIN"
#define BIN_FORMAT_VERSION 1
#define BIN_HEADER_SIZE 12
#define BIN_MAX_DEPTH 32

#define BIN_FLAG_BIG_ENDIAN ((unsigned)1u)
#define BIN_FLAG_NATURAL ((unsigned)2u)

typedef struct bin_writer {
    unsigned char *buf;
    size_t len;
    size_t cap;
    size_t body_start;
    int big_endian;
    int natural;
} bin_writer_t;

typedef struct bin_reader {
    const unsigned char *bytes;
    size_t length;
    size_t pos;
    size_t body_start;
    int big_endian;
    int natural;
} bin_reader_t;

typedef struct bin_ctx {
    bin_writer_t w;
    bin_reader_t r;
    const void *path[BIN_MAX_DEPTH]; /* pointer targets on the encode path */
    size_t depth_path;
} bin_ctx_t;

/* --- wire primitives -------------------------------------------------- */

static size_t bin_wire_width(const shrewd_basic_reflect_type_t *type) {
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
        return 1;
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
        return 2;
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
        return 4;
    default:
        return 8;
    }
}

static int bin_w_bytes(bin_writer_t *w, const void *data, size_t nbytes);

static int bin_w_pad(bin_writer_t *w, size_t width) {
    size_t offset = 0;
    size_t pad = 0;
    static const unsigned char zeros[8] = {0};
    if (0 == w->natural) {
        return SHREWD_BASIC_ERR_OK;
    }
    offset = w->len - w->body_start;
    pad = (1 < width) ? ((width - (offset % width)) % width) : 0;
    return bin_w_bytes(w, zeros, pad);
}

static int bin_r_pad(bin_reader_t *r, size_t width) {
    size_t offset = 0;
    size_t pad = 0;
    if (0 == r->natural) {
        return SHREWD_BASIC_ERR_OK;
    }
    offset = r->pos - r->body_start;
    pad = (1 < width) ? ((width - (offset % width)) % width) : 0;
    if (pad > (r->length - r->pos)) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    r->pos += pad;
    return SHREWD_BASIC_ERR_OK;
}

static int bin_w_bytes(bin_writer_t *w, const void *data, size_t nbytes) {
    if (nbytes > (SIZE_MAX - w->len)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if ((w->len + nbytes) > w->cap) {
        size_t cap = (0 == w->cap) ? 256 : w->cap;
        void *block = NULL;
        while (cap < (w->len + nbytes)) {
            if (cap > (SIZE_MAX / 2)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            cap *= 2;
        }
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, cap)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        if (NULL != w->buf) {
            memcpy(block, w->buf, w->len);
        }
        {
            void *old = w->buf;
            (void)shrewd_basic_platform_free(&old);
        }
        w->buf = block;
        w->cap = cap;
    }
    memcpy(w->buf + w->len, data, nbytes);
    w->len += nbytes;
    return SHREWD_BASIC_ERR_OK;
}

static int bin_w_u8(bin_writer_t *w, uint8_t byte) {
    return bin_w_bytes(w, &byte, 1);
}

static int bin_w_word(bin_writer_t *w, uint64_t word, size_t width) {
    unsigned char bytes[8];
    int status = bin_w_pad(w, width);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (size_t i = 0; i < width; ++i) {
        size_t shift = (0 != w->big_endian) ? (8 * (width - 1 - i)) : (8 * i);
        bytes[i] = (unsigned char)((word >> shift) & UINT64_C(0xff));
    }
    return bin_w_bytes(w, bytes, width);
}

static int bin_r_bytes(bin_reader_t *r, void *out, size_t nbytes) {
    if (nbytes > (r->length - r->pos)) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if (NULL != out) {
        memcpy(out, r->bytes + r->pos, nbytes);
    }
    r->pos += nbytes;
    return SHREWD_BASIC_ERR_OK;
}

static int bin_r_word(bin_reader_t *r, size_t width, uint64_t *out) {
    uint64_t word = 0;
    int status = bin_r_pad(r, width);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (width > (r->length - r->pos)) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    for (size_t i = 0; i < width; ++i) {
        size_t shift = (0 != r->big_endian) ? (8 * (width - 1 - i)) : (8 * i);
        word |= ((uint64_t)r->bytes[r->pos + i]) << shift;
    }
    r->pos += width;
    if (NULL != out) {
        *out = word;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* --- encode ----------------------------------------------------------- */

static int bin_encode_value(bin_ctx_t *ctx,
                            const shrewd_basic_reflect_type_t *type,
                            const unsigned char *value,
                            size_t nbytes,
                            const shrewd_basic_reflect_ops_t *ops,
                            size_t depth);

static int bin_encode_edge(bin_ctx_t *ctx,
                           const shrewd_basic_reflect_type_t *type,
                           const void *target,
                           const shrewd_basic_reflect_ops_t *ops,
                           size_t depth) {
    int status = bin_w_u8(&ctx->w, (NULL != target) ? 1u : 0u);
    if ((SHREWD_BASIC_ERR_OK != status) || (NULL == target)) {
        return status;
    }
    for (size_t k = 0; k < ctx->depth_path; ++k) {
        if (ctx->path[k] == target) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED; /* cycle */
        }
    }
    if (BIN_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    ctx->path[ctx->depth_path] = target;
    ctx->depth_path += 1;
    status = bin_encode_value(
        ctx, type, (const unsigned char *)target, type->sb_refl_size, ops, depth + 1);
    ctx->depth_path -= 1;
    return status;
}

static int bin_encode_fields(bin_ctx_t *ctx,
                             const shrewd_basic_reflect_field_t *fields,
                             size_t count,
                             const unsigned char *object,
                             size_t depth) {
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        reflect_array_view_t view = reflect_array_view(object, field);
        int status = 0;
        if ((NULL != field_ops) && ((NULL != field_ops->get) || (NULL != field_ops->set))) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            const unsigned char *storage = object + field->sb_refl_offset;
            int64_t tag
                = reflect_load_i64(object + field->sb_refl_limit_offset, field->sb_refl_limit_size);
            const shrewd_basic_reflect_variant_t *variant = NULL;
            for (size_t v = 0; v < field->sb_refl_type->sb_refl_variant_count; ++v) {
                if (tag == field->sb_refl_type->sb_refl_variants[v].sb_refl_tag) {
                    variant = &field->sb_refl_type->sb_refl_variants[v];
                }
            }
            status = bin_w_word(&ctx->w, (uint64_t)tag, 8);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (NULL == variant) {
                return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
            }
            status = bin_encode_fields(
                ctx, variant->sb_refl_fields, variant->sb_refl_field_count, storage, depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
            uint64_t unit = reflect_load_u64(object + field->sb_refl_offset, field->sb_refl_size);
            uint64_t bits
                = reflect_bits_extract(unit, field->sb_refl_bit_offset, field->sb_refl_bit_width);
            status = bin_w_word(&ctx->w, bits, bin_wire_width(field->sb_refl_type));
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            const char *text = (const char *)(object + field->sb_refl_offset);
            size_t length = reflect_strn_len(text, field->sb_refl_count);
            status = bin_w_word(&ctx->w, (uint64_t)length, 4);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            status = bin_w_bytes(&ctx->w, text, length);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0u
            != (field->sb_refl_flags
                & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                   | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL | SHREWD_BASIC_REFLECT_FIELD_F_FLEX))) {
            size_t elems = view.count;
            int counted = (0u
                           != (field->sb_refl_flags
                               & (SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                                  | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL
                                  | SHREWD_BASIC_REFLECT_FIELD_F_FLEX)));
            if (0 != counted) {
                status = bin_w_word(&ctx->w, (uint64_t)elems, 4);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            for (size_t j = 0; j < elems; ++j) {
                const unsigned char *elem = view.base + (j * view.stride);
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    status = bin_encode_edge(
                        ctx, field->sb_refl_type, reflect_load_ptr(elem), field_ops, depth);
                } else {
                    status = bin_encode_value(
                        ctx, field->sb_refl_type, elem, view.stride, field_ops, depth + 1);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            status = bin_encode_edge(ctx,
                                     field->sb_refl_type,
                                     reflect_load_ptr(object + field->sb_refl_offset),
                                     field_ops,
                                     depth);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        status = bin_encode_value(ctx,
                                  field->sb_refl_type,
                                  object + field->sb_refl_offset,
                                  field->sb_refl_stride,
                                  field_ops,
                                  depth);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static int bin_encode_value(bin_ctx_t *ctx,
                            const shrewd_basic_reflect_type_t *type,
                            const unsigned char *value,
                            size_t nbytes,
                            const shrewd_basic_reflect_ops_t *ops,
                            size_t depth) {
    int status = 0;
    if (BIN_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    if ((NULL != ops) && ((NULL != ops->get) || (NULL != ops->set))) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return bin_w_u8(&ctx->w, (0 != reflect_load_u64(value, nbytes)) ? 1u : 0u);
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
        return bin_w_u8(&ctx->w, *(const unsigned char *)value);
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        return bin_w_word(&ctx->w, (uint64_t)reflect_load_i64(value, nbytes), bin_wire_width(type));
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        return bin_w_word(&ctx->w, reflect_load_u64(value, nbytes), bin_wire_width(type));
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double number = reflect_load_f64(value, nbytes);
        if (4 == bin_wire_width(type)) {
            float single = (float)number;
            uint32_t bits = 0;
            memcpy(&bits, &single, sizeof(bits));
            return bin_w_word(&ctx->w, (uint64_t)bits, 4);
        } else {
            uint64_t bits = 0;
            memcpy(&bits, &number, sizeof(bits));
            return bin_w_word(&ctx->w, bits, 8);
        }
    }
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX:
    case SHREWD_BASIC_REFLECT_KIND_BIT_INT:
        /* Raw-width wire in native order: x87/complex/bit-int shapes
           have no portable byte order. */
        return bin_w_bytes(&ctx->w, value, nbytes);
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = reflect_load_str(value);
        if (NULL == text) {
            return bin_w_u8(&ctx->w, 0);
        }
        status = bin_w_u8(&ctx->w, 1);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = bin_w_word(&ctx->w, (uint64_t)strlen(text), 4);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        return bin_w_bytes(&ctx->w, text, strlen(text));
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            status = bin_encode_value(ctx,
                                      type->sb_refl_elem,
                                      value + (i * type->sb_refl_elem_stride),
                                      type->sb_refl_elem_stride,
                                      type->sb_refl_elem->sb_refl_ops,
                                      depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    return bin_encode_fields(ctx, type->sb_refl_fields, type->sb_refl_field_count, value, depth);
}

/* --- decode (out may be NULL: parse-only) ------------------------------ */

static int bin_decode_value(bin_ctx_t *ctx,
                            const shrewd_basic_reflect_type_t *type,
                            unsigned char *out,
                            size_t nbytes,
                            const shrewd_basic_reflect_ops_t *ops,
                            size_t depth,
                            size_t *flex);

static void bin_free_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth);

/*
 * Allocate and decode a pointer target: peek the stream parse-only to
 * learn the block size (flexible tails included), restore, allocate,
 * decode for real. A failed real decode releases the partial content
 * before the block, keeping bin_free() complete for whatever the
 * caller keeps.
 */
static int bin_alloc_target(bin_ctx_t *ctx,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_reflect_ops_t *ops,
                            size_t depth,
                            void **out) {
    size_t save_pos = ctx->r.pos;
    size_t flex = 0;
    size_t need = 0;
    void *target = NULL;
    int status = bin_decode_value(ctx, type, NULL, type->sb_refl_size, ops, depth, &flex);
    need = type->sb_refl_size + flex;
    ctx->r.pos = save_pos;
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&target, 1, need)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    status = bin_decode_value(
        ctx, type, (unsigned char *)target, type->sb_refl_size, ops, depth, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        bin_free_value(type, target, depth);
        {
            void *old = target;
            (void)shrewd_basic_platform_free(&old);
        }
        return status;
    }
    *out = target;
    return SHREWD_BASIC_ERR_OK;
}

static int bin_decode_fields(bin_ctx_t *ctx,
                             const shrewd_basic_reflect_field_t *fields,
                             size_t count,
                             unsigned char *object,
                             size_t depth,
                             size_t *flex) {
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        unsigned char *at = (NULL != object) ? (object + field->sb_refl_offset) : NULL;
        int status = 0;
        if ((NULL != field_ops) && ((NULL != field_ops->get) || (NULL != field_ops->set))) {
            return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            uint64_t tag = 0;
            const shrewd_basic_reflect_variant_t *variant = NULL;
            status = bin_r_word(&ctx->r, 8, &tag);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            for (size_t v = 0; v < field->sb_refl_type->sb_refl_variant_count; ++v) {
                if ((int64_t)tag == field->sb_refl_type->sb_refl_variants[v].sb_refl_tag) {
                    variant = &field->sb_refl_type->sb_refl_variants[v];
                }
            }
            if (NULL == variant) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            if (NULL != object) {
                reflect_store_u64(
                    object + field->sb_refl_limit_offset, field->sb_refl_limit_size, tag);
            }
            status = bin_decode_fields(
                ctx, variant->sb_refl_fields, variant->sb_refl_field_count, at, depth + 1, NULL);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
            uint64_t bits = 0;
            status = bin_r_word(&ctx->r, bin_wire_width(field->sb_refl_type), &bits);
            if ((SHREWD_BASIC_ERR_OK == status) && (NULL != at)) {
                uint64_t unit = reflect_load_u64(at, field->sb_refl_size);
                reflect_bits_insert(
                    &unit, field->sb_refl_bit_offset, field->sb_refl_bit_width, bits);
                reflect_store_u64(at, field->sb_refl_size, unit);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            uint64_t length = 0;
            status = bin_r_word(&ctx->r, 4, &length);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (length > field->sb_refl_count) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            status = bin_r_bytes(&ctx->r, at, (size_t)length);
            if ((SHREWD_BASIC_ERR_OK == status) && (NULL != at)
                && (length < field->sb_refl_count)) {
                at[length] = '\0';
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0u
            != (field->sb_refl_flags
                & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                   | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL | SHREWD_BASIC_REFLECT_FIELD_F_FLEX))) {
            unsigned char *base = at;
            unsigned char *table = NULL;
            size_t elems = field->sb_refl_count;
            int counted = (0u
                           != (field->sb_refl_flags
                               & (SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                                  | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL
                                  | SHREWD_BASIC_REFLECT_FIELD_F_FLEX)));
            int dynamic = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY));
            if (0 != counted) {
                uint64_t wire_count = 0;
                status = bin_r_word(&ctx->r, 4, &wire_count);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                elems = (size_t)wire_count;
            }
            if ((0 == dynamic) && (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX))
                && (elems > field->sb_refl_count)) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            if ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL))
                && (0 == dynamic) && (elems >= field->sb_refl_count)) {
                return SHREWD_BASIC_ERR_BIN_FORMAT;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
                if (NULL != flex) {
                    *flex += elems * field->sb_refl_stride;
                }
            }
            if ((0 != dynamic) && (NULL != at)) {
                size_t slots
                    = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL))
                          ? (elems + 1)
                          : elems;
                if (0 != slots) {
                    if (SHREWD_BASIC_ERR_OK
                        != shrewd_basic_platform_calloc(
                            (void **)&table, 1, slots * field->sb_refl_stride)) {
                        return SHREWD_BASIC_ERR_MEM_NOMEM;
                    }
                }
                memcpy(at, &table, sizeof(table));
                base = table;
                if (0 != field->sb_refl_limit_size) {
                    uint64_t bound = (uint64_t)elems;
                    memcpy(object + field->sb_refl_limit_offset, &bound, field->sb_refl_limit_size);
                }
            } else if (0 != counted && (NULL != at) && (0 != field->sb_refl_limit_size)) {
                uint64_t bound = (uint64_t)elems;
                memcpy(object + field->sb_refl_limit_offset, &bound, field->sb_refl_limit_size);
            }
            for (size_t j = 0; j < elems; ++j) {
                unsigned char *slot = (NULL != base) ? (base + (j * field->sb_refl_stride)) : NULL;
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    uint64_t present = 0;
                    void *target = NULL;
                    status = bin_r_word(&ctx->r, 1, &present);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        return status;
                    }
                    if (0 == present) {
                        continue;
                    }
                    if (NULL != slot) {
                        status = bin_alloc_target(
                            ctx, field->sb_refl_type, field_ops, depth + 1, &target);
                        if (SHREWD_BASIC_ERR_OK == status) {
                            memcpy(slot, &target, sizeof(target));
                        }
                    } else {
                        status = bin_decode_value(ctx,
                                                  field->sb_refl_type,
                                                  NULL,
                                                  field->sb_refl_type->sb_refl_size,
                                                  field_ops,
                                                  depth + 1,
                                                  NULL);
                    }
                } else {
                    status = bin_decode_value(ctx,
                                              field->sb_refl_type,
                                              slot,
                                              field->sb_refl_stride,
                                              field_ops,
                                              depth + 1,
                                              NULL);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            uint64_t present = 0;
            status = bin_r_word(&ctx->r, 1, &present);
            if ((SHREWD_BASIC_ERR_OK != status) || (0 == present)) {
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                continue;
            }
            if (NULL != at) {
                void *target = NULL;
                status = bin_alloc_target(ctx, field->sb_refl_type, field_ops, depth + 1, &target);
                if (SHREWD_BASIC_ERR_OK == status) {
                    memcpy(at, &target, sizeof(target));
                }
            } else {
                status = bin_decode_value(ctx,
                                          field->sb_refl_type,
                                          NULL,
                                          field->sb_refl_type->sb_refl_size,
                                          field_ops,
                                          depth + 1,
                                          NULL);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        status = bin_decode_value(
            ctx, field->sb_refl_type, at, field->sb_refl_stride, field_ops, depth, flex);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static int bin_decode_value(bin_ctx_t *ctx,
                            const shrewd_basic_reflect_type_t *type,
                            unsigned char *out,
                            size_t nbytes,
                            const shrewd_basic_reflect_ops_t *ops,
                            size_t depth,
                            size_t *flex) {
    int status = 0;
    if (BIN_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if ((NULL != ops) && ((NULL != ops->get) || (NULL != ops->set))) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL: {
        uint64_t flag = 0;
        status = bin_r_word(&ctx->r, 1, &flag);
        if ((SHREWD_BASIC_ERR_OK == status) && (NULL != out)) {
            reflect_store_u64(out, nbytes, (0 != flag) ? 1u : 0u);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        unsigned char byte = 0;
        status = bin_r_bytes(&ctx->r, &byte, 1);
        if ((SHREWD_BASIC_ERR_OK == status) && (NULL != out)) {
            *out = byte;
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        uint64_t word = 0;
        status = bin_r_word(&ctx->r, bin_wire_width(type), &word);
        if ((SHREWD_BASIC_ERR_OK == status) && (NULL != out)) {
            reflect_store_u64(out, nbytes, word);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        uint64_t word = 0;
        size_t width = bin_wire_width(type);
        status = bin_r_word(&ctx->r, width, &word);
        if ((SHREWD_BASIC_ERR_OK == status) && (NULL != out)) {
            double number = 0.0;
            if (4 == width) {
                float single = 0.0f;
                uint32_t bits = (uint32_t)word;
                memcpy(&single, &bits, sizeof(single));
                number = (double)single;
            } else {
                memcpy(&number, &word, sizeof(number));
            }
            if (4 == nbytes) {
                float single = (float)number;
                memcpy(out, &single, sizeof(single));
            } else {
                memcpy(out, &number, sizeof(number));
            }
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX:
    case SHREWD_BASIC_REFLECT_KIND_BIT_INT:
        /* Raw-width wire in native order (mirror of the encoder). */
        return bin_r_bytes(&ctx->r, out, nbytes);
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        uint64_t present = 0;
        uint64_t length = 0;
        void *block = NULL;
        status = bin_r_word(&ctx->r, 1, &present);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (0 == present) {
            if (NULL != out) {
                memset(out, 0, sizeof(void *));
            }
            return SHREWD_BASIC_ERR_OK;
        }
        status = bin_r_word(&ctx->r, 4, &length);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (length >= SIZE_MAX) {
            return SHREWD_BASIC_ERR_BIN_FORMAT;
        }
        if (NULL != out) {
            if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_calloc(&block, 1, (size_t)length + 1)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
        }
        status = bin_r_bytes(&ctx->r, block, (size_t)length);
        if (SHREWD_BASIC_ERR_OK != status) {
            void *old = block;
            (void)shrewd_basic_platform_free(&old);
            return status;
        }
        if (NULL != out) {
            memcpy(out, &block, sizeof(block));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            status
                = bin_decode_value(ctx,
                                   type->sb_refl_elem,
                                   (NULL != out) ? (out + (i * type->sb_refl_elem_stride)) : NULL,
                                   type->sb_refl_elem_stride,
                                   type->sb_refl_elem->sb_refl_ops,
                                   depth + 1,
                                   NULL);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    return bin_decode_fields(
        ctx, type->sb_refl_fields, type->sb_refl_field_count, out, depth, flex);
}

/* --- free ------------------------------------------------------------- */

static void bin_free_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth);

static void bin_free_fields(const shrewd_basic_reflect_field_t *fields,
                            size_t count,
                            void *object,
                            size_t depth) {
    if (BIN_MAX_DEPTH <= depth) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        unsigned char *at = (unsigned char *)object + field->sb_refl_offset;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            int64_t tag = reflect_load_i64((unsigned char *)object + field->sb_refl_limit_offset,
                                           field->sb_refl_limit_size);
            for (size_t v = 0; v < field->sb_refl_type->sb_refl_variant_count; ++v) {
                if (tag == field->sb_refl_type->sb_refl_variants[v].sb_refl_tag) {
                    bin_free_fields(field->sb_refl_type->sb_refl_variants[v].sb_refl_fields,
                                    field->sb_refl_type->sb_refl_variants[v].sb_refl_field_count,
                                    at,
                                    depth + 1);
                }
            }
            continue;
        }
        if (0u
            != (field->sb_refl_flags
                & (SHREWD_BASIC_REFLECT_FIELD_F_BITS | SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))) {
            continue;
        }
        if (0u
            != (field->sb_refl_flags
                & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                   | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL | SHREWD_BASIC_REFLECT_FIELD_F_FLEX))) {
            reflect_array_view_t view = reflect_array_view(object, field);
            unsigned char *table = NULL;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                table = (unsigned char *)(uintptr_t)view.base;
                if (NULL == table) {
                    continue;
                }
            }
            for (size_t j = 0; j < view.count; ++j) {
                unsigned char *slot
                    = (NULL != table) ? (table + (j * view.stride)) : (at + (j * view.stride));
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    void *pointer = NULL;
                    memcpy(&pointer, slot, sizeof(pointer));
                    if (NULL != pointer) {
                        bin_free_value(field->sb_refl_type, pointer, depth + 1);
                        {
                            void *old = pointer;
                            (void)shrewd_basic_platform_free(&old);
                        }
                        memset(slot, 0, sizeof(void *));
                    }
                } else {
                    bin_free_value(field->sb_refl_type, slot, depth + 1);
                }
            }
            if (NULL != table) {
                void *old = table;
                (void)shrewd_basic_platform_free(&old);
                memset(at, 0, sizeof(void *));
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            void *pointer = NULL;
            memcpy(&pointer, at, sizeof(pointer));
            if (NULL != pointer) {
                bin_free_value(field->sb_refl_type, pointer, depth + 1);
                {
                    void *old = pointer;
                    (void)shrewd_basic_platform_free(&old);
                }
                memset(at, 0, sizeof(void *));
            }
            continue;
        }
        bin_free_value(field->sb_refl_type, at, depth);
    }
}

static void bin_free_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth) {
    if (BIN_MAX_DEPTH <= depth) {
        return;
    }
    if (0 != reflect_kind_is_string(type->sb_refl_kind)) {
        void *text = NULL;
        memcpy(&text, value, sizeof(text));
        {
            void *old = text;
            (void)shrewd_basic_platform_free(&old);
        }
        memset(value, 0, sizeof(void *));
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            bin_free_value(type->sb_refl_elem,
                           (unsigned char *)value + (i * type->sb_refl_elem_stride),
                           depth + 1);
        }
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        bin_free_fields(type->sb_refl_fields, type->sb_refl_field_count, value, depth);
    }
}

/* --- public API ------------------------------------------------------- */

static void bin_setup(bin_ctx_t *ctx, const shrewd_basic_bin_options_t *options, int big_endian) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->w.big_endian = big_endian;
    ctx->r.big_endian = big_endian;
    if (NULL != options) {
        ctx->w.natural = options->natural_align;
        ctx->r.natural = options->natural_align;
    }
}

static int bin_resolved_endian(const shrewd_basic_bin_options_t *options) {
    if ((NULL == options) || (SHREWD_BASIC_BIN_ENDIAN_LITTLE == options->endian)) {
        return 0;
    }
    if (SHREWD_BASIC_BIN_ENDIAN_BIG == options->endian) {
        return 1;
    }
    {
        const uint16_t probe = 1;
        return (1 != *(const unsigned char *)&probe);
    }
}

int shrewd_basic_bin_encode(const void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_bin_options_t *options,
                            void **out_bytes,
                            size_t *out_size) {
    bin_ctx_t ctx;
    unsigned char header[BIN_HEADER_SIZE];
    uint32_t user_version = 0;
    unsigned flags = 0;
    int status = 0;
    if ((NULL == object) || (NULL == type) || (NULL == out_bytes) || (NULL == out_size)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_bin_log,
                         "encode",
                         "encode: object, type or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_bytes = NULL;
    *out_size = 0;
    bin_setup(&ctx, options, bin_resolved_endian(options));
    if (0 != ctx.w.big_endian) {
        flags |= BIN_FLAG_BIG_ENDIAN;
    }
    if (0 != ctx.w.natural) {
        flags |= BIN_FLAG_NATURAL;
    }
    if (NULL != options) {
        user_version = options->version;
    }
    memcpy(header, BIN_MAGIC, 4);
    header[4] = BIN_FORMAT_VERSION;
    header[5] = (unsigned char)flags;
    header[6] = 0;
    header[7] = 0;
    memcpy(header + 8, &user_version, sizeof(user_version)); /* LE stamp */
    ctx.w.body_start = BIN_HEADER_SIZE;
    status = bin_w_bytes(&ctx.w, header, sizeof(header));
    if (SHREWD_BASIC_ERR_OK == status) {
        status = bin_encode_value(
            &ctx, type, (const unsigned char *)object, type->sb_refl_size, type->sb_refl_ops, 0);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        void *old = ctx.w.buf;
        (void)shrewd_basic_platform_free(&old);
        return status;
    }
    *out_bytes = ctx.w.buf;
    *out_size = ctx.w.len;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_bin_log,
                     "encode",
                     "encode ok (%s, %zu bytes)\n",
                     type->sb_refl_name,
                     ctx.w.len);
    return SHREWD_BASIC_ERR_OK;
}

static int bin_parse_header(bin_ctx_t *ctx,
                            const void *bytes,
                            size_t length,
                            const shrewd_basic_bin_options_t *options) {
    const unsigned char *data = (const unsigned char *)bytes;
    unsigned flags = 0;
    uint32_t user_version = 0;
    if (length < BIN_HEADER_SIZE) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if (0 != memcmp(data, BIN_MAGIC, 4)) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    if (BIN_FORMAT_VERSION != data[4]) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    flags = data[5];
    ctx->r.big_endian = (0 != (flags & BIN_FLAG_BIG_ENDIAN)) ? 1 : 0;
    ctx->r.natural = (0 != (flags & BIN_FLAG_NATURAL)) ? 1 : 0;
    memcpy(&user_version, data + 8, sizeof(user_version));
    if ((NULL != options) && (0 != options->version) && (user_version != options->version)) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    ctx->r.bytes = data;
    ctx->r.length = length;
    ctx->r.pos = BIN_HEADER_SIZE;
    ctx->r.body_start = BIN_HEADER_SIZE;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_bin_decode(const void *bytes,
                            size_t length,
                            void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_bin_options_t *options) {
    bin_ctx_t ctx;
    int status = 0;
    if ((NULL == bytes) || (NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_bin_log,
                         "decode",
                         "decode: bytes, object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&ctx, 0, sizeof(ctx));
    status = bin_parse_header(&ctx, bytes, length, options);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memset(object, 0, type->sb_refl_size);
    status = bin_decode_value(
        &ctx, type, (unsigned char *)object, type->sb_refl_size, type->sb_refl_ops, 0, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_bin_log,
                     "decode",
                     "decode ok (%s)\n",
                     type->sb_refl_name);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_bin_value_size(const void *bytes,
                                size_t length,
                                const shrewd_basic_reflect_type_t *type,
                                size_t *out_size) {
    bin_ctx_t ctx;
    size_t flex = 0;
    int status = 0;
    if ((NULL == bytes) || (NULL == type) || (NULL == out_size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&ctx, 0, sizeof(ctx));
    status = bin_parse_header(&ctx, bytes, length, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = bin_decode_value(&ctx, type, NULL, type->sb_refl_size, type->sb_refl_ops, 0, &flex);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    *out_size = type->sb_refl_size + flex;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_bin_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_bin_log,
                         "free",
                         "free: object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    bin_free_value(type, object, 0);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_bin_log,
                     "free",
                     "free ok (%s)\n",
                     type->sb_refl_name);
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_bin_bytes_free(void *bytes) {
    void *block = bytes;
    (void)shrewd_basic_platform_free(&block);
}

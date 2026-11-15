#include "shrewd-basic/reflect/reflect.h"

#include <stdint.h>
#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared reflect log module, defined in library/reflect/reflect.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

/*
 * Hashing mirrors shrewd_basic_reflect_equal(): equal objects must hash
 * equal. Leaves fold through canonical encodings (integers by value,
 * floats with -0.0 folded and NaNs as a fixed marker, strings by
 * content, opaque targets by address) into a 64-bit FNV-1a stream that
 * is endianness- and width-independent. The depth cap matches equal's -
 * a capped region folds as a marker on both sides - and cyclic targets
 * fold as a marker too, so bisimilar but differently shaped cycles may
 * hash differently (documented caveat).
 */
#define HASH_MAX_DEPTH 16 /* mirrors EQUAL_MAX_DEPTH in reflect_equal.c */
#define HASH_OFFSET UINT64_C(0xcbf29ce484222325)
#define HASH_PRIME UINT64_C(0x100000001b3)

typedef struct hash_frame {
    const void *object;
    const shrewd_basic_reflect_type_t *type;
} hash_frame_t;

static uint64_t hash_bytes(uint64_t h, const void *data, size_t nbytes)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < nbytes; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= HASH_PRIME;
    }
    return h;
}

/* Fold a 64-bit word in little-endian byte order (stable everywhere). */
static uint64_t hash_u64(uint64_t h, uint64_t word)
{
    unsigned char bytes[8];
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)((word >> (8 * i)) & UINT64_C(0xff));
    }
    return hash_bytes(h, bytes, sizeof(bytes));
}

static int hash_target_seen(const hash_frame_t *stack, size_t depth,
                            const void *target,
                            const shrewd_basic_reflect_type_t *type)
{
    for (size_t i = 0; i < depth; ++i) {
        if ((stack[i].object == target) && (stack[i].type == type)) {
            return 1;
        }
    }
    return 0;
}

static int hash_value(const shrewd_basic_reflect_type_t *type,
                      const unsigned char *value, size_t nbytes,
                      const shrewd_basic_reflect_ops_t *ops,
                      hash_frame_t *stack, size_t depth, uint64_t *h)
{
    if (HASH_MAX_DEPTH <= depth) {
        /* Same cutoff as reflect_equal(): past the cap nothing is read. */
        *h = hash_u64(*h, UINT64_C(0x4841534843415000)); /* "HASHCAP\0" */
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->hash)) {
        size_t custom = 0;
        int status = ops->hash(value, &custom);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        *h = hash_u64(*h, (uint64_t)custom);
        return SHREWD_BASIC_ERR_OK;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        *h = hash_u64(*h, (0 != reflect_load_u64(value, nbytes)) ? 1u : 0u);
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        *h = hash_u64(*h, (uint64_t)reflect_load_i64(value, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        *h = hash_u64(*h, reflect_load_u64(value, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
        /* equal() compares char fields byte-wise. */
        *h = hash_bytes(*h, value, 1);
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double number = reflect_load_f64(value, nbytes);
        uint64_t bits = 0;
        if (number != number) {
            bits = UINT64_C(0x7ff8000000000000); /* NaN marker */
        } else {
            if (0.0 == number) {
                number = 0.0; /* fold -0.0 into +0.0 (they compare equal) */
            }
            memcpy(&bits, &number, sizeof(bits));
        }
        *h = hash_u64(*h, bits);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = reflect_load_str(value);
        if (NULL == text) {
            *h = hash_u64(*h, 0);
        } else {
            *h = hash_u64(*h, 1);
            *h = hash_bytes(*h, text, strlen(text));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        /* Without ops equal() is address identity. */
        *h = hash_u64(*h, (uint64_t)(uintptr_t)value);
        return SHREWD_BASIC_ERR_OK;
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        *h = hash_bytes(*h, value, nbytes);
        return SHREWD_BASIC_ERR_OK;
    }
    if (0 != hash_target_seen(stack, depth, value, type)) {
        /* Back-edge: folded as a marker instead of expanded. */
        *h = hash_u64(*h, UINT64_C(0x4841534843594300)); /* "HASHCYC\0" */
        return SHREWD_BASIC_ERR_OK;
    }
    stack[depth].object = value;
    stack[depth].type = type;
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        reflect_array_view_t view = reflect_array_view(value, field);
        *h = hash_u64(*h, (uint64_t)view.count);
        for (size_t j = 0; j < view.count; ++j) {
            const unsigned char *elem = view.base + (j * view.stride);
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = reflect_load_ptr(elem);
                if (NULL == pointer) {
                    *h = hash_u64(*h, 0);
                    continue;
                }
                *h = hash_u64(*h, 1);
                int status = hash_value(field->sb_refl_type,
                                        (const unsigned char *)pointer,
                                        field->sb_refl_type->sb_refl_size,
                                        field_ops, stack, depth + 1, h);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            } else {
                int status = hash_value(field->sb_refl_type, elem, view.stride,
                                        field_ops, stack, depth + 1, h);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_hash(const void *object,
                              const shrewd_basic_reflect_type_t *type,
                              size_t *out_hash)
{
    if ((NULL == object) || (NULL == type) || (NULL == out_hash)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "hash", "hash: object, type or out_hash is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    hash_frame_t stack[HASH_MAX_DEPTH];
    uint64_t h = HASH_OFFSET;
    int status = hash_value(type, (const unsigned char *)object, type->sb_refl_size,
                            type->sb_refl_ops, stack, 0, &h);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
#if SIZE_MAX < UINT64_MAX
    *out_hash = (size_t)(h ^ (h >> 32));
#else
    *out_hash = (size_t)h;
#endif
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "hash", "hash ok (%s, 0x%zx)\n", type->sb_refl_name, *out_hash);
    return SHREWD_BASIC_ERR_OK;
}

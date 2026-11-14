#ifndef SHREWD_BASIC_REFLECT_INTERNAL_H
#define SHREWD_BASIC_REFLECT_INTERNAL_H

/**
 * @file reflect-internal.h
 * @brief Width-matched value loaders shared by the reflect translation
 * units only. Not part of the public API and not installed.
 *
 * Each loader copies into a width-matched temporary (alignment-safe,
 * packed-safe, endian-safe) and widens it. The byte count comes from
 * field->sb_refl_size (the size invariant), so a long mapped to the
 * int64 descriptor is read with its real width.
 */

#include <stdint.h>
#include <string.h>

#include "shrewd-basic/reflect/reflect.h"

/* Effective ops of a field: the field-level override wins over type ops. */
static inline const shrewd_basic_reflect_ops_t *reflect_field_ops(
        const shrewd_basic_reflect_field_t *field)
{
    return (NULL != field->sb_refl_ops) ? field->sb_refl_ops
                                        : field->sb_refl_type->sb_refl_ops;
}

static inline int64_t reflect_load_i64(const unsigned char *bytes, size_t nbytes)
{
    switch (nbytes) {
    case 1: {
        int8_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (int64_t)value;
    }
    case 2: {
        int16_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (int64_t)value;
    }
    case 4: {
        int32_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (int64_t)value;
    }
    case 8: {
        int64_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

static inline uint64_t reflect_load_u64(const unsigned char *bytes, size_t nbytes)
{
    switch (nbytes) {
    case 1: {
        uint8_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (uint64_t)value;
    }
    case 2: {
        uint16_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (uint64_t)value;
    }
    case 4: {
        uint32_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return (uint64_t)value;
    }
    case 8: {
        uint64_t value = 0;
        memcpy(&value, bytes, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

static inline double reflect_load_f64(const unsigned char *bytes, size_t nbytes)
{
    if (4 == nbytes) {
        float value = 0.0f;
        memcpy(&value, bytes, sizeof(value));
        return (double)value;
    }
    if (8 == nbytes) {
        double value = 0.0;
        memcpy(&value, bytes, sizeof(value));
        return value;
    }
    return 0.0;
}

static inline void *reflect_load_ptr(const unsigned char *bytes)
{
    void *pointer = NULL;
    memcpy(&pointer, bytes, sizeof(pointer));
    return pointer;
}

static inline const char *reflect_load_str(const unsigned char *bytes)
{
    return (const char *)reflect_load_ptr(bytes);
}

#endif /* SHREWD_BASIC_REFLECT_INTERNAL_H */

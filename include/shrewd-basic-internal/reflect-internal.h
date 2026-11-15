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

/*
 * Bounded strlen for string buffers (strnlen is POSIX, not C11): scans
 * at most @p max bytes and stops at the first NUL.
 */
static inline size_t reflect_strn_len(const char *text, size_t max)
{
    size_t n = 0;
    while ((n < max) && ('\0' != text[n])) {
        ++n;
    }
    return n;
}

/*
 * Element storage of an array-shaped field, inline or dynamic. For a
 * F_DARRAY field the table pointer is followed: a NULL table yields an
 * empty view, a F_SENTINEL table is bounded by its NULL slot, and any
 * other dynamic table by the linked count member. The stride is the
 * real element width captured by the authoring macros (sizeof of the
 * element), so even width-lying basic types stay safe.
 */
typedef struct reflect_array_view {
    const unsigned char *base;
    size_t count;
    size_t stride;
} reflect_array_view_t;

static inline reflect_array_view_t reflect_array_view(const unsigned char *object,
        const shrewd_basic_reflect_field_t *field)
{
    reflect_array_view_t view;
    view.base = NULL;
    view.count = 0;
    view.stride = field->sb_refl_stride;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
        const unsigned char *table =
                (const unsigned char *)reflect_load_ptr(object + field->sb_refl_offset);
        if (NULL == table) {
            return view;
        }
        view.base = table;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL)) {
            while (NULL != reflect_load_ptr(table + (view.count * view.stride))) {
                ++view.count;
            }
        } else {
            view.count = (size_t)reflect_load_u64(
                    object + field->sb_refl_limit_offset, field->sb_refl_limit_size);
        }
        return view;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
        /* Flexible tail: inline storage, run-time bound. */
        view.base = object + field->sb_refl_offset;
        view.count = (size_t)reflect_load_u64(
                object + field->sb_refl_limit_offset, field->sb_refl_limit_size);
        return view;
    }
    view.base = object + field->sb_refl_offset;
    view.count = field->sb_refl_count;
    return view;
}

/*
 * Storage size of one object: the struct's own size plus the inline
 * flexible tails (a FAM struct is allocated with room for its tail, and
 * a clone must match). Single indirection is enough - a FAM struct can
 * never be embedded in another struct.
 */
static inline size_t reflect_object_size(const unsigned char *object,
        const shrewd_basic_reflect_type_t *type)
{
    size_t size = type->sb_refl_size;
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
            reflect_array_view_t view = reflect_array_view(object, field);
            size += view.count * view.stride;
        }
    }
    return size;
}

#endif /* SHREWD_BASIC_REFLECT_INTERNAL_H */

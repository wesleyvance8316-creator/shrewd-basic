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
    const shrewd_basic_reflect_field_t *field) {
    return (NULL != field->sb_refl_ops) ? field->sb_refl_ops : field->sb_refl_type->sb_refl_ops;
}

static inline int64_t reflect_load_i64(const unsigned char *bytes, size_t nbytes) {
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

static inline uint64_t reflect_load_u64(const unsigned char *bytes, size_t nbytes) {
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

static inline double reflect_load_f64(const unsigned char *bytes, size_t nbytes) {
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

/* Long double load (value semantics; padding bits never enter a compare). */
static inline long double reflect_load_ld(const unsigned char *bytes, size_t nbytes) {
    if (nbytes == sizeof(long double)) {
        long double value = 0.0L;
        memcpy(&value, bytes, sizeof(value));
        return value;
    }
    return 0.0L;
}

/* One complex component (half of a complex member) as long double. */
static inline long double reflect_load_cpart(const unsigned char *bytes, size_t nbytes) {
    if (4 == nbytes) {
        return (long double)reflect_load_f64(bytes, nbytes);
    }
    if (8 == nbytes) {
        return (long double)reflect_load_f64(bytes, nbytes);
    }
    return reflect_load_ld(bytes, nbytes);
}

/* Canonical hash fold shared by float/double/long double and complex parts. */
static inline uint64_t reflect_ld_hash_bits(long double number) {
    if (number != number) {
        return UINT64_C(0x7ff8000000000000); /* NaN marker */
    }
    if (0.0L == number) {
        number = 0.0L; /* fold -0.0 into +0.0 (they compare equal) */
    }
    {
        double folded = (double)number;
        uint64_t bits = 0;
        memcpy(&bits, &folded, sizeof(bits));
        return bits;
    }
}

static inline void *reflect_load_ptr(const unsigned char *bytes) {
    void *pointer = NULL;
    memcpy(&pointer, bytes, sizeof(pointer));
    return pointer;
}

static inline const char *reflect_load_str(const unsigned char *bytes) {
    return (const char *)reflect_load_ptr(bytes);
}

/*
 * Bounded strlen for string buffers (strnlen is POSIX, not C11): scans
 * at most @p max bytes and stops at the first NUL.
 */
static inline size_t reflect_strn_len(const char *text, size_t max) {
    size_t n = 0;
    while ((n < max) && ('\0' != text[n])) {
        ++n;
    }
    return n;
}

/* Code-unit width of a string kind in bytes (0 for non-strings). */
static inline size_t reflect_string_unit(shrewd_basic_reflect_kind_t kind) {
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_STRING:
        return 1;
    case SHREWD_BASIC_REFLECT_KIND_STRING16:
        return 2;
    case SHREWD_BASIC_REFLECT_KIND_STRING32:
        return 4;
    default:
        return 0;
    }
}

/* Non-zero for the three string kinds (char / char16 / char32). */
static inline int reflect_kind_is_string(shrewd_basic_reflect_kind_t kind) {
    return (0 != reflect_string_unit(kind)) ? 1 : 0;
}

/*
 * Bounded strlen in code units for wide strings: scans at most
 * @p max_units units of @p unit bytes each and stops at the first
 * all-zero unit (the NUL code unit). reflect_strn_len() is the
 * unit == 1 case.
 */
static inline size_t reflect_wstrn_len(const void *text, size_t unit, size_t max_units) {
    const unsigned char *bytes = (const unsigned char *)text;
    size_t n = 0;
    while (n < max_units) {
        size_t zero = 1;
        for (size_t b = 0; b < unit; ++b) {
            if (0 != bytes[(n * unit) + b]) {
                zero = 0;
                break;
            }
        }
        if (0 != zero) {
            break;
        }
        ++n;
    }
    return n;
}

/*
 * Code-unit-semantic string comparison: equal when both sides carry the
 * same code units up to the NUL unit. @p max_units bounds the scan
 * (SIZE_MAX for unbounded pointer strings).
 */
static inline int reflect_wstrn_eq(const void *a, const void *b, size_t unit, size_t max_units) {
    size_t len_a = reflect_wstrn_len(a, unit, max_units);
    size_t len_b = reflect_wstrn_len(b, unit, max_units);
    return ((len_a == len_b) && (0 == memcmp(a, b, len_a * unit))) ? 1 : 0;
}

/*
 * Strict UTF conversions between NUL-unit-terminated wide strings and
 * UTF-8 (reflect_utf.c). Both directions are strict: invalid UTF-8,
 * truncated sequences, lone UTF-16 surrogates and out-of-range code
 * points fail with SHREWD_BASIC_ERR_REFLECT_ENCODING - never
 * replacement characters. Measure first (dst NULL), then convert into
 * an exact buffer; no terminator is written (callers own layout).
 * unit is the code-unit width in bytes (2 or 4).
 */
int reflect_utf8_from_units(
    const void *src, size_t unit, size_t max_units, char *dst, size_t dst_cap, size_t *out_len);
int reflect_units_from_utf8(const char *src,
                            size_t src_len,
                            size_t unit,
                            void *dst,
                            size_t dst_cap_units,
                            size_t *out_units);

/* Width-matched store: the write-side twin of reflect_load_u64(). */
static inline void reflect_store_u64(unsigned char *bytes, size_t nbytes, uint64_t value) {
    switch (nbytes) {
    case 1: {
        uint8_t narrowed = (uint8_t)value;
        memcpy(bytes, &narrowed, sizeof(narrowed));
        break;
    }
    case 2: {
        uint16_t narrowed = (uint16_t)value;
        memcpy(bytes, &narrowed, sizeof(narrowed));
        break;
    }
    case 4: {
        uint32_t narrowed = (uint32_t)value;
        memcpy(bytes, &narrowed, sizeof(narrowed));
        break;
    }
    case 8:
        memcpy(bytes, &value, sizeof(value));
        break;
    default:
        break;
    }
}

/*
 * Bit-group helpers for F_BITS fields. Out-of-range positions (bad
 * author input) extract as 0 and leave the unit untouched on insert -
 * defined behavior either way.
 */
static inline uint64_t reflect_bits_extract(uint64_t unit, size_t offset, size_t width) {
    uint64_t mask = 0;
    if ((0 == width) || (64 < width) || (64 <= offset) || (64 < (offset + width))) {
        return 0;
    }
    mask = (64 == width) ? UINT64_MAX : ((UINT64_C(1) << width) - 1);
    return (unit >> offset) & mask;
}

static inline void reflect_bits_insert(uint64_t *unit,
                                       size_t offset,
                                       size_t width,
                                       uint64_t value) {
    uint64_t mask = 0;
    if ((0 == width) || (64 < width) || (64 <= offset) || (64 < (offset + width))) {
        return;
    }
    mask = (64 == width) ? UINT64_MAX : ((UINT64_C(1) << width) - 1);
    *unit = (*unit & ~(mask << offset)) | ((value & mask) << offset);
}

/*
 * Select the active variant of a F_UNION field. @p storage is the union
 * value itself; the tag member lives in the owner at a fixed
 * displacement from it. NULL means the tag names no known variant.
 */
static inline const shrewd_basic_reflect_variant_t *reflect_union_variant(
    const shrewd_basic_reflect_field_t *field, const unsigned char *storage) {
    int64_t tag = reflect_load_i64(storage - field->sb_refl_offset + field->sb_refl_limit_offset,
                                   field->sb_refl_limit_size);
    const shrewd_basic_reflect_type_t *type = field->sb_refl_type;
    for (size_t i = 0; i < type->sb_refl_variant_count; ++i) {
        if (type->sb_refl_variants[i].sb_refl_tag == tag) {
            return &type->sb_refl_variants[i];
        }
    }
    return NULL;
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
                                                      const shrewd_basic_reflect_field_t *field) {
    reflect_array_view_t view;
    view.base = NULL;
    view.count = 0;
    view.stride = field->sb_refl_stride;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
        const unsigned char *table
            = (const unsigned char *)reflect_load_ptr(object + field->sb_refl_offset);
        if (NULL == table) {
            return view;
        }
        view.base = table;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL)) {
            while (NULL != reflect_load_ptr(table + (view.count * view.stride))) {
                ++view.count;
            }
        } else {
            view.count = (size_t)reflect_load_u64(object + field->sb_refl_limit_offset,
                                                  field->sb_refl_limit_size);
        }
        return view;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
        /* Flexible tail: inline storage, run-time bound. */
        view.base = object + field->sb_refl_offset;
        view.count = (size_t)reflect_load_u64(object + field->sb_refl_limit_offset,
                                              field->sb_refl_limit_size);
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
                                         const shrewd_basic_reflect_type_t *type) {
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

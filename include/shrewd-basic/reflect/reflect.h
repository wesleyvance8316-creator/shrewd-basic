#ifndef SHREWD_BASIC_REFLECT_REFLECT_H
#define SHREWD_BASIC_REFLECT_REFLECT_H

/**
 * @file reflect.h
 * @brief Compile-time reflection: const type metadata for C structs.
 *
 * Structures are annotated with macros that expand to const descriptors,
 * so generic code can enumerate fields, read and write values, and
 * pretty-print objects without knowing the concrete type:
 *
 *   typedef struct { int32_t x; int32_t y; } point_t;
 *   SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_type, point_t,
 *       SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
 *       SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));
 *
 * Scalar and string members bind automatically through _Generic
 * (SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE); aggregates bind explicitly:
 * SHREWD_BASIC_REFLECT_FIELD_STRUCT for nested structs and
 * SHREWD_BASIC_REFLECT_FIELD_ARRAY for fixed arrays. Define each type
 * exactly once in one .c of a module and import it in sibling .c files
 * with SHREWD_BASIC_REFLECT_TYPE_DECLARE().
 *
 * Size invariant: all data access goes through field->sb_refl_size (with
 * field->sb_refl_size / field->sb_refl_count as the array stride); type->sb_refl_size is
 * informational only. This keeps access safe when a basic type maps to
 * a descriptor of a different width (e.g. long to int64 on LLP64).
 *
 * Constraints (the macros fail at compile time otherwise):
 *   - members must be basic types, strings (char * / const char *),
 *     fixed single-dimension arrays of those, nested reflected structs,
 *     or pointers described with SHREWD_BASIC_REFLECT_FIELD_PTR;
 *     bit-fields, unions, flexible array members, multi-dimension arrays
 *     and function pointers are not described;
 *   - a char buffer must use SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER
 *     (text semantics) or SHREWD_BASIC_REFLECT_FIELD_ARRAY(struct_type,
 *     buf, shrewd_basic_reflect_char) (raw bytes): plain FIELD_PRIMITIVE would
 *     treat it as a string pointer (arrays decay to pointers in _Generic);
 *   - a type needs at least one field;
 *   - the macros require C11 (_Generic) and expand to declarations, so
 *     they belong at file scope. The C API itself is C++-friendly.
 *
 * Strings: the field bytes hold the char pointer itself; get/set copy
 * the pointer and ownership stays with the caller. All value access is
 * raw memcpy of field->sb_refl_size, never a dereference of offset-derived
 * typed pointers (packed and odd alignments stay well-defined).
 *
 * String buffers: a char[N] member described with
 * SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER carries C-string semantics -
 * dump renders it quoted and escaped, equal/diff/hash work on its
 * content up to the first NUL (bytes past the terminator are ignored)
 * and visit reports one LEAF. get/set still move the raw N bytes and
 * paths may index single characters ("title[2]").
 *
 * Pointers: a FIELD_PTR member holds a pointer to its declared target
 * type (shrewd_basic_reflect_void for opaque targets). get/set copy the
 * pointer value - the module never allocates, frees or deep-copies.
 * dump follows the pointer and renders the pointee ((null) for NULL,
 * address-only for opaque targets, depth-capped for self-referential
 * types); a followed pointee scalar is read with the pointee type's
 * canonical width.
 *
 * Traversal: shrewd_basic_reflect_visit() reports the value graph event
 * by event (ENTER/LEAF/EXIT with path strings) so applications -
 * serializers, validators, exporters - can walk objects without writing
 * their own recursion. Pointer targets are followed like dump; a cycle
 * or the depth cap reports a CYCLE event instead of expanding.
 *
 * Operations: a type (or a single field) may carry a
 * shrewd_basic_reflect_ops_t vtable so generic code can read, write,
 * check, copy and destroy targets it cannot describe otherwise. The
 * callbacks are consulted per operation - a missing callback falls back
 * to the metadata-driven behavior - and field ops override type ops.
 * get/set/check run for values and pointees alike; copy/destroy have
 * object semantics (pointer targets and the root of
 * shrewd_basic_reflect_copy/destroy): copy is clone-out through the
 * application's allocator when ops exist, otherwise struct pointees are
 * cloned recursively through shrewd_basic_platform_alloc, scalar
 * pointees are cloned at their canonical width, string pointees stay
 * borrowed (aliased, never freed) and opaque targets without ops cannot
 * be copied. destroy is the inverse of copy and must only run on graphs
 * that own their pointer targets (clone outputs or platform_alloc-built
 * data); pointer targets into caller stack/static storage need ops (a
 * no-op destroy) or must not be destroyed. Owned STRING members use
 * SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED: copy clones their content
 * (strings shared between fields stay shared) and destroy frees them.
 * For owned strings behind a pointer, provide ops->copy/ops->destroy on
 * the field (the pointer-target hooks) - plain STRING values have no
 * ops hook and need the flag.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Value kinds. New members (enum, pointer, ...) may be appended later;
 * every switch on kind falls back to a neutral "unknown" rendering.
 */
typedef enum shrewd_basic_reflect_kind {
    SHREWD_BASIC_REFLECT_KIND_BOOL = 0,
    SHREWD_BASIC_REFLECT_KIND_CHAR,
    SHREWD_BASIC_REFLECT_KIND_INT8,
    SHREWD_BASIC_REFLECT_KIND_UINT8,
    SHREWD_BASIC_REFLECT_KIND_INT16,
    SHREWD_BASIC_REFLECT_KIND_UINT16,
    SHREWD_BASIC_REFLECT_KIND_INT32,
    SHREWD_BASIC_REFLECT_KIND_UINT32,
    SHREWD_BASIC_REFLECT_KIND_INT64,
    SHREWD_BASIC_REFLECT_KIND_UINT64,
    SHREWD_BASIC_REFLECT_KIND_ENUM, /**< Enumerated value (int-compatible, name/value table). */
    SHREWD_BASIC_REFLECT_KIND_FLOAT,
    SHREWD_BASIC_REFLECT_KIND_DOUBLE,
    SHREWD_BASIC_REFLECT_KIND_STRING, /**< char * / const char * member. */
    SHREWD_BASIC_REFLECT_KIND_STRUCT, /**< Aggregate with field descriptors. */
    SHREWD_BASIC_REFLECT_KIND_ARRAY,  /**< Fixed array value: one row of a multi-dimension array. */
    SHREWD_BASIC_REFLECT_KIND_UNION,  /**< Tagged union: one variant active per tag value. */
    SHREWD_BASIC_REFLECT_KIND_OPAQUE  /**< Unknown pointee (shrewd_basic_reflect_void). */
} shrewd_basic_reflect_kind_t;

typedef struct shrewd_basic_reflect_type shrewd_basic_reflect_type_t;
typedef struct shrewd_basic_reflect_ops shrewd_basic_reflect_ops_t;

/**
 * Application callbacks for a described type. Every member is optional
 * (NULL falls back to the metadata-driven behavior where one exists) and
 * each receives the target value itself (a pointee for pointer fields).
 * Errors return shrewd-basic/platform/error.h codes.
 */
struct shrewd_basic_reflect_ops {
    size_t value_size; /**< Width of the canonical value used by get/set. */
    /** Copy the canonical value of @p pointee into @p out_value. */
    int (*get)(const void *pointee, void *out_value, size_t value_size);
    /** Write @p value into @p pointee as the canonical value. */
    int (*set)(void *pointee, const void *value, size_t value_size);
    /** Validate the invariants of @p pointee. */
    int (*check)(const void *pointee);
    /** Compare @p a and @p b for equality. */
    int (*equal)(const void *a, const void *b, bool *out_equal);
    /**
     * Render @p pointee as text for dump (NUL-terminated into @p buf,
     * truncated to @p buf_size). Used for followed pointer targets;
     * without it dump falls back to the pointee value or address.
     */
    int (*render)(const void *pointee, char *buf, size_t buf_size);
    /** Deep-copy @p pointee into a new object stored into @p *out. */
    int (*copy)(void **out, const void *pointee);
    /** Release an object produced by @c copy (or an equivalent owner). */
    void (*destroy)(void *pointee);
    /**
     * Hash @p value consistently with @c equal (equal values must hash
     * equal). Used by shrewd_basic_reflect_hash() in place of the
     * metadata-driven fold.
     */
    int (*hash)(const void *value, size_t *out_hash);
};

/**
 * One reflected member of a struct type. Every descriptor member carries
 * the sb_refl_ prefix so common names like type/name/size stay free for
 * macro parameters and user code - un-prefixed designators would be
 * rewritten by such parameter names during macro expansion.
 */
typedef struct shrewd_basic_reflect_field {
    const char *sb_refl_name;                        /**< Member name, C string literal. */
    const shrewd_basic_reflect_type_t *sb_refl_type; /**< Value type, or array element type. */
    size_t sb_refl_offset;                           /**< offsetof(owner struct, member). */
    size_t sb_refl_size;                             /**< sizeof(member); arrays: total bytes. */
    size_t sb_refl_count;                            /**< Elements; 1 unless fixed array. */
    size_t sb_refl_stride;                           /**< One element's width. */
    uint32_t sb_refl_flags;                          /**< SHREWD_BASIC_REFLECT_FIELD_F_*. */
    const shrewd_basic_reflect_ops_t *sb_refl_ops;   /**< Field-level override; NULL = type ops. */
    size_t sb_refl_limit_offset; /**< F_DARRAY: owner offset of the count member. */
    size_t sb_refl_limit_size;   /**< Its width; both 0 without a count member. */
    size_t sb_refl_bit_offset;   /**< F_BITS: first bit inside the unit. */
    size_t sb_refl_bit_width;    /**< F_BITS: number of bits in the group. */
} shrewd_basic_reflect_field_t;

/** One name/value pair of a reflected enum. */
typedef struct shrewd_basic_reflect_enum_entry {
    const char *sb_refl_name; /**< Enum constant name, C string literal. */
    int64_t sb_refl_value;    /**< The constant's value. */
} shrewd_basic_reflect_enum_entry_t;

/**
 * One variant of a tagged union: the field list describing the union
 * members that are live when the tag member holds @c sb_refl_tag. The
 * field offsets are relative to the union storage.
 */
typedef struct shrewd_basic_reflect_variant {
    int64_t sb_refl_tag;        /**< Tag value selecting this variant. */
    const char *sb_refl_name;   /**< Variant label (the tag constant). */
    size_t sb_refl_field_count; /**< Fields live in this variant. */
    const shrewd_basic_reflect_field_t *sb_refl_fields;
} shrewd_basic_reflect_variant_t;

/**
 * Descriptor ABI version. Every descriptor carries the version of the
 * headers it was authored against; shrewd_basic_reflect_check_type()
 * flags mismatches, which catch stale descriptors compiled against an
 * older shrewd-basic (e.g. before the align/abi members existed).
 * Bump when the descriptor structs change shape or semantics.
 */
#define SHREWD_BASIC_REFLECT_ABI ((size_t)1)

/** A described type: a struct layout, a primitive/string kind, or an enum. */
struct shrewd_basic_reflect_type {
    shrewd_basic_reflect_kind_t sb_refl_kind; /**< STRUCT for aggregates. */
    const char *sb_refl_name;                 /**< "person_t" or "int32". */
    size_t sb_refl_size;                      /**< Informational width. */
    size_t sb_refl_align;                     /**< _Alignof of the C type (0 = unknown). */
    size_t sb_refl_abi;                       /**< SHREWD_BASIC_REFLECT_ABI of the headers used. */
    size_t sb_refl_field_count;               /**< 0 for primitives. */
    const shrewd_basic_reflect_field_t *sb_refl_fields;    /**< NULL for primitives. */
    const shrewd_basic_reflect_enum_entry_t *sb_refl_enum; /**< NULL unless ENUM. */
    size_t sb_refl_enum_count;                             /**< 0 unless ENUM. */
    const shrewd_basic_reflect_ops_t *sb_refl_ops;         /**< Type-level ops; NULL = defaults. */
    const shrewd_basic_reflect_type_t *sb_refl_elem;       /**< ARRAY: element descriptor. */
    size_t sb_refl_elem_count;                             /**< ARRAY: elements in one row. */
    size_t sb_refl_elem_stride;                            /**< ARRAY: real width of one element. */
    const shrewd_basic_reflect_variant_t *sb_refl_variants; /**< UNION: variant table. */
    size_t sb_refl_variant_count;                           /**< UNION: variants. */
};

/** Field flag: the field is a fixed-size array of @c count elements. */
#define SHREWD_BASIC_REFLECT_FIELD_F_ARRAY ((uint32_t)1u)

/** Field flag: the field is a pointer to the field's type (or an array of them). */
#define SHREWD_BASIC_REFLECT_FIELD_F_PTR ((uint32_t)2u)

/**
 * Field flag: the field is a pointer to a heap array of elements (a
 * dynamic array). The element count comes from the linked count member
 * (sb_refl_limit_*) or, with F_SENTINEL, from a NULL slot terminator;
 * F_PTR adds one more indirection (a table of element pointers).
 */
#define SHREWD_BASIC_REFLECT_FIELD_F_DARRAY ((uint32_t)4u)

/** Field flag: a F_PTR|F_DARRAY table is NULL-terminated (argv style). */
#define SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL ((uint32_t)8u)

/**
 * Field flag: a flexible tail (C99 flexible array member, or the GNU
 * zero-length array form). The storage is inline at the end of the owner
 * struct and its element count comes from the linked count member at
 * walk time - a dynamic array without the extra indirection.
 */
#define SHREWD_BASIC_REFLECT_FIELD_F_FLEX ((uint32_t)16u)

/**
 * Field flag: an inline char array holding a NUL-terminated string (a
 * text buffer). Rendered and compared as a C string - dump quotes and
 * escapes it, equal/diff/hash work on its content up to the first NUL
 * (bytes past the terminator are ignored), visit reports one LEAF. The
 * storage is still an array of count chars: get/set move the raw bytes
 * and paths may index single characters ("title[2]").
 */
#define SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF ((uint32_t)32u)

/**
 * Field flag: the graph OWNS the string a STRING field points at.
 * shrewd_basic_reflect_copy() clones its content (strings shared between
 * fields stay shared in the clone) and shrewd_basic_reflect_destroy()
 * frees it - unlike the default borrowed-string semantics where copy
 * aliases and destroy never touches strings. NULL strings stay NULL.
 * Comparison, hashing and dump are unaffected (content semantics).
 */
#define SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED ((uint32_t)64u)

/**
 * Field flag: a bit group inside a real integer unit member (register
 * bit-fields, protocol headers). The field's value is the group's bits
 * zero-extended to the unit's width: get/set exchange that value
 * (n == sizeof(unit), read-modify-write on the unit) and
 * equal/diff/hash/dump use the bits only - other bits of the unit are
 * ignored. C bit-fields cannot be described (their layout is
 * implementation-defined and not introspectable); this flag covers
 * explicit unit + bit position instead.
 */
#define SHREWD_BASIC_REFLECT_FIELD_F_BITS ((uint32_t)128u)

/**
 * Field flag: a tagged union member. The variant is selected at walk
 * time through the linked tag member (sb_refl_limit_* carries its
 * location in the owner); only the active variant's fields are compared,
 * copied, destroyed and rendered. A tag with no variant renders as
 * unknown and compares by raw union bytes; shrewd_basic_reflect_copy()
 * copies the raw storage in that case (nothing to descend into).
 */
#define SHREWD_BASIC_REFLECT_FIELD_F_UNION ((uint32_t)256u)

/** Primitive descriptors; pass them to FIELD_ARRAY/FIELD_STRUCT by name. */
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_bool;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_char;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_int8;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint8;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_int16;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint16;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_int32;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint32;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_int64;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint64;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_float;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_double;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_string;
extern const shrewd_basic_reflect_type_t shrewd_basic_reflect_void;

/*
 * Map a member expression to its primitive descriptor via _Generic.
 * Only basic types are listed (never stdint aliases - they would collide
 * with the basic types they stand for) and there is no default branch:
 * an unsupported member type is a compile error by design.
 */
#define SHREWD_BASIC_REFLECT_PRIM_TYPE(expr)                                                       \
    _Generic((expr),                                                                               \
        _Bool: &shrewd_basic_reflect_bool,                                                         \
        char: &shrewd_basic_reflect_char,                                                          \
        signed char: &shrewd_basic_reflect_int8,                                                   \
        unsigned char: &shrewd_basic_reflect_uint8,                                                \
        short: &shrewd_basic_reflect_int16,                                                        \
        unsigned short: &shrewd_basic_reflect_uint16,                                              \
        int: &shrewd_basic_reflect_int32,                                                          \
        unsigned int: &shrewd_basic_reflect_uint32,                                                \
        long: &shrewd_basic_reflect_int64,                                                         \
        unsigned long: &shrewd_basic_reflect_uint64,                                               \
        long long: &shrewd_basic_reflect_int64,                                                    \
        unsigned long long: &shrewd_basic_reflect_uint64,                                          \
        float: &shrewd_basic_reflect_float,                                                        \
        double: &shrewd_basic_reflect_double,                                                      \
        char *: &shrewd_basic_reflect_string,                                                      \
        const char *: &shrewd_basic_reflect_string)

/* Describe a primitive member (scalar or string); type deduced automatically. */
#define SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(struct_type, member)                                  \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = SHREWD_BASIC_REFLECT_PRIM_TYPE(((struct_type *)0)->member),                   \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = (uint32_t)0}

/* Describe a nested struct member; ref is its descriptor without '&'. */
#define SHREWD_BASIC_REFLECT_FIELD_STRUCT(struct_type, member, ref)                                \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = (uint32_t)0}

/* Describe a fixed-size array member; ref is the element descriptor. */
#define SHREWD_BASIC_REFLECT_FIELD_ARRAY(struct_type, member, ref)                                 \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = sizeof(((struct_type *)0)->member) / sizeof(((struct_type *)0)->member[0]),  \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_ARRAY}

/*
 * Describe a pointer member; ref is the pointee descriptor passed without
 * '&' (shrewd_basic_reflect_void for opaque targets like void *). get/set
 * copy the pointer value; dump follows it. A self-referential type may
 * reference its own descriptor here (the DEFINE macro forward-declares it).
 */
#define SHREWD_BASIC_REFLECT_FIELD_PTR(struct_type, member, ref)                                   \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR}

/* Describe a fixed-size array of pointers; ref is the pointee descriptor. */
#define SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY(struct_type, member, ref)                             \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = sizeof(((struct_type *)0)->member) / sizeof(((struct_type *)0)->member[0]),  \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY}

/*
 * Compile-time type registration: define `var` exactly once, in a single
 * .c of the module; sibling translation units import it with
 * SHREWD_BASIC_REFLECT_TYPE_DECLARE(var). The descriptor object gets
 * external linkage so &var is an address constant usable inside other
 * descriptors (nested and, later, self-referential types); the field
 * table `var##_fields` stays file-local. The trailing ';' belongs to the
 * call, like the log module macros. Descriptor members carry the sb_refl_ prefix, so common
 * parameter names like type or name are safe.
 */
#define SHREWD_BASIC_REFLECT_TYPE_DEFINE(var, struct_type, ...)                                    \
    extern const shrewd_basic_reflect_type_t var;                                                  \
    static const shrewd_basic_reflect_field_t var##_fields[] = {__VA_ARGS__};                      \
    const shrewd_basic_reflect_type_t var                                                          \
        = {.sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_STRUCT,                                       \
           .sb_refl_name = #struct_type,                                                           \
           .sb_refl_size = sizeof(struct_type),                                                    \
           .sb_refl_align = _Alignof(struct_type),                                                 \
           .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,                                                \
           .sb_refl_field_count = sizeof(var##_fields) / sizeof(var##_fields[0]),                  \
           .sb_refl_fields = var##_fields}

/** Import a type defined by SHREWD_BASIC_REFLECT_TYPE_DEFINE() elsewhere. */
#define SHREWD_BASIC_REFLECT_TYPE_DECLARE(var) extern const shrewd_basic_reflect_type_t var

/*
 * Compile-time registration of a fixed array value type: `var` describes
 * one row of a multi-dimension array (elem_type[count] - e.g. the [4]
 * row of an int32_t m[3][4]). The element C type is passed for its real
 * width (the size invariant), the element descriptor for its semantics;
 * rows nest (a row of rows describes 3D). Use it as the element of
 * SHREWD_BASIC_REFLECT_FIELD_ARRAY:
 *
 *   SHREWD_BASIC_REFLECT_ARRAY_DEFINE(row4_type, shrewd_basic_reflect_int32,
 *                                     int32_t, 4);
 *   SHREWD_BASIC_REFLECT_FIELD_ARRAY(matrix_t, m, row4_type)
 *
 * describes the member "int32_t m[3][4]" (3 rows of 4).
 */
#define SHREWD_BASIC_REFLECT_ARRAY_DEFINE(var, elem_ref, elem_type, count)                         \
    extern const shrewd_basic_reflect_type_t var;                                                  \
    const shrewd_basic_reflect_type_t var = {.sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_ARRAY,      \
                                             .sb_refl_name = #elem_type "[" #count "]",            \
                                             .sb_refl_size = sizeof(elem_type) * (size_t)(count),  \
                                             .sb_refl_align = _Alignof(elem_type),                 \
                                             .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,              \
                                             .sb_refl_field_count = 0,                             \
                                             .sb_refl_fields = NULL,                               \
                                             .sb_refl_elem = &(elem_ref),                          \
                                             .sb_refl_elem_count = (size_t)(count),                \
                                             .sb_refl_elem_stride = sizeof(elem_type)}

/*
 * SHREWD_BASIC_REFLECT_TYPE_DEFINE with a type-level ops vtable; the ops
 * object is referenced without '&'. Descriptor members carry the sb_refl_ prefix, so common
 * parameter names like type or name are safe.
 */
#define SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(var, struct_type, ops_ref, ...)                       \
    extern const shrewd_basic_reflect_type_t var;                                                  \
    static const shrewd_basic_reflect_field_t var##_fields[] = {__VA_ARGS__};                      \
    const shrewd_basic_reflect_type_t var                                                          \
        = {.sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_STRUCT,                                       \
           .sb_refl_name = #struct_type,                                                           \
           .sb_refl_size = sizeof(struct_type),                                                    \
           .sb_refl_align = _Alignof(struct_type),                                                 \
           .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,                                                \
           .sb_refl_field_count = sizeof(var##_fields) / sizeof(var##_fields[0]),                  \
           .sb_refl_fields = var##_fields,                                                         \
           .sb_refl_ops = &(ops_ref)}

/*
 * Describe an opaque target type with its ops vtable (one definition per
 * type, shared across translation units with TYPE_DECLARE). Use
 * shrewd_basic_reflect_void for opaque targets without ops.
 */
#define SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(var, type_name, ops_ref)                                \
    extern const shrewd_basic_reflect_type_t var;                                                  \
    const shrewd_basic_reflect_type_t var = {.sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_OPAQUE,     \
                                             .sb_refl_name = (type_name),                          \
                                             .sb_refl_size = 0,                                    \
                                             .sb_refl_align = 0,                                   \
                                             .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,              \
                                             .sb_refl_field_count = 0,                             \
                                             .sb_refl_fields = NULL,                               \
                                             .sb_refl_ops = &(ops_ref)}

/*
 * One enum constant; the name is the stringified constant and the value
 * is the constant itself. Combine into SHREWD_BASIC_REFLECT_ENUM_DEFINE.
 */
#define SHREWD_BASIC_REFLECT_ENUM_VALUE(value)                                                     \
    {.sb_refl_name = #value, .sb_refl_value = (int64_t)(value)}

/*
 * Compile-time enum registration: define `var` exactly once per module,
 * share it with TYPE_DECLARE. Every constant takes one ENUM_VALUE entry
 * (at least one).
 */
#define SHREWD_BASIC_REFLECT_ENUM_DEFINE(var, enum_type, ...)                                      \
    extern const shrewd_basic_reflect_type_t var;                                                  \
    static const shrewd_basic_reflect_enum_entry_t var##_entries[] = {__VA_ARGS__};                \
    const shrewd_basic_reflect_type_t var                                                          \
        = {.sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_ENUM,                                         \
           .sb_refl_name = #enum_type,                                                             \
           .sb_refl_size = sizeof(enum_type),                                                      \
           .sb_refl_align = _Alignof(enum_type),                                                   \
           .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,                                                \
           .sb_refl_field_count = 0,                                                               \
           .sb_refl_fields = NULL,                                                                 \
           .sb_refl_enum = var##_entries,                                                          \
           .sb_refl_enum_count = sizeof(var##_entries) / sizeof(var##_entries[0]),                 \
           .sb_refl_ops = NULL}

/* Describe an enum member; ref is its ENUM_DEFINE descriptor without '&'. */
#define SHREWD_BASIC_REFLECT_FIELD_ENUM(struct_type, member, ref)                                  \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = (uint32_t)0,                                                                 \
     .sb_refl_ops = NULL}

/*
 * Dynamic arrays: the storage lives behind the pointer in @p member.
 * The element type @p ref is the type of one array slot:
 *   - FIELD_DARRAY: contiguous values (a vector) bounded by the count
 *     member @p count_member ("int32_t *values" + size_t value_count);
 *   - FIELD_PTR_DARRAY: slots are pointers to @p ref, same bound
 *     ("point_t **points" + size_t point_count);
 *   - FIELD_SENTINEL: values terminated by a NULL value slot (argv
 *     style - "char **argv" is an array of string VALUES, not another
 *     level of indirection);
 *   - FIELD_PTR_SENTINEL: slots are pointers to @p ref terminated by a
 *     NULL slot;
 *   - FIELD_FLEX: a flexible tail (@c T member[]) sized by the count
 *     member - a dynamic array inline at the end of the owner struct.
 * The count member must hold an integer; its value is read at walk
 * time.
 */
#define SHREWD_BASIC_REFLECT_FIELD_DARRAY(struct_type, member, count_member, ref)                  \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(*(((struct_type *)0)->member)),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_DARRAY,                                         \
     .sb_refl_ops = NULL,                                                                          \
     .sb_refl_limit_offset = offsetof(struct_type, count_member),                                  \
     .sb_refl_limit_size = sizeof(((struct_type *)0)->count_member)}

#define SHREWD_BASIC_REFLECT_FIELD_PTR_DARRAY(struct_type, member, count_member, ref)              \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(*(((struct_type *)0)->member)),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_PTR,      \
     .sb_refl_ops = NULL,                                                                          \
     .sb_refl_limit_offset = offsetof(struct_type, count_member),                                  \
     .sb_refl_limit_size = sizeof(((struct_type *)0)->count_member)}

#define SHREWD_BASIC_REFLECT_FIELD_SENTINEL(struct_type, member, ref)                              \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(*(((struct_type *)0)->member)),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL, \
     .sb_refl_ops = NULL,                                                                          \
     .sb_refl_limit_offset = 0,                                                                    \
     .sb_refl_limit_size = 0}

#define SHREWD_BASIC_REFLECT_FIELD_PTR_SENTINEL(struct_type, member, ref)                          \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(*(((struct_type *)0)->member)),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_PTR       \
                      | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL,                                     \
     .sb_refl_ops = NULL,                                                                          \
     .sb_refl_limit_offset = 0,                                                                    \
     .sb_refl_limit_size = 0}

/* FIELD_PRIMITIVE with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE_OPS(struct_type, member, ops_ref)                     \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = SHREWD_BASIC_REFLECT_PRIM_TYPE(((struct_type *)0)->member),                   \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = (uint32_t)0,                                                                 \
     .sb_refl_ops = &(ops_ref)}

/* FIELD_ARRAY with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_ARRAY_OPS(struct_type, member, ref, ops_ref)                    \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = sizeof(((struct_type *)0)->member) / sizeof(((struct_type *)0)->member[0]),  \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_ARRAY,                                          \
     .sb_refl_ops = &(ops_ref)}

/*
 * Describe a char[N] text buffer: an inline char array with C-string
 * semantics (F_ARRAY | F_STRINGBUF). dump renders it quoted and escaped,
 * equal/diff/hash use its content up to the first NUL, visit reports one
 * LEAF; get/set move the raw N bytes. Use FIELD_ARRAY for raw byte
 * buffers whose padding bytes must compare too.
 */
#define SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(struct_type, member)                              \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &shrewd_basic_reflect_char,                                                   \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = sizeof(((struct_type *)0)->member) / sizeof(((struct_type *)0)->member[0]),  \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF}

/* FIELD_STRING_BUFFER with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER_OPS(struct_type, member, ops_ref)                 \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &shrewd_basic_reflect_char,                                                   \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = sizeof(((struct_type *)0)->member) / sizeof(((struct_type *)0)->member[0]),  \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF, \
     .sb_refl_ops = &(ops_ref)}

/*
 * Describe a STRING member whose content the graph owns: copy clones it
 * (shared strings stay shared) and destroy frees it, instead of the
 * default borrowed-string behavior. NULL stays NULL. Equal, hashing and
 * dump are content-based and unchanged.
 */
#define SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(struct_type, member)                               \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &shrewd_basic_reflect_string,                                                 \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED}

/* FIELD_STRING_OWNED with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED_OPS(struct_type, member, ops_ref)                  \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &shrewd_basic_reflect_string,                                                 \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED,                                   \
     .sb_refl_ops = &(ops_ref)}

/*
 * Describe a bit group inside a real integer unit member. @p unit is the
 * compile-time member holding the bits (offsetof works on it); @p name
 * is the group's label - it need not be a member - and bit_offset/
 * bit_width come from the register map or protocol spec. Multiple
 * groups may share one unit. get/set exchange the group zero-extended to
 * the unit's width (read-modify-write); equal/diff/hash compare the
 * group's bits only. C bit-field members themselves cannot be described
 * (implementation-defined layout) - this macro covers explicit units.
 */
#define SHREWD_BASIC_REFLECT_FIELD_BITS(struct_type, unit, name, bit_offset, bit_width)            \
    {.sb_refl_name = #name,                                                                        \
     .sb_refl_type = SHREWD_BASIC_REFLECT_PRIM_TYPE(((struct_type *)0)->unit),                     \
     .sb_refl_offset = offsetof(struct_type, unit),                                                \
     .sb_refl_size = sizeof(((struct_type *)0)->unit),                                             \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->unit),                                           \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_BITS,                                           \
     .sb_refl_bit_offset = (size_t)(bit_offset),                                                   \
     .sb_refl_bit_width = (size_t)(bit_width)}

/*
 * One variant of a tagged union: @p tag_value (an integer constant, e.g.
 * an enum constant) selects it and the fields describe the union members
 * that are live in that variant (at least one). Combine into
 * SHREWD_BASIC_REFLECT_UNION_DEFINE.
 */
#define SHREWD_BASIC_REFLECT_UNION_VARIANT(tag_value, ...)                                         \
    {                                                                                              \
        .sb_refl_tag = (int64_t)(tag_value), .sb_refl_name = #tag_value,                           \
        .sb_refl_field_count = (sizeof((const shrewd_basic_reflect_field_t[]){__VA_ARGS__})        \
                                / sizeof(shrewd_basic_reflect_field_t)),                           \
        .sb_refl_fields = (const shrewd_basic_reflect_field_t[]) {                                 \
            __VA_ARGS__                                                                            \
        }                                                                                          \
    }

/*
 * Compile-time registration of a tagged union type: `var` describes
 * union_type as a set of variants; every UNION_VARIANT field list uses
 * the FIELD_* macros on the union's members (offsets relative to the
 * union). Define once per module, import with TYPE_DECLARE.
 */
#define SHREWD_BASIC_REFLECT_UNION_DEFINE(var, union_type, ...)                                    \
    extern const shrewd_basic_reflect_type_t var;                                                  \
    static const shrewd_basic_reflect_variant_t var##_variants[] = {__VA_ARGS__};                  \
    const shrewd_basic_reflect_type_t var                                                          \
        = {.sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UNION,                                        \
           .sb_refl_name = #union_type,                                                            \
           .sb_refl_size = sizeof(union_type),                                                     \
           .sb_refl_align = _Alignof(union_type),                                                  \
           .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,                                                \
           .sb_refl_field_count = 0,                                                               \
           .sb_refl_fields = NULL,                                                                 \
           .sb_refl_variants = var##_variants,                                                     \
           .sb_refl_variant_count = sizeof(var##_variants) / sizeof(var##_variants[0])}

/*
 * Describe a tagged union member: @p member is the union storage inside
 * the owner struct, @p union_var its UNION_DEFINE descriptor (without
 * '&') and @p tag_member the owner's integer member selecting the active
 * variant. The tag is read at walk time; only the active variant's
 * fields take part in compare/copy/destroy/dump/visit.
 */
#define SHREWD_BASIC_REFLECT_FIELD_UNION(struct_type, member, union_var, tag_member)               \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(union_var),                                                                 \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_UNION,                                          \
     .sb_refl_limit_offset = offsetof(struct_type, tag_member),                                    \
     .sb_refl_limit_size = sizeof(((struct_type *)0)->tag_member)}

/* FIELD_STRUCT with a field-level ops override (takes precedence over type ops). */
#define SHREWD_BASIC_REFLECT_FIELD_STRUCT_OPS(struct_type, member, ref, ops_ref)                   \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = (uint32_t)0,                                                                 \
     .sb_refl_ops = &(ops_ref)}

/* FIELD_PTR with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_PTR_OPS(struct_type, member, ref, ops_ref)                      \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(((struct_type *)0)->member),                                         \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR,                                            \
     .sb_refl_ops = &(ops_ref)}

/* FIELD_PTR_ARRAY with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY_OPS(struct_type, member, ref, ops_ref)                \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = sizeof(((struct_type *)0)->member) / sizeof(((struct_type *)0)->member[0]),  \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY,       \
     .sb_refl_ops = &(ops_ref)}

/*
 * Flexible tail (@c T member[] or the GNU @c T member[0]): inline
 * storage sized at run time by the count member. The member must be the
 * struct's last one (a C requirement) and the owner struct cannot be
 * embedded elsewhere. The zero-length form is a GNU extension and warns
 * under -Wpedantic.
 */
#define SHREWD_BASIC_REFLECT_FIELD_FLEX(struct_type, member, count_member, ref)                    \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = 0,                                                                            \
     .sb_refl_count = 0,                                                                           \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_FLEX,      \
     .sb_refl_ops = NULL,                                                                          \
     .sb_refl_limit_offset = offsetof(struct_type, count_member),                                  \
     .sb_refl_limit_size = sizeof(((struct_type *)0)->count_member)}

/* Dynamic-array macros with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_FLEX_OPS(struct_type, member, count_member, ref, ops_ref)       \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = 0,                                                                            \
     .sb_refl_count = 0,                                                                           \
     .sb_refl_stride = sizeof(((struct_type *)0)->member[0]),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_FLEX,      \
     .sb_refl_ops = &(ops_ref),                                                                    \
     .sb_refl_limit_offset = offsetof(struct_type, count_member),                                  \
     .sb_refl_limit_size = sizeof(((struct_type *)0)->count_member)}

#define SHREWD_BASIC_REFLECT_FIELD_DARRAY_OPS(struct_type, member, count_member, ref, ops_ref)     \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(*(((struct_type *)0)->member)),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_DARRAY,                                         \
     .sb_refl_ops = &(ops_ref),                                                                    \
     .sb_refl_limit_offset = offsetof(struct_type, count_member),                                  \
     .sb_refl_limit_size = sizeof(((struct_type *)0)->count_member)}

#define SHREWD_BASIC_REFLECT_FIELD_PTR_DARRAY_OPS(struct_type, member, count_member, ref, ops_ref) \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(*(((struct_type *)0)->member)),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_PTR,      \
     .sb_refl_ops = &(ops_ref),                                                                    \
     .sb_refl_limit_offset = offsetof(struct_type, count_member),                                  \
     .sb_refl_limit_size = sizeof(((struct_type *)0)->count_member)}

#define SHREWD_BASIC_REFLECT_FIELD_PTR_SENTINEL_OPS(struct_type, member, ref, ops_ref)             \
    {.sb_refl_name = #member,                                                                      \
     .sb_refl_type = &(ref),                                                                       \
     .sb_refl_offset = offsetof(struct_type, member),                                              \
     .sb_refl_size = sizeof(((struct_type *)0)->member),                                           \
     .sb_refl_count = (size_t)1,                                                                   \
     .sb_refl_stride = sizeof(*(((struct_type *)0)->member)),                                      \
     .sb_refl_flags = SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_PTR       \
                      | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL,                                     \
     .sb_refl_ops = &(ops_ref),                                                                    \
     .sb_refl_limit_offset = 0,                                                                    \
     .sb_refl_limit_size = 0}

/** Name of a kind, e.g. "int32"; unknown values map to "unknown". */
const char *shrewd_basic_reflect_kind_name(shrewd_basic_reflect_kind_t kind);

/**
 * Name of an enum @p value in the ENUM descriptor @p type.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when the value has no constant,
 * SHREWD_BASIC_ERR_ARG_INVALID for NULL arguments or a non-enum type.
 */
int shrewd_basic_reflect_enum_find_name(const shrewd_basic_reflect_type_t *type,
                                        int64_t value,
                                        const char **out_name);

/**
 * Number of fields described by @p type (0 for primitives and enums,
 * or when @p type is NULL).
 */
size_t shrewd_basic_reflect_type_field_count(const shrewd_basic_reflect_type_t *type);

/**
 * Field at @p index of @p type, or NULL when the index is out of range
 * or @p type is NULL. Together with
 * shrewd_basic_reflect_type_field_count() this iterates a type without
 * touching the descriptor layout.
 */
const shrewd_basic_reflect_field_t *
shrewd_basic_reflect_type_field_at(const shrewd_basic_reflect_type_t *type, size_t index);

/** Number of constants described by an ENUM type (0 otherwise). */
size_t shrewd_basic_reflect_enum_count(const shrewd_basic_reflect_type_t *type);

/** Enum constant at @p index, or NULL when out of range or not an enum. */
const shrewd_basic_reflect_enum_entry_t *
shrewd_basic_reflect_enum_at(const shrewd_basic_reflect_type_t *type, size_t index);

/**
 * Value of the enum constant @p name in the ENUM descriptor @p type.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no constant matches,
 * SHREWD_BASIC_ERR_ARG_INVALID for NULL arguments or a non-enum type.
 */
int shrewd_basic_reflect_enum_find_value(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         int64_t *out_value);

/**
 * Find a field by name (flat, single level).
 * Returns SHREWD_BASIC_ERR_OK and stores a pointer into type->sb_refl_fields
 * (static storage) into @p *out, SHREWD_BASIC_ERR_KEY_NOTFOUND when no
 * field matches, SHREWD_BASIC_ERR_ARG_INVALID for NULL arguments.
 */
int shrewd_basic_reflect_type_find_field(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         const shrewd_basic_reflect_field_t **out);

/**
 * Effective alignment of a field's storage: alignof(void *) for
 * pointer-shaped fields (F_PTR and F_DARRAY tables), otherwise the
 * shrewd_basic_reflect_type_t.sb_refl_align of the field's type (0 when
 * unknown). NULL argument returns 0.
 */
size_t shrewd_basic_reflect_field_align(const shrewd_basic_reflect_field_t *field);

/**
 * Validate a descriptor's invariants before trusting it: ABI version
 * (SHREWD_BASIC_REFLECT_ABI), kind bounds, field table consistency
 * (names, references, offsets and sizes inside the type, stride
 * nonzero), F_BITS ranges inside their unit, union/enum/array reference
 * shapes. Use it in tests and at startup to catch stale or hand-edited
 * descriptors; it walks the type one level deep only. Returns
 * SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_REFLECT_MISMATCH when an
 * invariant is broken (the offending name is logged), or
 * SHREWD_BASIC_ERR_ARG_INVALID for NULL.
 */
int shrewd_basic_reflect_check_type(const shrewd_basic_reflect_type_t *type);

/**
 * Copy the field's raw bytes out of @p object into @p out_value.
 * @p value_size must equal field->sb_refl_size or the call fails with
 * SHREWD_BASIC_ERR_REFLECT_MISMATCH. For string fields the bytes are the
 * char pointer itself. @p object must be an instance of the struct that
 * declared @p field.
 */
int shrewd_basic_reflect_field_get(const void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   void *out_value,
                                   size_t value_size);

/**
 * Write @p value into the field (raw bytes, same size rule and errors as
 * shrewd_basic_reflect_field_get()).
 */
int shrewd_basic_reflect_field_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value,
                                   size_t value_size);

/**
 * Print a type's self-description (the descriptor, not an object)
 * through the "reflect" log module as one INFO record: the header line
 * (name, kind, size, alignment) then one line per field - name, shape
 * (pointer/array markers), offset, size, alignment and flag notes
 * (owned/stringbuf/bits/union/flex). ENUM types list their constants,
 * ARRAY rows their element shape, UNION types their variants. Useful
 * for debugging registrations and generating documentation. Returns
 * SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_MEM_NOMEM or
 * SHREWD_BASIC_ERR_ARG_INVALID.
 */
int shrewd_basic_reflect_describe(const shrewd_basic_reflect_type_t *type);

/** One field's layout report line (shrewd_basic_reflect_layout()). */
typedef struct shrewd_basic_reflect_layout_entry {
    const shrewd_basic_reflect_field_t *field; /**< The field described. */
    size_t offset;                             /**< Byte offset in the type. */
    size_t size;                               /**< Field storage width. */
    size_t align;                              /**< Effective alignment. */
    size_t pad_before; /**< Padding since the previous field's end (0 on overlap). */
} shrewd_basic_reflect_layout_entry_t;

/**
 * Layout callback. Return SHREWD_BASIC_ERR_OK to keep walking; any
 * other code aborts the walk and is returned by
 * shrewd_basic_reflect_layout().
 */
typedef int (*shrewd_basic_reflect_layout_fn)(void *ctx,
                                              const shrewd_basic_reflect_layout_entry_t *entry);

/**
 * Report a STRUCT type's memory layout (pahole-lite): one callback per
 * field in ascending offset order (declaration order on ties), each
 * carrying offset, size, effective alignment and the padding before
 * it. Overlapping bit groups report pad_before 0; trailing padding is
 * type->sb_refl_size minus the last field's end. Nested structs report
 * as one entry - recurse with their own descriptor for detail. Returns
 * SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_ARG_INVALID (NULL arguments or
 * a non-struct type) or the callback's abort code.
 */
int shrewd_basic_reflect_layout(const shrewd_basic_reflect_type_t *type,
                                shrewd_basic_reflect_layout_fn fn,
                                void *ctx);

/**
 * Pretty-print @p object according to @p type through the "reflect" log
 * module as a single log record (one prefix line, then the unprefixed
 * dump body from the next line on; level INFO). @p label may be NULL.
 * @p type must describe a struct (SHREWD_BASIC_REFLECT_KIND_STRUCT); the
 * nested fields are rendered recursively with indentation. Use the
 * shrewd_basic_reflect_dump() macro to record the call site.
 */
int shrewd_basic_reflect_dump_write(const shrewd_basic_reflect_type_t *type,
                                    const void *object,
                                    const char *label,
                                    const char *file,
                                    int line,
                                    const char *func);

/**
 * Pretty-print through shrewd_basic_reflect_dump_write() with the call site
 * captured, so the log record's prefix shows where the dump was requested
 * (same pattern as the shrewd_basic_log() macro).
 */
#define shrewd_basic_reflect_dump(type, object, label)                                             \
    shrewd_basic_reflect_dump_write((type), (object), (label), __FILE__, __LINE__, __func__)

/*
 * Path syntax shared by the three functions below:
 * member ( '[' index ']' )? ( '.' member ( '[' index ']' )? )*
 * with C identifier member names and decimal indices, e.g. "home.x",
 * "pair[2].y". A member takes at most one index (multi-dimension arrays
 * are not described). Paths that cross a pointer segment are followed
 * where the API says so; NULL pointers along the way end the walk.
 */

/**
 * Resolve a value path to its final field (metadata only, no memory
 * access). @p out_offset receives the byte offset of the value inside
 * the root object, @p out_size the size of the designated value - one
 * element's size when the path ends with an index. Pointer-free paths
 * only: crossing a pointer cannot be resolved statically and returns
 * SHREWD_BASIC_ERR_ARG_INVALID (use path_get/path_set). Unknown members
 * or out-of-range indices return SHREWD_BASIC_ERR_KEY_NOTFOUND;
 * malformed paths and NULL arguments return SHREWD_BASIC_ERR_ARG_INVALID.
 */
int shrewd_basic_reflect_type_resolve(const shrewd_basic_reflect_type_t *type,
                                      const char *path,
                                      const shrewd_basic_reflect_field_t **out_field,
                                      size_t *out_offset,
                                      size_t *out_size);

/**
 * Read the value a path designates out of @p object (raw bytes, same
 * size rule as shrewd_basic_reflect_field_get(): @p value_size must equal
 * the resolved value size or the call fails with
 * SHREWD_BASIC_ERR_REFLECT_MISMATCH). Pointer segments are followed; a
 * NULL pointer along the way returns SHREWD_BASIC_ERR_KEY_NOTFOUND. A
 * path ending at a pointer field yields the pointer value itself.
 */
int shrewd_basic_reflect_path_get(const void *object,
                                  const shrewd_basic_reflect_type_t *type,
                                  const char *path,
                                  void *out_value,
                                  size_t value_size);

/**
 * Write @p value where a path designates (raw bytes, same size rule and
 * errors as shrewd_basic_reflect_path_get()). Pointer segments are
 * followed for addressing only; nothing is allocated or freed and a
 * NULL pointer along the way leaves the object untouched
 * (SHREWD_BASIC_ERR_KEY_NOTFOUND).
 */
int shrewd_basic_reflect_path_set(void *object,
                                  const shrewd_basic_reflect_type_t *type,
                                  const char *path,
                                  const void *value,
                                  size_t value_size);

/*
 * Caller-hosted lookup lists: the name->field index lives in the
 * application's own container. The library builds one entry per
 * registered type (lookup_init fills a caller-provided shrewd_basic_reflect_lookup_t)
 * and searches it through the application's ops callbacks - the
 * container's data structure, storage, key choice and locking policy
 * all stay with the caller. The library never synchronizes a list:
 * build it during single-threaded initialization and it is safe for
 * concurrent read-only lookups afterwards. Types missing from the list
 * fall back to the plain linear scan (correct, just not accelerated).
 *
 * The *_list family is the thread-safe path; the no-list interfaces
 * above accelerate through a per-thread cache (see their notes).
 */

/** Inline field-index capacity of one lookup entry; larger structs are
 * registered with sb_refl_count == 0 (no index, linear lookup). */
#define SHREWD_BASIC_REFLECT_LOOKUP_FIELDS 64

/**
 * One registered type: the sorted-by-name index over its field list.
 * Storage belongs to the caller (embed it in your own node if the
 * container needs links); fill it with shrewd_basic_reflect_lookup_init().
 * sb_refl_count == 0 means "no index built" and sb_refl_sorted is
 * meaningless - consult sb_refl_count before searching.
 */
typedef struct shrewd_basic_reflect_lookup {
    const char *sb_refl_name; /**< Registration key; often type->sb_refl_name. */
    const shrewd_basic_reflect_type_t *sb_refl_type; /**< The registered type. */
    size_t sb_refl_count;                            /**< Indexed fields; 0 = no index. */
    const shrewd_basic_reflect_field_t *sb_refl_sorted[SHREWD_BASIC_REFLECT_LOOKUP_FIELDS];
} shrewd_basic_reflect_lookup_t;

/**
 * Container callbacks. All three receive the application's context
 * pointer and return library error codes / NULL like the rest of the
 * API. find_by_type/find_by_name return the entry (its embedded
 * shrewd_basic_reflect_lookup_t) or NULL on miss; insert places @p type
 * into the container and reports where the entry lives via @p out_slot
 * so the library can fill the index in place (return
 * SHREWD_BASIC_ERR_KEY_EXISTS for duplicate keys - the policy is the
 * container's).
 */
typedef struct shrewd_basic_reflect_list_ops {
    int (*insert)(void *ctx,
                  const char *name,
                  const shrewd_basic_reflect_type_t *type,
                  shrewd_basic_reflect_lookup_t **out_slot);
    const shrewd_basic_reflect_lookup_t *(*find_by_type)(void *ctx,
                                                         const shrewd_basic_reflect_type_t *type);
    const shrewd_basic_reflect_lookup_t *(*find_by_name)(void *ctx, const char *name);
} shrewd_basic_reflect_list_ops_t;

/** A caller-hosted list: ops plus the container handle. */
typedef struct shrewd_basic_reflect_list {
    const shrewd_basic_reflect_list_ops_t *sb_refl_ops; /**< Callbacks; never NULL. */
    void *sb_refl_ctx;                                  /**< Application container. */
} shrewd_basic_reflect_list_t;

/**
 * Build the sorted field index of @p type into caller-owned @p look.
 * @p name is the registration key (NULL = type->sb_refl_name; the
 * pointer is stored as-is and must outlive the entry). Non-struct types
 * and types with more than SHREWD_BASIC_REFLECT_LOOKUP_FIELDS fields
 * succeed with sb_refl_count == 0 (no index).
 */
int shrewd_basic_reflect_lookup_init(shrewd_basic_reflect_lookup_t *look,
                                     const shrewd_basic_reflect_type_t *type,
                                     const char *name);

/**
 * Binary-search @p look for @p name (not necessarily NUL-terminated at
 * @p name_len). Returns the field, or NULL when absent or when the
 * entry carries no index (sb_refl_count == 0).
 */
const shrewd_basic_reflect_field_t *shrewd_basic_reflect_lookup_find(
    const shrewd_basic_reflect_lookup_t *look, const char *name, size_t name_len);

/**
 * Register @p type under @p name (NULL = type->sb_refl_name) in @p list:
 * ops->insert places the entry, then the field index is built in place.
 * Propagates the insert callback's status (e.g.
 * SHREWD_BASIC_ERR_KEY_EXISTS for a duplicate key).
 */
int shrewd_basic_reflect_list_register(const shrewd_basic_reflect_list_t *list,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *name);

/**
 * Find a registered type by its registration name (find_field's
 * counterpart for types). SHREWD_BASIC_ERR_KEY_NOTFOUND on miss,
 * SHREWD_BASIC_ERR_ARG_INVALID for NULL arguments or a NULL ops member.
 */
int shrewd_basic_reflect_type_find_list(const shrewd_basic_reflect_list_t *list,
                                        const char *name,
                                        const shrewd_basic_reflect_type_t **out);

/**
 * Find a field by name through @p list (same contract and errors as
 * shrewd_basic_reflect_type_find_field()). Types absent from the list -
 * and entries without an index - are searched linearly.
 */
int shrewd_basic_reflect_type_find_field_list(const shrewd_basic_reflect_list_t *list,
                                              const shrewd_basic_reflect_type_t *type,
                                              const char *name,
                                              const shrewd_basic_reflect_field_t **out);

/**
 * Path APIs through @p list: same contract and errors as their no-list
 * twins, with every by-name segment resolved through the list. NULL
 * @p list is rejected (use the no-list interfaces for the per-thread
 * cache path).
 */
int shrewd_basic_reflect_type_resolve_list(const shrewd_basic_reflect_list_t *list,
                                           const shrewd_basic_reflect_type_t *type,
                                           const char *path,
                                           const shrewd_basic_reflect_field_t **out_field,
                                           size_t *out_offset,
                                           size_t *out_size);

/** Path read through @p list (see shrewd_basic_reflect_type_resolve_list()). */
int shrewd_basic_reflect_path_get_list(const shrewd_basic_reflect_list_t *list,
                                       const void *object,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *path,
                                       void *out_value,
                                       size_t value_size);

/** Path write through @p list (see shrewd_basic_reflect_type_resolve_list()). */
int shrewd_basic_reflect_path_set_list(const shrewd_basic_reflect_list_t *list,
                                       void *object,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *path,
                                       const void *value,
                                       size_t value_size);

/*
 * Ready-made registry container: a fixed-capacity array of lookup
 * entries implementing the three list ops (names kept sorted for
 * binary search, duplicate names rejected with KEY_EXISTS). It is the
 * "type registry by name" sugar over the *_list family - storage stays
 * with the caller (a static array behind the macro), no global state,
 * same threading contract (register single-threaded, query freely).
 * Entry pointers returned by the ops stay valid until the next
 * registration (insertion keeps the array sorted).
 */
typedef struct shrewd_basic_reflect_registry {
    shrewd_basic_reflect_lookup_t *sb_refl_entries; /**< Caller-owned slots. */
    size_t sb_refl_capacity;                        /**< Slots available. */
    size_t sb_refl_count;                           /**< Registered entries. */
} shrewd_basic_reflect_registry_t;

/** Declare a registry with inline entry storage (file scope). */
#define SHREWD_BASIC_REFLECT_REGISTRY_DEFINE(var, capacity)                                        \
    static shrewd_basic_reflect_lookup_t var##_entries[(capacity)];                                \
    static shrewd_basic_reflect_registry_t var = {var##_entries, (size_t)(capacity), 0}

/**
 * Point @p out_list at @p registry through the built-in array ops, so
 * shrewd_basic_reflect_list_register() and the *_list queries work
 * unchanged. Returns SHREWD_BASIC_ERR_ARG_INVALID for NULL arguments.
 */
int shrewd_basic_reflect_registry_bind(shrewd_basic_reflect_registry_t *registry,
                                       shrewd_basic_reflect_list_t *out_list);

/** The ops behind shrewd_basic_reflect_registry_bind() (for manual composition). */
const shrewd_basic_reflect_list_ops_t *shrewd_basic_reflect_registry_ops(void);

/*
 * Operations: the effective vtable of a field is field->sb_refl_ops when set,
 * else field->sb_refl_type->sb_refl_ops. get/set/check run for values and pointees
 * alike; copy/destroy handle objects (pointer targets and the root of
 * copy/destroy) - see the file header for the ownership contract.
 */

/**
 * Read the canonical value of a field's target through ops->get. For a
 * pointer field the pointee is read (a NULL pointer returns
 * SHREWD_BASIC_ERR_KEY_NOTFOUND); for a value field the field's own
 * bytes are read. Fields with arrays, or without ops->get, return
 * SHREWD_BASIC_ERR_ARG_INVALID. @p value_size must equal ops->value_size
 * or the call fails with SHREWD_BASIC_ERR_REFLECT_MISMATCH.
 */
int shrewd_basic_reflect_value_get(const void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   void *out_value,
                                   size_t value_size);

/** Write the canonical value of a field's target through ops->set. */
int shrewd_basic_reflect_value_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value,
                                   size_t value_size);

/**
 * Validate an object graph: every effective ops->check is called and
 * struct values are walked field by field (pointer targets included,
 * NULL pointers skipped). Depth-capped; returns the first non-OK code
 * from a callback, SHREWD_BASIC_ERR_OK otherwise.
 */
int shrewd_basic_reflect_check(const void *object, const shrewd_basic_reflect_type_t *type);

/**
 * Deep-copy an object into @p *out. Root structs are cloned recursively
 * (pointer targets cloned per the ownership contract in the file
 * header); a type with ops->copy delegates the whole clone to it.
 * Returns SHREWD_BASIC_ERR_MEM_NOMEM when an allocation fails.
 */
int shrewd_basic_reflect_copy(void **out,
                              const void *object,
                              const shrewd_basic_reflect_type_t *type);

/**
 * Allocation context for shrewd_basic_reflect_copy_with() and
 * shrewd_basic_reflect_destroy_with(): alloc must return zeroed memory
 * (calloc semantics) and free must release it. The generic copy's
 * bookkeeping nodes go through the same pair, so every byte a
 * copy_with call allocates is released by the matching destroy_with.
 * NULL selects the platform allocator (what the plain copy/destroy
 * use). The context is not stored anywhere - two calls may use
 * different allocators.
 */
typedef struct shrewd_basic_reflect_allocator {
    int (*alloc)(void *ctx, void **out, size_t size);
    void (*free)(void *ctx, void *ptr);
    void *ctx;
} shrewd_basic_reflect_allocator_t;

/** shrewd_basic_reflect_copy() with an injected allocator. */
int shrewd_basic_reflect_copy_with(const shrewd_basic_reflect_allocator_t *alloc,
                                   void **out,
                                   const void *object,
                                   const shrewd_basic_reflect_type_t *type);

/**
 * Destroy an object and everything the generic copy would have cloned -
 * the inverse of shrewd_basic_reflect_copy(). Only run it on graphs that
 * own their pointer targets; borrowed (string) targets and opaque
 * targets without ops->destroy are left untouched.
 */
int shrewd_basic_reflect_destroy(void *object, const shrewd_basic_reflect_type_t *type);

/** shrewd_basic_reflect_destroy() with an injected allocator. */
int shrewd_basic_reflect_destroy_with(const shrewd_basic_reflect_allocator_t *alloc,
                                      void *object,
                                      const shrewd_basic_reflect_type_t *type);

/**
 * Compare two objects of @p type for equality into @p *out_equal. Values
 * compare by their semantics (integers and enums numerically, floats
 * numerically, strings by content, char and bool as themselves);
 * pointer fields compare their targets recursively (NULL rules apply,
 * cycles terminate); effective ops->equal overrides the comparison per
 * node. Opaque targets without ops compare by address identity.
 */
int shrewd_basic_reflect_equal(const void *a,
                               const void *b,
                               const shrewd_basic_reflect_type_t *type,
                               bool *out_equal);

/**
 * Compare like shrewd_basic_reflect_equal() but report where the two
 * graphs first differ. Returns SHREWD_BASIC_ERR_OK when the objects are
 * equal, SHREWD_BASIC_ERR_REFLECT_MISMATCH otherwise - with the first
 * difference's path written into @p path_out (the path_get() syntax,
 * e.g. "home.y" or "pair[2].x"; "(root)" when the roots themselves
 * compare unequal, through ops->equal). @p path_out may be NULL to skip
 * the report; a non-NULL buffer is always NUL-terminated and truncated
 * when too small. An array field whose length differs reports the
 * field itself ("values").
 */
int shrewd_basic_reflect_diff(const void *a,
                              const void *b,
                              const shrewd_basic_reflect_type_t *type,
                              char *path_out,
                              size_t path_out_size);

/** Difference categories reported by shrewd_basic_reflect_diff_all(). */
typedef enum shrewd_basic_reflect_diff_kind {
    SHREWD_BASIC_REFLECT_DIFF_VALUE = 0, /**< A leaf (or ops->equal) mismatch. */
    SHREWD_BASIC_REFLECT_DIFF_SHAPE = 1  /**< NULL-ness, array length or union tag. */
} shrewd_basic_reflect_diff_kind_t;

/**
 * diff_all callback. Return SHREWD_BASIC_ERR_OK to keep walking; any
 * other code aborts the walk and is returned by
 * shrewd_basic_reflect_diff_all().
 */
typedef int (*shrewd_basic_reflect_diff_fn)(void *ctx,
                                            const char *path,
                                            shrewd_basic_reflect_diff_kind_t kind);

/**
 * Compare like shrewd_basic_reflect_diff() but report EVERY difference:
 * each mismatch is delivered to @p fn with the same path syntax
 * ("(root)", "home.y", "pair[2].x"). Semantics mirror the equal walk -
 * bit groups compare their bits, string buffers their text, unions the
 * tag and the active variant, cyclic object pairs are visited once -
 * and the walk continues past each reported difference. Returns
 * SHREWD_BASIC_ERR_OK when the walk completes (equal graphs report
 * nothing), @p fn's abort code, or SHREWD_BASIC_ERR_ARG_INVALID for
 * NULL arguments.
 */
int shrewd_basic_reflect_diff_all(const void *a,
                                  const void *b,
                                  const shrewd_basic_reflect_type_t *type,
                                  shrewd_basic_reflect_diff_fn fn,
                                  void *ctx);

/**
 * Hash a value graph consistently with shrewd_basic_reflect_equal():
 * equal objects always hash equal. Leaves fold into a 64-bit FNV-1a
 * stream through canonical encodings (integers by value, floats with
 * -0.0 folded and NaNs as a fixed marker, strings by content, opaque
 * targets by address) so the result is endianness- and
 * width-independent. Shared and cyclic targets are folded by iterative
 * color refinement, making the hash bisimulation-invariant: a self-loop
 * hashes the same as a two-cycle, a diamond like its shared tail. A
 * depth-capped region folds as a marker on both sides, matching
 * reflect_equal()'s cap, so structures differing only beyond the cap
 * may still collide. Effective ops->hash overrides a node. Returns
 * SHREWD_BASIC_ERR_OK, SHREWD_BASIC_ERR_MEM_NOMEM or
 * SHREWD_BASIC_ERR_ARG_INVALID.
 */
int shrewd_basic_reflect_hash(const void *object,
                              const shrewd_basic_reflect_type_t *type,
                              size_t *out_hash);

/*
 * Traversal: event-by-event walk of a value graph for applications that
 * want the structure without writing their own recursion. Pointers are
 * followed like dump; arrays are bracketed by ARRAY_ENTER/ARRAY_EXIT;
 * cycles and the depth cap are reported as CYCLE events instead of being
 * expanded. The walk allocates nothing.
 */

/** Events reported by shrewd_basic_reflect_visit(). */
typedef enum shrewd_basic_reflect_visit_event {
    SHREWD_BASIC_REFLECT_VISIT_ENTER = 0,   /**< Before a struct value's fields. */
    SHREWD_BASIC_REFLECT_VISIT_ARRAY_ENTER, /**< Before an array's elements. */
    SHREWD_BASIC_REFLECT_VISIT_LEAF,        /**< One scalar/string/opaque value or NULL slot. */
    SHREWD_BASIC_REFLECT_VISIT_CYCLE,       /**< Skipped target (cycle or depth cap). */
    SHREWD_BASIC_REFLECT_VISIT_ARRAY_EXIT,  /**< After an array's elements. */
    SHREWD_BASIC_REFLECT_VISIT_EXIT         /**< After a struct value's fields. */
} shrewd_basic_reflect_visit_event_t;

/** What the walker should do after a visit callback returns. */
typedef enum shrewd_basic_reflect_visit_action {
    SHREWD_BASIC_REFLECT_VISIT_CONTINUE = 0, /**< Keep walking. */
    SHREWD_BASIC_REFLECT_VISIT_PRUNE = 1,    /**< Skip this node's subtree (ENTER/ARRAY_ENTER). */
    SHREWD_BASIC_REFLECT_VISIT_STOP = 2      /**< Abort the walk at once. */
} shrewd_basic_reflect_visit_action_t;

/**
 * One event of shrewd_basic_reflect_visit(). @c path follows the
 * path_get() syntax ("home.x", "pair[2].y", "" for the root).
 * @c field is the member the node was reached through (NULL at the root),
 * @c value points at the node's storage - NULL for a NULL pointer slot -
 * and @c count carries the element count on ARRAY_ENTER/ARRAY_EXIT.
 * A STRING node's storage holds the char pointer itself.
 */
typedef struct shrewd_basic_reflect_visit_node {
    shrewd_basic_reflect_visit_event_t event;
    const char *path;
    const shrewd_basic_reflect_field_t *field;
    const void *value;
    const shrewd_basic_reflect_type_t *type;
    size_t count;
} shrewd_basic_reflect_visit_node_t;

/**
 * Visit callback. Return SHREWD_BASIC_REFLECT_VISIT_CONTINUE to keep
 * walking, ..._PRUNE to skip the subtree (only meaningful on ENTER and
 * ARRAY_ENTER, where it also suppresses the matching EXIT; elsewhere it
 * acts as CONTINUE) or ..._STOP to abort the walk - no further events are
 * reported, not even the pending EXIT ones.
 */
typedef shrewd_basic_reflect_visit_action_t (*shrewd_basic_reflect_visit_fn)(
    void *ctx, const shrewd_basic_reflect_visit_node_t *node);

/**
 * Walk the value graph of @p object (an instance of the struct @p type
 * describes) and report every node to @p visit. Pointers are followed
 * exactly like shrewd_basic_reflect_dump() (NULL slots become LEAF events
 * with a NULL value, opaque targets become LEAF events, cyclic targets
 * become CYCLE events). Returns SHREWD_BASIC_ERR_OK once the walk is
 * done (or stopped by the callback), SHREWD_BASIC_ERR_ARG_INVALID for
 * NULL arguments or a non-struct type.
 */
int shrewd_basic_reflect_visit(const void *object,
                               const shrewd_basic_reflect_type_t *type,
                               shrewd_basic_reflect_visit_fn visit,
                               void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_REFLECT_REFLECT_H */

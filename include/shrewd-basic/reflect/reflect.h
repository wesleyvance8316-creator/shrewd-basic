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
 *       SHREWD_BASIC_REFLECT_FIELD(point_t, x),
 *       SHREWD_BASIC_REFLECT_FIELD(point_t, y));
 *
 * Scalar and string members bind automatically through _Generic
 * (SHREWD_BASIC_REFLECT_FIELD); aggregates bind explicitly:
 * SHREWD_BASIC_REFLECT_FIELD_STRUCT for nested structs and
 * SHREWD_BASIC_REFLECT_FIELD_ARRAY for fixed arrays. Define each type
 * exactly once in one .c of a module and import it in sibling .c files
 * with SHREWD_BASIC_REFLECT_TYPE_DECLARE().
 *
 * Size invariant: all data access goes through field->size (with
 * field->size / field->count as the array stride); type->size is
 * informational only. This keeps access safe when a basic type maps to
 * a descriptor of a different width (e.g. long to int64 on LLP64).
 *
 * Constraints (the macros fail at compile time otherwise):
 *   - members must be basic types, strings (char * / const char *),
 *     fixed single-dimension arrays of those, nested reflected structs,
 *     or pointers described with SHREWD_BASIC_REFLECT_FIELD_PTR;
 *     bit-fields, unions, flexible array members, multi-dimension arrays
 *     and function pointers are not described;
 *   - a char buffer must use SHREWD_BASIC_REFLECT_FIELD_ARRAY(struct_type,
 *     buf, shrewd_basic_reflect_char): plain FIELD would treat it as a
 *     string pointer (arrays decay to pointers in _Generic);
 *   - a type needs at least one field;
 *   - the macros require C11 (_Generic) and expand to declarations, so
 *     they belong at file scope. The C API itself is C++-friendly.
 *
 * Strings: the field bytes hold the char pointer itself; get/set copy
 * the pointer and ownership stays with the caller. All value access is
 * raw memcpy of field->size, never a dereference of offset-derived
 * typed pointers (packed and odd alignments stay well-defined).
 *
 * Pointers: a FIELD_PTR member holds a pointer to its declared target
 * type (shrewd_basic_reflect_void for opaque targets). get/set copy the
 * pointer value - the module never allocates, frees or deep-copies.
 * dump follows the pointer and renders the pointee ((null) for NULL,
 * address-only for opaque targets, depth-capped for self-referential
 * types); a followed pointee scalar is read with the pointee type's
 * canonical width.
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
 * no-op destroy) or must not be destroyed.
 */

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
    SHREWD_BASIC_REFLECT_KIND_FLOAT,
    SHREWD_BASIC_REFLECT_KIND_DOUBLE,
    SHREWD_BASIC_REFLECT_KIND_STRING, /**< char * / const char * member. */
    SHREWD_BASIC_REFLECT_KIND_STRUCT, /**< Aggregate with field descriptors. */
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
    /** Deep-copy @p pointee into a new object stored into @p *out. */
    int (*copy)(void **out, const void *pointee);
    /** Release an object produced by @c copy (or an equivalent owner). */
    void (*destroy)(void *pointee);
};

/** One reflected member of a struct type. */
typedef struct shrewd_basic_reflect_field {
    const char *name;                        /**< Member name, C string literal. */
    const shrewd_basic_reflect_type_t *type; /**< Value type, or array element type. */
    size_t offset;                           /**< offsetof(owner struct, member). */
    size_t size;                             /**< sizeof(member); arrays: total bytes. */
    size_t count;                            /**< Elements; 1 unless fixed array. */
    uint32_t flags;                          /**< SHREWD_BASIC_REFLECT_FIELD_F_*. */
    const shrewd_basic_reflect_ops_t *ops;   /**< Field-level override; NULL = type ops. */
} shrewd_basic_reflect_field_t;

/** A described type: a struct layout, or one primitive/string kind. */
struct shrewd_basic_reflect_type {
    shrewd_basic_reflect_kind_t kind;             /**< STRUCT for aggregates. */
    const char *name;                             /**< "person_t" or "int32". */
    size_t size;                                  /**< Informational width. */
    size_t field_count;                           /**< 0 for primitives. */
    const shrewd_basic_reflect_field_t *fields;    /**< NULL for primitives. */
    const shrewd_basic_reflect_ops_t *ops;         /**< Type-level ops; NULL = defaults. */
};

/** Field flag: the field is a fixed-size array of @c count elements. */
#define SHREWD_BASIC_REFLECT_FIELD_F_ARRAY ((uint32_t)1u)

/** Field flag: the field is a pointer to the field's type (or an array of them). */
#define SHREWD_BASIC_REFLECT_FIELD_F_PTR ((uint32_t)2u)

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
#define SHREWD_BASIC_REFLECT_PRIM_TYPE(expr)                                   \
    _Generic((expr),                                                           \
        _Bool: &shrewd_basic_reflect_bool,                                     \
        char: &shrewd_basic_reflect_char,                                      \
        signed char: &shrewd_basic_reflect_int8,                               \
        unsigned char: &shrewd_basic_reflect_uint8,                            \
        short: &shrewd_basic_reflect_int16,                                    \
        unsigned short: &shrewd_basic_reflect_uint16,                          \
        int: &shrewd_basic_reflect_int32,                                      \
        unsigned int: &shrewd_basic_reflect_uint32,                            \
        long: &shrewd_basic_reflect_int64,                                     \
        unsigned long: &shrewd_basic_reflect_uint64,                           \
        long long: &shrewd_basic_reflect_int64,                                \
        unsigned long long: &shrewd_basic_reflect_uint64,                      \
        float: &shrewd_basic_reflect_float,                                    \
        double: &shrewd_basic_reflect_double,                                  \
        char *: &shrewd_basic_reflect_string,                                  \
        const char *: &shrewd_basic_reflect_string)

/* Describe a scalar or string member; its type is deduced automatically. */
#define SHREWD_BASIC_REFLECT_FIELD(struct_type, member)                        \
    {                                                                          \
        .name = #member,                                                       \
        .type = SHREWD_BASIC_REFLECT_PRIM_TYPE(((struct_type *)0)->member),    \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = (size_t)1,                                                    \
        .flags = (uint32_t)0                                                   \
    }

/* Describe a nested struct member; ref is its descriptor without '&'. */
#define SHREWD_BASIC_REFLECT_FIELD_STRUCT(struct_type, member, ref)            \
    {                                                                          \
        .name = #member,                                                       \
        .type = &(ref),                                                        \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = (size_t)1,                                                    \
        .flags = (uint32_t)0                                                   \
    }

/* Describe a fixed-size array member; ref is the element descriptor. */
#define SHREWD_BASIC_REFLECT_FIELD_ARRAY(struct_type, member, ref)             \
    {                                                                          \
        .name = #member,                                                       \
        .type = &(ref),                                                        \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = sizeof(((struct_type *)0)->member)                            \
                / sizeof(((struct_type *)0)->member[0]),                       \
        .flags = SHREWD_BASIC_REFLECT_FIELD_F_ARRAY                            \
    }

/*
 * Describe a pointer member; ref is the pointee descriptor passed without
 * '&' (shrewd_basic_reflect_void for opaque targets like void *). get/set
 * copy the pointer value; dump follows it. A self-referential type may
 * reference its own descriptor here (the DEFINE macro forward-declares it).
 */
#define SHREWD_BASIC_REFLECT_FIELD_PTR(struct_type, member, ref)               \
    {                                                                          \
        .name = #member,                                                       \
        .type = &(ref),                                                        \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = (size_t)1,                                                    \
        .flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR                              \
    }

/* Describe a fixed-size array of pointers; ref is the pointee descriptor. */
#define SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY(struct_type, member, ref)         \
    {                                                                          \
        .name = #member,                                                       \
        .type = &(ref),                                                        \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = sizeof(((struct_type *)0)->member)                            \
                / sizeof(((struct_type *)0)->member[0]),                       \
        .flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR                              \
                | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY                           \
    }

/*
 * Compile-time type registration: define `var` exactly once, in a single
 * .c of the module; sibling translation units import it with
 * SHREWD_BASIC_REFLECT_TYPE_DECLARE(var). The descriptor object gets
 * external linkage so &var is an address constant usable inside other
 * descriptors (nested and, later, self-referential types); the field
 * table `var##_fields` stays file-local. The trailing ';' belongs to the
 * call, like the log module macros. Macro arguments must not collide
 * with descriptor member names (hence var/struct_type/member/ref).
 */
#define SHREWD_BASIC_REFLECT_TYPE_DEFINE(var, struct_type, ...)                \
    extern const shrewd_basic_reflect_type_t var;                              \
    static const shrewd_basic_reflect_field_t var##_fields[] = { __VA_ARGS__ };\
    const shrewd_basic_reflect_type_t var = {                                  \
        .kind = SHREWD_BASIC_REFLECT_KIND_STRUCT,                              \
        .name = #struct_type,                                                  \
        .size = sizeof(struct_type),                                           \
        .field_count = sizeof(var##_fields) / sizeof(var##_fields[0]),         \
        .fields = var##_fields                                                 \
    }

/** Import a type defined by SHREWD_BASIC_REFLECT_TYPE_DEFINE() elsewhere. */
#define SHREWD_BASIC_REFLECT_TYPE_DECLARE(var)                                 \
    extern const shrewd_basic_reflect_type_t var

/*
 * SHREWD_BASIC_REFLECT_TYPE_DEFINE with a type-level ops vtable; the ops
 * object is referenced without '&'. Macro arguments must not collide with
 * descriptor member names (hence var/struct_type/member/ref/ops_ref).
 */
#define SHREWD_BASIC_REFLECT_TYPE_DEFINE_OPS(var, struct_type, ops_ref, ...)   \
    extern const shrewd_basic_reflect_type_t var;                              \
    static const shrewd_basic_reflect_field_t var##_fields[] = { __VA_ARGS__ };\
    const shrewd_basic_reflect_type_t var = {                                  \
        .kind = SHREWD_BASIC_REFLECT_KIND_STRUCT,                              \
        .name = #struct_type,                                                  \
        .size = sizeof(struct_type),                                           \
        .field_count = sizeof(var##_fields) / sizeof(var##_fields[0]),         \
        .fields = var##_fields,                                                \
        .ops = &(ops_ref)                                                      \
    }

/*
 * Describe an opaque target type with its ops vtable (one definition per
 * type, shared across translation units with TYPE_DECLARE). Use
 * shrewd_basic_reflect_void for opaque targets without ops.
 */
#define SHREWD_BASIC_REFLECT_OPAQUE_DEFINE(var, type_name, ops_ref)            \
    extern const shrewd_basic_reflect_type_t var;                              \
    const shrewd_basic_reflect_type_t var = {                                  \
        .kind = SHREWD_BASIC_REFLECT_KIND_OPAQUE,                              \
        .name = (type_name),                                                   \
        .size = 0,                                                             \
        .field_count = 0,                                                      \
        .fields = NULL,                                                        \
        .ops = &(ops_ref)                                                      \
    }

/* FIELD_STRUCT with a field-level ops override (takes precedence over type ops). */
#define SHREWD_BASIC_REFLECT_FIELD_STRUCT_OPS(struct_type, member, ref, ops_ref) \
    {                                                                          \
        .name = #member,                                                       \
        .type = &(ref),                                                        \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = (size_t)1,                                                    \
        .flags = (uint32_t)0,                                                  \
        .ops = &(ops_ref)                                                      \
    }

/* FIELD_PTR with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_PTR_OPS(struct_type, member, ref, ops_ref)  \
    {                                                                          \
        .name = #member,                                                       \
        .type = &(ref),                                                        \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = (size_t)1,                                                    \
        .flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR,                             \
        .ops = &(ops_ref)                                                      \
    }

/* FIELD_PTR_ARRAY with a field-level ops override. */
#define SHREWD_BASIC_REFLECT_FIELD_PTR_ARRAY_OPS(struct_type, member, ref, ops_ref) \
    {                                                                          \
        .name = #member,                                                       \
        .type = &(ref),                                                        \
        .offset = offsetof(struct_type, member),                               \
        .size = sizeof(((struct_type *)0)->member),                            \
        .count = sizeof(((struct_type *)0)->member)                            \
                / sizeof(((struct_type *)0)->member[0]),                       \
        .flags = SHREWD_BASIC_REFLECT_FIELD_F_PTR                              \
                | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY,                          \
        .ops = &(ops_ref)                                                      \
    }

/** Name of a kind, e.g. "int32"; unknown values map to "unknown". */
const char *shrewd_basic_reflect_kind_name(shrewd_basic_reflect_kind_t kind);

/**
 * Find a field by name (flat, single level).
 * Returns SHREWD_BASIC_ERR_OK and stores a pointer into type->fields
 * (static storage) into @p *out, SHREWD_BASIC_ERR_KEY_NOTFOUND when no
 * field matches, SHREWD_BASIC_ERR_ARG_INVALID for NULL arguments.
 */
int shrewd_basic_reflect_type_find_field(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         const shrewd_basic_reflect_field_t **out);

/**
 * Copy the field's raw bytes out of @p object into @p out_value.
 * @p value_size must equal field->size or the call fails with
 * SHREWD_BASIC_ERR_REFLECT_MISMATCH. For string fields the bytes are the
 * char pointer itself. @p object must be an instance of the struct that
 * declared @p field.
 */
int shrewd_basic_reflect_field_get(const void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   void *out_value, size_t value_size);

/**
 * Write @p value into the field (raw bytes, same size rule and errors as
 * shrewd_basic_reflect_field_get()).
 */
int shrewd_basic_reflect_field_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value, size_t value_size);

/**
 * Pretty-print @p object according to @p type through the "reflect" log
 * module (one log line per output line, level INFO). @p label may be NULL.
 * @p type must describe a struct (SHREWD_BASIC_REFLECT_KIND_STRUCT); the
 * nested fields are rendered recursively with indentation.
 */
int shrewd_basic_reflect_dump(const shrewd_basic_reflect_type_t *type,
                              const void *object, const char *label);

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
                                      size_t *out_offset, size_t *out_size);

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
                                  const char *path, void *out_value, size_t value_size);

/**
 * Write @p value where a path designates (raw bytes, same size rule and
 * errors as shrewd_basic_reflect_path_get()). Pointer segments are
 * followed for addressing only; nothing is allocated or freed and a
 * NULL pointer along the way leaves the object untouched
 * (SHREWD_BASIC_ERR_KEY_NOTFOUND).
 */
int shrewd_basic_reflect_path_set(void *object,
                                  const shrewd_basic_reflect_type_t *type,
                                  const char *path, const void *value, size_t value_size);

/*
 * Operations: the effective vtable of a field is field->ops when set,
 * else field->type->ops. get/set/check run for values and pointees
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
                                   void *out_value, size_t value_size);

/** Write the canonical value of a field's target through ops->set. */
int shrewd_basic_reflect_value_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value, size_t value_size);

/**
 * Validate an object graph: every effective ops->check is called and
 * struct values are walked field by field (pointer targets included,
 * NULL pointers skipped). Depth-capped; returns the first non-OK code
 * from a callback, SHREWD_BASIC_ERR_OK otherwise.
 */
int shrewd_basic_reflect_check(const void *object,
                               const shrewd_basic_reflect_type_t *type);

/**
 * Deep-copy an object into @p *out. Root structs are cloned recursively
 * (pointer targets cloned per the ownership contract in the file
 * header); a type with ops->copy delegates the whole clone to it.
 * Returns SHREWD_BASIC_ERR_MEM_NOMEM when an allocation fails.
 */
int shrewd_basic_reflect_copy(void **out, const void *object,
                              const shrewd_basic_reflect_type_t *type);

/**
 * Destroy an object and everything the generic copy would have cloned -
 * the inverse of shrewd_basic_reflect_copy(). Only run it on graphs that
 * own their pointer targets; borrowed (string) targets and opaque
 * targets without ops->destroy are left untouched.
 */
int shrewd_basic_reflect_destroy(void *object,
                                 const shrewd_basic_reflect_type_t *type);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_REFLECT_REFLECT_H */

#include "shrewd-basic/reflect/reflect.h"

#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/ds/rbtree.h"
#include "shrewd-basic/platform/memory.h"

/*
 * Shared log module of the reflect module (imported by reflect_dump.c
 * with SHREWD_BASIC_LOG_MODULE_DECLARE).
 *
 * Logging policy:
 *   WARN  - invalid arguments (caller misuse)
 *   TRACE - field_get/field_set results and find_field misses
 * Successful find_field stays silent (pure query); dump output itself is
 * the product of an explicit call and is emitted at INFO by reflect_dump.c.
 */
SHREWD_BASIC_LOG_MODULE_DEFINE(shrewd_basic_reflect_log, "reflect",
                               SHREWD_BASIC_LOG_LEVEL_ALL);

/* Primitive descriptors: canonical widths; type->sb_refl_size is informational. */
const shrewd_basic_reflect_type_t shrewd_basic_reflect_bool = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_BOOL, .sb_refl_name = "bool",
    .sb_refl_size = sizeof(_Bool),
    .sb_refl_align = _Alignof(_Bool),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_char = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_CHAR, .sb_refl_name = "char",
    .sb_refl_size = sizeof(char),
    .sb_refl_align = _Alignof(char),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int8 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT8, .sb_refl_name = "int8",
    .sb_refl_size = sizeof(int8_t),
    .sb_refl_align = _Alignof(int8_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint8 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT8, .sb_refl_name = "uint8",
    .sb_refl_size = sizeof(uint8_t),
    .sb_refl_align = _Alignof(uint8_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int16 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT16, .sb_refl_name = "int16",
    .sb_refl_size = sizeof(int16_t),
    .sb_refl_align = _Alignof(int16_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint16 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT16, .sb_refl_name = "uint16",
    .sb_refl_size = sizeof(uint16_t),
    .sb_refl_align = _Alignof(uint16_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int32 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT32, .sb_refl_name = "int32",
    .sb_refl_size = sizeof(int32_t),
    .sb_refl_align = _Alignof(int32_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint32 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT32, .sb_refl_name = "uint32",
    .sb_refl_size = sizeof(uint32_t),
    .sb_refl_align = _Alignof(uint32_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int64 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT64, .sb_refl_name = "int64",
    .sb_refl_size = sizeof(int64_t),
    .sb_refl_align = _Alignof(int64_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint64 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT64, .sb_refl_name = "uint64",
    .sb_refl_size = sizeof(uint64_t),
    .sb_refl_align = _Alignof(uint64_t),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_float = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_FLOAT, .sb_refl_name = "float",
    .sb_refl_size = sizeof(float),
    .sb_refl_align = _Alignof(float),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_double = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_DOUBLE, .sb_refl_name = "double",
    .sb_refl_size = sizeof(double),
    .sb_refl_align = _Alignof(double),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_string = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_STRING, .sb_refl_name = "string",
    .sb_refl_size = sizeof(const char *),
    .sb_refl_align = _Alignof(const char *),
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_void = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_OPAQUE, .sb_refl_name = "void",
    .sb_refl_size = 0, .sb_refl_align = 0,
    .sb_refl_abi = SHREWD_BASIC_REFLECT_ABI,
    .sb_refl_field_count = 0, .sb_refl_fields = NULL };

const char *shrewd_basic_reflect_kind_name(shrewd_basic_reflect_kind_t kind)
{
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return "bool";
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
        return "char";
    case SHREWD_BASIC_REFLECT_KIND_INT8:
        return "int8";
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
        return "uint8";
    case SHREWD_BASIC_REFLECT_KIND_INT16:
        return "int16";
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
        return "uint16";
    case SHREWD_BASIC_REFLECT_KIND_INT32:
        return "int32";
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
        return "uint32";
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        return "int64";
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        return "uint64";
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        return "enum";
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
        return "float";
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        return "double";
    case SHREWD_BASIC_REFLECT_KIND_STRING:
        return "string";
    case SHREWD_BASIC_REFLECT_KIND_STRUCT:
        return "struct";
    case SHREWD_BASIC_REFLECT_KIND_ARRAY:
        return "array";
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        return "opaque";
    default:
        return "unknown";
    }
}

size_t shrewd_basic_reflect_type_field_count(const shrewd_basic_reflect_type_t *type)
{
    return (NULL != type) ? type->sb_refl_field_count : 0;
}

const shrewd_basic_reflect_field_t *shrewd_basic_reflect_type_field_at(
        const shrewd_basic_reflect_type_t *type, size_t index)
{
    if ((NULL == type) || (index >= type->sb_refl_field_count)) {
        return NULL;
    }
    return &type->sb_refl_fields[index];
}

size_t shrewd_basic_reflect_enum_count(const shrewd_basic_reflect_type_t *type)
{
    if ((NULL == type) || (SHREWD_BASIC_REFLECT_KIND_ENUM != type->sb_refl_kind)) {
        return 0;
    }
    return type->sb_refl_enum_count;
}

const shrewd_basic_reflect_enum_entry_t *shrewd_basic_reflect_enum_at(
        const shrewd_basic_reflect_type_t *type, size_t index)
{
    if ((NULL == type) || (SHREWD_BASIC_REFLECT_KIND_ENUM != type->sb_refl_kind)
            || (index >= type->sb_refl_enum_count)) {
        return NULL;
    }
    return &type->sb_refl_enum[index];
}

int shrewd_basic_reflect_enum_find_name(const shrewd_basic_reflect_type_t *type,
                                        int64_t value, const char **out_name)
{
    if ((NULL == type) || (NULL == out_name)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "enum_find_name: type or out_name is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_ENUM != type->sb_refl_kind)
            || (NULL == type->sb_refl_enum)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "enum_find_name: type \"%s\" is not an enum\n",
                         type->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
        if (type->sb_refl_enum[i].sb_refl_value == value) {
            *out_name = type->sb_refl_enum[i].sb_refl_name;
            return SHREWD_BASIC_ERR_OK;
        }
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "enum_find_name: no constant for %lld\n",
                     (long long)value);
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
}

int shrewd_basic_reflect_enum_find_value(const shrewd_basic_reflect_type_t *type,
                                         const char *name, int64_t *out_value)
{
    if ((NULL == type) || (NULL == name) || (NULL == out_value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "enum_find_value: type, name or out_value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_ENUM != type->sb_refl_kind)
            || (NULL == type->sb_refl_enum)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "enum_find_value: type \"%s\" is not an enum\n",
                         type->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
        if (0 == strcmp(type->sb_refl_enum[i].sb_refl_name, name)) {
            *out_value = type->sb_refl_enum[i].sb_refl_value;
            return SHREWD_BASIC_ERR_OK;
        }
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "enum_find_value: no constant \"%s\"\n", name);
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
}

/*
 * find_field acceleration for the no-list interfaces: a per-thread
 * side table of sorted field indexes, keyed by type pointer.
 * Descriptors are const and never freed, so an index never goes stale.
 * The index is built lazily on the first lookup of a type; types with
 * more fields than one slot holds, or once every slot is busy and one
 * is recycled (round-robin), fall back to the plain linear scan - which
 * also rebuilds the index. Thread-local storage keeps concurrent
 * lookups race-free without locking (each thread warms its own cache);
 * applications that want one shared index instead build a caller-hosted
 * list (shrewd_basic_reflect_list_register + the *_list interfaces).
 *
 * Build-time knob: -DSHREWD_BASIC_REFLECT_LOOKUP_SLOTS=n sets the per-
 * thread capacity; 0 disables the cache entirely (every lookup is a
 * plain linear scan).
 */
#ifndef SHREWD_BASIC_REFLECT_LOOKUP_SLOTS
#define SHREWD_BASIC_REFLECT_LOOKUP_SLOTS 8
#endif
#define REFLECT_LOOKUP_SLOTS SHREWD_BASIC_REFLECT_LOOKUP_SLOTS
#define REFLECT_LOOKUP_FIELDS 64

#if REFLECT_LOOKUP_SLOTS > 0

typedef struct reflect_lookup {
    const shrewd_basic_reflect_type_t *type;
    size_t count;
    const shrewd_basic_reflect_field_t *sorted[REFLECT_LOOKUP_FIELDS];
} reflect_lookup_t;

static _Thread_local reflect_lookup_t g_lookup[REFLECT_LOOKUP_SLOTS];
static _Thread_local size_t g_lookup_next = 0;

/* Order two names when the left one is not NUL-terminated. */
static int reflect_name_cmp(const char *name, size_t name_len,
                            const char *field_name)
{
    size_t field_len = strlen(field_name);
    size_t shared = (name_len < field_len) ? name_len : field_len;
    int result = memcmp(name, field_name, shared);
    if (0 != result) {
        return result;
    }
    return (name_len < field_len) ? -1 : ((name_len > field_len) ? 1 : 0);
}

/* Build the sorted index of @p type (no-op when unsuitable or present). */
static void reflect_lookup_build(const shrewd_basic_reflect_type_t *type)
{
    reflect_lookup_t *slot = NULL;
    if ((SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind)
            || (type->sb_refl_field_count > REFLECT_LOOKUP_FIELDS)) {
        return;
    }
    for (size_t i = 0; i < REFLECT_LOOKUP_SLOTS; ++i) {
        if (type == g_lookup[i].type) {
            return;
        }
        if ((NULL == g_lookup[i].type) && (NULL == slot)) {
            slot = &g_lookup[i];
        }
    }
    if (NULL == slot) {
        slot = &g_lookup[g_lookup_next];
        g_lookup_next = (g_lookup_next + 1) % REFLECT_LOOKUP_SLOTS;
    }
    slot->type = type;
    slot->count = type->sb_refl_field_count;
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        size_t j = i;
        while ((0 < j) && (0 > strcmp(field->sb_refl_name,
                                      slot->sorted[j - 1]->sb_refl_name))) {
            slot->sorted[j] = slot->sorted[j - 1];
            --j;
        }
        slot->sorted[j] = field;
    }
}

/*
 * Query the sorted index: 1 in *out_indexed when @p type has one, with
 * *out_field the hit (NULL when the name is absent). 0 means "not
 * indexed" and *out_field is untouched.
 */
static int reflect_lookup_query(const shrewd_basic_reflect_type_t *type,
                                const char *name, size_t name_len,
                                const shrewd_basic_reflect_field_t **out_field)
{
    for (size_t i = 0; i < REFLECT_LOOKUP_SLOTS; ++i) {
        const reflect_lookup_t *slot = &g_lookup[i];
        if (type != slot->type) {
            continue;
        }
        size_t low = 0;
        size_t high = slot->count;
        *out_field = NULL;
        while (low < high) {
            size_t mid = low + ((high - low) / 2);
            int result = reflect_name_cmp(name, name_len,
                                          slot->sorted[mid]->sb_refl_name);
            if (0 == result) {
                *out_field = slot->sorted[mid];
                break;
            }
            if (0 > result) {
                high = mid;
            } else {
                low = mid + 1;
            }
        }
        return 1;
    }
    return 0;
}

#else /* cache disabled: every lookup is a plain linear scan */

static void reflect_lookup_build(const shrewd_basic_reflect_type_t *type)
{
    (void)type;
}

static int reflect_lookup_query(const shrewd_basic_reflect_type_t *type,
                                const char *name, size_t name_len,
                                const shrewd_basic_reflect_field_t **out_field)
{
    (void)type;
    (void)name;
    (void)name_len;
    (void)out_field;
    return 0;
}

#endif /* REFLECT_LOOKUP_SLOTS */

/*
 * Linear find over a bare field list (a union variant's fields, which
 * have no type-level index).
 */
static const shrewd_basic_reflect_field_t *reflect_fields_find(
        const shrewd_basic_reflect_field_t *fields, size_t count,
        const char *name, size_t name_len)
{
    for (size_t i = 0; i < count; ++i) {
        const char *candidate = fields[i].sb_refl_name;
        if ((strlen(candidate) == name_len)
                && (0 == memcmp(candidate, name, name_len))) {
            return &fields[i];
        }
    }
    return NULL;
}

/*
 * Find a field by name (not necessarily NUL-terminated at @p name_len).
 * With @p list the lookup goes through the caller-hosted entry
 * (never touching the per-thread cache); without it the sorted index
 * accelerates the search - built either way so the next lookup is cheap.
 */
static const shrewd_basic_reflect_field_t *reflect_field_find(
        const shrewd_basic_reflect_list_t *list,
        const shrewd_basic_reflect_type_t *type, const char *name, size_t name_len)
{
    const shrewd_basic_reflect_field_t *field = NULL;
    if (NULL != list) {
        const shrewd_basic_reflect_lookup_t *look = NULL;
        if ((NULL != list->sb_refl_ops)
                && (NULL != list->sb_refl_ops->find_by_type)) {
            look = list->sb_refl_ops->find_by_type(list->sb_refl_ctx, type);
        }
        if ((NULL != look) && (0 < look->sb_refl_count)) {
            return shrewd_basic_reflect_lookup_find(look, name, name_len);
        }
        return reflect_fields_find(type->sb_refl_fields,
                                   type->sb_refl_field_count, name, name_len);
    }
    if (0 != reflect_lookup_query(type, name, name_len, &field)) {
        return field;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *candidate = &type->sb_refl_fields[i];
        if ((strlen(candidate->sb_refl_name) == name_len)
                && (0 == memcmp(candidate->sb_refl_name, name, name_len))) {
            reflect_lookup_build(type);
            return candidate;
        }
    }
    reflect_lookup_build(type);
    return NULL;
}

int shrewd_basic_reflect_type_find_field(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         const shrewd_basic_reflect_field_t **out)
{
    if ((NULL == type) || (NULL == name) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "find_field: type, name or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_reflect_field_t *field =
            reflect_field_find(NULL, type, name, strlen(name));
    if (NULL != field) {
        *out = field;
        return SHREWD_BASIC_ERR_OK;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "find_field: no field \"%s\"\n", name);
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
}

size_t shrewd_basic_reflect_field_align(const shrewd_basic_reflect_field_t *field)
{
    if (NULL == field) {
        return 0;
    }
    if (0u != (field->sb_refl_flags
               & (SHREWD_BASIC_REFLECT_FIELD_F_PTR
                  | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
        return _Alignof(void *);
    }
    return (NULL != field->sb_refl_type) ? field->sb_refl_type->sb_refl_align : 0;
}

int shrewd_basic_reflect_check_type(const shrewd_basic_reflect_type_t *type)
{
    if (NULL == type) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_ABI != type->sb_refl_abi) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "check", "check_type: \"%s\" has abi %zu, want %zu\n",
                         (NULL != type->sb_refl_name) ? type->sb_refl_name : "?",
                         type->sb_refl_abi, SHREWD_BASIC_REFLECT_ABI);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_STRUCT: {
        if ((0 == type->sb_refl_field_count)
                != (NULL == type->sb_refl_fields)) {
            goto broken;
        }
        for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
            const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
            if ((NULL == field->sb_refl_name) || (NULL == field->sb_refl_type)
                    || (0 == field->sb_refl_stride)) {
                goto broken;
            }
            if ((field->sb_refl_offset + field->sb_refl_size)
                    > type->sb_refl_size) {
                goto broken;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
                if ((field->sb_refl_bit_offset + field->sb_refl_bit_width)
                        > (field->sb_refl_size * 8u)) {
                    goto broken;
                }
            }
        }
        break;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        if ((0 != type->sb_refl_enum_count) && (NULL == type->sb_refl_enum)) {
            goto broken;
        }
        break;
    case SHREWD_BASIC_REFLECT_KIND_ARRAY:
        if ((NULL == type->sb_refl_elem) || (0 == type->sb_refl_elem_count)
                || (0 == type->sb_refl_elem_stride)) {
            goto broken;
        }
        break;
    case SHREWD_BASIC_REFLECT_KIND_UNION:
        if ((0 != type->sb_refl_variant_count)
                && (NULL == type->sb_refl_variants)) {
            goto broken;
        }
        for (size_t i = 0; i < type->sb_refl_variant_count; ++i) {
            const shrewd_basic_reflect_variant_t *variant =
                    &type->sb_refl_variants[i];
            if ((NULL == variant->sb_refl_name)
                    || (0 == variant->sb_refl_field_count)
                    || (NULL == variant->sb_refl_fields)) {
                goto broken;
            }
        }
        break;
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_STRING:
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        break;
    default:
        goto broken;
    }
    return SHREWD_BASIC_ERR_OK;

broken:
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                     "check", "check_type: \"%s\" breaks a descriptor invariant\n",
                     (NULL != type->sb_refl_name) ? type->sb_refl_name : "?");
    return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
}

int shrewd_basic_reflect_field_get(const void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   void *out_value, size_t value_size)
{
    if ((NULL == object) || (NULL == field) || (NULL == out_value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "field_get: object, field or out_value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    size_t bytes = field->sb_refl_size;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
        reflect_array_view_t view = reflect_array_view((const unsigned char *)object, field);
        bytes = view.count * view.stride;
    }
    if (value_size != bytes) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "field_get: size mismatch (%zu != %zu)\n",
                         value_size, bytes);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(out_value, (const unsigned char *)object + field->sb_refl_offset, bytes);
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        /* Bit group: exchange the extracted value, zero-extended. */
        uint64_t unit = reflect_load_u64(out_value, bytes);
        uint64_t value = reflect_bits_extract(unit, field->sb_refl_bit_offset,
                                              field->sb_refl_bit_width);
        reflect_store_u64(out_value, bytes, value);
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "field_get ok (%s, %zu bytes)\n",
                     field->sb_refl_name, bytes);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_field_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value, size_t value_size)
{
    if ((NULL == object) || (NULL == field) || (NULL == value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "field_set: object, field or value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    size_t bytes = field->sb_refl_size;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
        reflect_array_view_t view = reflect_array_view((const unsigned char *)object, field);
        bytes = view.count * view.stride;
    }
    if (value_size != bytes) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "field_set: size mismatch (%zu != %zu)\n",
                         value_size, bytes);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        /* Bit group: read-modify-write the unit. */
        uint64_t unit = reflect_load_u64((const unsigned char *)object
                                         + field->sb_refl_offset, bytes);
        reflect_bits_insert(&unit, field->sb_refl_bit_offset,
                            field->sb_refl_bit_width,
                            reflect_load_u64((const unsigned char *)value, bytes));
        reflect_store_u64((unsigned char *)(uintptr_t)object + field->sb_refl_offset,
                          bytes, unit);
    } else {
        memcpy((unsigned char *)object + field->sb_refl_offset, value, bytes);
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "field_set ok (%s, %zu bytes)\n",
                     field->sb_refl_name, bytes);
    return SHREWD_BASIC_ERR_OK;
}

/*
 * Walk a dotted/indexed path over an object: member ( '[' index ']' )?
 * ( '.' member ( '[' index ']' )? )*. With @p follow the walk
 * dereferences pointer segments (a NULL pointer ends the walk with
 * KEY_NOTFOUND); without it crossing a pointer is rejected, since such a
 * path has no static offset. *out_at may be NULL to only compute the
 * offset. *out_size receives the designated value's size: the whole
 * field, or one element when the path ends with an index.
 */
static int reflect_path_walk(const shrewd_basic_reflect_list_t *list,
                             const unsigned char *object,
                             const shrewd_basic_reflect_type_t *type,
                             const char *path, int follow,
                             const unsigned char **out_at, size_t *out_offset,
                             const shrewd_basic_reflect_field_t **out_field,
                             size_t *out_size)
{
    const unsigned char *base = object;
    size_t offset = 0;
    const shrewd_basic_reflect_type_t *cur = type;
    const shrewd_basic_reflect_field_t *fields = type->sb_refl_fields;
    size_t field_count = type->sb_refl_field_count;
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t size = 0;
    const char *p = path;

    for (;;) {
        if (('A' > *p || 'Z' < *p) && ('a' > *p || 'z' < *p) && ('_' != *p)) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        const char *name = p;
        while (('a' <= *p && 'z' >= *p) || ('A' <= *p && 'Z' >= *p)
                || ('0' <= *p && '9' >= *p) || ('_' == *p)) {
            ++p;
        }
        size_t name_len = (size_t)(p - name);
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT == cur->sb_refl_kind) {
            field = reflect_field_find(list, cur, name, name_len);
        } else if (SHREWD_BASIC_REFLECT_KIND_UNION == cur->sb_refl_kind) {
            /* Inside a union: the active variant's field list. */
            field = reflect_fields_find(fields, field_count, name, name_len);
        } else {
            field = NULL;
        }
        if (NULL == field) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        offset += field->sb_refl_offset;
        size = field->sb_refl_size;

        int indexed = 0;
        if ('[' == *p) {
            ++p;
            if (('0' > *p) || ('9' < *p)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            size_t index = 0;
            while (('0' <= *p) && ('9' >= *p)) {
                if (index <= (SIZE_MAX / 10)) {
                    index = (index * 10) + (size_t)(*p - '0');
                }
                ++p;
            }
            if (']' != *p) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            ++p;
            if (0u == (field->sb_refl_flags
                       & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                          | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                /* Elements live behind a run-time table: only the dynamic
                   walk can index them (same rule as pointer segments). */
                if (0 == follow) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                reflect_array_view_t view = reflect_array_view(base, field);
                if (index >= view.count) {
                    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
                }
                base = view.base;
                offset = index * view.stride;
                size = view.stride;
            } else {
                size_t stride = field->sb_refl_stride;
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
                    /* Flexible tail: bound is run-time. */
                    if (0 != follow) {
                        reflect_array_view_t view = reflect_array_view(base, field);
                        if (index >= view.count) {
                            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
                        }
                    }
                } else if (index >= field->sb_refl_count) {
                    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
                }
                offset += index * stride;
                size = stride;
            }
            indexed = 1;
        }

        /*
         * Chained indices through fixed array value types (multi-dimension
         * arrays): "m[1][2]" consumes one index per row level, statically
         * (each level's bound is compile-time metadata).
         */
        const shrewd_basic_reflect_type_t *row = field->sb_refl_type;
        while ((SHREWD_BASIC_REFLECT_KIND_ARRAY == row->sb_refl_kind)
                && ('[' == *p)) {
            ++p;
            if (('0' > *p) || ('9' < *p)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            size_t index = 0;
            while (('0' <= *p) && ('9' >= *p)) {
                if (index <= (SIZE_MAX / 10)) {
                    index = (index * 10) + (size_t)(*p - '0');
                }
                ++p;
            }
            if (']' != *p) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            ++p;
            if (index >= row->sb_refl_elem_count) {
                return SHREWD_BASIC_ERR_KEY_NOTFOUND;
            }
            offset += index * row->sb_refl_elem_stride;
            size = row->sb_refl_elem_stride;
            row = row->sb_refl_elem;
        }

        if ('.' == *p) {
            ++p;
            if ((0u != (field->sb_refl_flags
                        & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                           | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)))
                    && (0 == indexed)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
                /* Union: the active variant is a run-time choice (the
                   tag is data), so resolve's static walk refuses it. */
                const shrewd_basic_reflect_variant_t *variant = NULL;
                if (0 == follow) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                variant = reflect_union_variant(field, base + offset);
                if (NULL == variant) {
                    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
                }
                fields = variant->sb_refl_fields;
                field_count = variant->sb_refl_field_count;
                cur = field->sb_refl_type;
                continue;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                if (0 == follow) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                void *pointer = NULL;
                memcpy(&pointer, base + offset, sizeof(pointer));
                if (NULL == pointer) {
                    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
                }
                base = (const unsigned char *)pointer;
                offset = 0;
            }
            cur = row;
            fields = (SHREWD_BASIC_REFLECT_KIND_STRUCT == row->sb_refl_kind)
                    ? row->sb_refl_fields : NULL;
            field_count = (SHREWD_BASIC_REFLECT_KIND_STRUCT == row->sb_refl_kind)
                    ? row->sb_refl_field_count : 0;
            continue;
        }
        if ('\0' != *p) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        if (NULL != out_at) {
            *out_at = base + offset;
        }
        if (NULL != out_offset) {
            *out_offset = offset;
        }
        *out_field = field;
        *out_size = size;
        return SHREWD_BASIC_ERR_OK;
    }
}

static void reflect_path_log(int status, const char *op, const char *path,
                             size_t size)
{
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "%s ok (%s, %zu bytes)\n", op, path, size);
    } else if (SHREWD_BASIC_ERR_KEY_NOTFOUND == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "%s: no path \"%s\"\n", op, path);
    } else {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "%s: bad path \"%s\"\n", op, path);
    }
}

int shrewd_basic_reflect_type_resolve(const shrewd_basic_reflect_type_t *type,
                                      const char *path,
                                      const shrewd_basic_reflect_field_t **out_field,
                                      size_t *out_offset, size_t *out_size)
{
    if ((NULL == type) || (NULL == path) || (NULL == out_field)
            || (NULL == out_offset) || (NULL == out_size)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "resolve: type, path or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    size_t size = 0;
    int status = reflect_path_walk(NULL, NULL, type, path, 0, NULL, out_offset,
                                   out_field, &size);
    if (SHREWD_BASIC_ERR_OK == status) {
        *out_size = size;
    } else {
        reflect_path_log(status, "resolve", path, 0);
    }
    return status;
}

int shrewd_basic_reflect_path_get(const void *object,
                                  const shrewd_basic_reflect_type_t *type,
                                  const char *path, void *out_value, size_t value_size)
{
    if ((NULL == object) || (NULL == type) || (NULL == path) || (NULL == out_value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "path_get: object, type, path or out_value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const unsigned char *at = NULL;
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t size = 0;
    int status = reflect_path_walk(NULL, (const unsigned char *)object, type,
                                   path, 1, &at, NULL, &field, &size);
    if (SHREWD_BASIC_ERR_OK != status) {
        reflect_path_log(status, "path_get", path, 0);
        return status;
    }
    if (value_size != size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "path_get: size mismatch (%zu != %zu)\n",
                         value_size, size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(out_value, at, size);
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        uint64_t unit = reflect_load_u64(out_value, size);
        uint64_t value = reflect_bits_extract(unit, field->sb_refl_bit_offset,
                                              field->sb_refl_bit_width);
        reflect_store_u64(out_value, size, value);
    }
    reflect_path_log(SHREWD_BASIC_ERR_OK, "path_get", path, size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_path_set(void *object,
                                  const shrewd_basic_reflect_type_t *type,
                                  const char *path, const void *value, size_t value_size)
{
    if ((NULL == object) || (NULL == type) || (NULL == path) || (NULL == value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "path_set: object, type, path or value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const unsigned char *at = NULL;
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t size = 0;
    int status = reflect_path_walk(NULL, (const unsigned char *)object, type,
                                   path, 1, &at, NULL, &field, &size);
    if (SHREWD_BASIC_ERR_OK != status) {
        reflect_path_log(status, "path_set", path, 0);
        return status;
    }
    if (value_size != size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "path_set: size mismatch (%zu != %zu)\n",
                         value_size, size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        uint64_t unit = reflect_load_u64(at, size);
        reflect_bits_insert(&unit, field->sb_refl_bit_offset,
                            field->sb_refl_bit_width,
                            reflect_load_u64((const unsigned char *)value, size));
        reflect_store_u64((unsigned char *)(uintptr_t)at, size, unit);
    } else {
        memcpy((unsigned char *)(uintptr_t)at, value, size);
    }
    reflect_path_log(SHREWD_BASIC_ERR_OK, "path_set", path, size);
    return SHREWD_BASIC_ERR_OK;
}

/*
 * Caller-hosted lookup lists: the *_list family resolves every by-name
 * segment through the application's container (never touching the
 * per-thread cache) and falls back to the plain linear scan for types
 * the container does not hold. The library does no locking; a list is
 * as thread-safe as the container behind its ops.
 */

int shrewd_basic_reflect_type_find_list(const shrewd_basic_reflect_list_t *list,
                                        const char *name,
                                        const shrewd_basic_reflect_type_t **out)
{
    const shrewd_basic_reflect_lookup_t *look = NULL;
    if ((NULL == list) || (NULL == list->sb_refl_ops)
            || (NULL == list->sb_refl_ops->find_by_name) || (NULL == name)
            || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "type_find_list: list, name or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    look = list->sb_refl_ops->find_by_name(list->sb_refl_ctx, name);
    if ((NULL == look) || (NULL == look->sb_refl_type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "type_find_list: no type \"%s\"\n", name);
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = look->sb_refl_type;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_type_find_field_list(
        const shrewd_basic_reflect_list_t *list,
        const shrewd_basic_reflect_type_t *type, const char *name,
        const shrewd_basic_reflect_field_t **out)
{
    const shrewd_basic_reflect_field_t *field = NULL;
    if ((NULL == list) || (NULL == list->sb_refl_ops) || (NULL == type)
            || (NULL == name) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "find_field_list: list, type, name or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    field = reflect_field_find(list, type, name, strlen(name));
    if (NULL == field) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "find_field_list: no field \"%s\"\n", name);
        return SHREWD_BASIC_ERR_KEY_NOTFOUND;
    }
    *out = field;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_type_resolve_list(const shrewd_basic_reflect_list_t *list,
                                           const shrewd_basic_reflect_type_t *type,
                                           const char *path,
                                           const shrewd_basic_reflect_field_t **out_field,
                                           size_t *out_offset, size_t *out_size)
{
    if ((NULL == list) || (NULL == list->sb_refl_ops) || (NULL == type)
            || (NULL == path) || (NULL == out_field) || (NULL == out_offset)
            || (NULL == out_size)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "resolve_list: list, type, path or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    size_t size = 0;
    int status = reflect_path_walk(list, NULL, type, path, 0, NULL, out_offset,
                                   out_field, &size);
    if (SHREWD_BASIC_ERR_OK == status) {
        *out_size = size;
    } else {
        reflect_path_log(status, "resolve_list", path, 0);
    }
    return status;
}

int shrewd_basic_reflect_path_get_list(const shrewd_basic_reflect_list_t *list,
                                       const void *object,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *path, void *out_value,
                                       size_t value_size)
{
    if ((NULL == list) || (NULL == list->sb_refl_ops) || (NULL == object)
            || (NULL == type) || (NULL == path) || (NULL == out_value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "path_get_list: list, object, type, path or out_value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const unsigned char *at = NULL;
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t size = 0;
    int status = reflect_path_walk(list, (const unsigned char *)object, type,
                                   path, 1, &at, NULL, &field, &size);
    if (SHREWD_BASIC_ERR_OK != status) {
        reflect_path_log(status, "path_get_list", path, 0);
        return status;
    }
    if (value_size != size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "path_get_list: size mismatch (%zu != %zu)\n",
                         value_size, size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(out_value, at, size);
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        uint64_t unit = reflect_load_u64(out_value, size);
        uint64_t value = reflect_bits_extract(unit, field->sb_refl_bit_offset,
                                              field->sb_refl_bit_width);
        reflect_store_u64(out_value, size, value);
    }
    reflect_path_log(SHREWD_BASIC_ERR_OK, "path_get_list", path, size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_path_set_list(const shrewd_basic_reflect_list_t *list,
                                       void *object,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *path, const void *value,
                                       size_t value_size)
{
    if ((NULL == list) || (NULL == list->sb_refl_ops) || (NULL == object)
            || (NULL == type) || (NULL == path) || (NULL == value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "path_set_list: list, object, type, path or value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const unsigned char *at = NULL;
    const shrewd_basic_reflect_field_t *field = NULL;
    size_t size = 0;
    int status = reflect_path_walk(list, (const unsigned char *)object, type,
                                   path, 1, &at, NULL, &field, &size);
    if (SHREWD_BASIC_ERR_OK != status) {
        reflect_path_log(status, "path_set_list", path, 0);
        return status;
    }
    if (value_size != size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "path_set_list: size mismatch (%zu != %zu)\n",
                         value_size, size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        uint64_t unit = reflect_load_u64(at, size);
        reflect_bits_insert(&unit, field->sb_refl_bit_offset,
                            field->sb_refl_bit_width,
                            reflect_load_u64((const unsigned char *)value, size));
        reflect_store_u64((unsigned char *)(uintptr_t)at, size, unit);
    } else {
        memcpy((unsigned char *)(uintptr_t)at, value, size);
    }
    reflect_path_log(SHREWD_BASIC_ERR_OK, "path_set_list", path, size);
    return SHREWD_BASIC_ERR_OK;
}


/* Defensive recursion cap for the graph walkers. */
#define REFLECT_MAX_DEPTH 16

/* Target address of a single-value field (deref one pointer level). */
static int reflect_field_target(const void *object,
                                const shrewd_basic_reflect_field_t *field,
                                const unsigned char **out_at)
{
    const unsigned char *at = (const unsigned char *)object + field->sb_refl_offset;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        void *pointer = NULL;
        memcpy(&pointer, at, sizeof(pointer));
        if (NULL == pointer) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                             "field", "value: field \"%s\" is NULL\n", field->sb_refl_name);
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        at = (const unsigned char *)pointer;
    }
    *out_at = at;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_value_get(const void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   void *out_value, size_t value_size)
{
    if ((NULL == object) || (NULL == field) || (NULL == out_value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_get: object, field or out_value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_reflect_ops_t *ops = reflect_field_ops(field);
    if ((NULL == ops) || (NULL == ops->get)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_get: field \"%s\" has no ops->get\n",
                         field->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_get: field \"%s\" is an array\n",
                         field->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (value_size != ops->value_size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "value_get: size mismatch (%zu != %zu)\n",
                         value_size, ops->value_size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    const unsigned char *at = NULL;
    int status = reflect_field_target(object, field, &at);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = ops->get(at, out_value, value_size);
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "value_get ok (%s, %zu bytes)\n",
                         field->sb_refl_name, value_size);
    }
    return status;
}

int shrewd_basic_reflect_value_set(void *object,
                                   const shrewd_basic_reflect_field_t *field,
                                   const void *value, size_t value_size)
{
    if ((NULL == object) || (NULL == field) || (NULL == value)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_set: object, field or value is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    const shrewd_basic_reflect_ops_t *ops = reflect_field_ops(field);
    if ((NULL == ops) || (NULL == ops->set)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_set: field \"%s\" has no ops->set\n",
                         field->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_set: field \"%s\" is an array\n",
                         field->sb_refl_name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (value_size != ops->value_size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "value_set: size mismatch (%zu != %zu)\n",
                         value_size, ops->value_size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    const unsigned char *at = NULL;
    int status = reflect_field_target(object, field, &at);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = ops->set((unsigned char *)(uintptr_t)at, value, value_size);
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "value_set ok (%s, %zu bytes)\n",
                         field->sb_refl_name, value_size);
    }
    return status;
}

/* --- graph walkers: check / copy / destroy ---------------------------- */

/*
 * Graph tracking (rbtree-backed, address-ordered): a visited set records
 * pointer targets already processed so cycles and shared nodes are
 * handled exactly once, and a clone map records src -> clone so shared
 * sources stay shared and back-edges find their clone. Value nesting is
 * statically finite and needs no tracking.
 */
typedef struct reflect_pair {
    const void *src;
    void *dst;
    const shrewd_basic_reflect_ops_t *ops; /* set for ops->copy clones */
} reflect_pair_t;

static int reflect_cmp_addr(const void *a, const void *b, void *ctx)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    (void)ctx;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

static int reflect_cmp_pair(const void *a, const void *b, void *ctx)
{
    uintptr_t x = (uintptr_t)((const reflect_pair_t *)a)->src;
    uintptr_t y = (uintptr_t)((const reflect_pair_t *)b)->src;
    (void)ctx;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

/* 1 in *out_seen when @p pointer was visited before; records it otherwise. */
static int reflect_visited_check(shrewd_basic_rbtree_t *visited,
                                 const void *pointer, int *out_seen)
{
    void *found = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_rbtree_search(visited, pointer, &found)) {
        *out_seen = 1;
        return SHREWD_BASIC_ERR_OK;
    }
    *out_seen = 0;
    return shrewd_basic_rbtree_insert(visited, (void *)(uintptr_t)pointer);
}

/* 1 in *out_found with *out_dst when @p src already has a clone. */
static int reflect_map_lookup(shrewd_basic_rbtree_t *map, const void *src,
                              void **out_dst, int *out_found)
{
    reflect_pair_t probe;
    void *found = NULL;
    probe.src = src;
    probe.dst = NULL;
    if (SHREWD_BASIC_ERR_OK == shrewd_basic_rbtree_search(map, &probe, &found)) {
        *out_dst = ((const reflect_pair_t *)found)->dst;
        *out_found = 1;
    } else {
        *out_found = 0;
    }
    return SHREWD_BASIC_ERR_OK;
}

static int reflect_map_insert(shrewd_basic_rbtree_t *map, const void *src, void *dst,
                              const shrewd_basic_reflect_ops_t *ops)
{
    reflect_pair_t *pair = NULL;
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, sizeof(*pair));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    pair = block;
    pair->src = src;
    pair->dst = dst;
    pair->ops = ops;
    status = shrewd_basic_rbtree_insert(map, pair);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_platform_free(&block);
    }
    return status;
}

/*
 * Release the map: with @p free_dst every recorded clone is destroyed as
 * well (ops clones through ops->destroy) - the cleanup path of a failed
 * copy, where the map is the authoritative list of what was created.
 */
static void reflect_map_release(shrewd_basic_rbtree_t *map, int free_dst)
{
    if (NULL == map) {
        return;
    }
    for (;;) {
        void *data = NULL;
        reflect_pair_t *pair = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_rbtree_min(map, &data)) {
            break;
        }
        pair = data;
        if (0 != free_dst) {
            if (NULL != pair->ops) {
                if (NULL != pair->ops->destroy) {
                    pair->ops->destroy(pair->dst);
                }
            } else {
                (void)shrewd_basic_platform_free(&pair->dst);
            }
        }
        void *removed = NULL;
        (void)shrewd_basic_rbtree_remove(map, pair, &removed);
        (void)shrewd_basic_platform_free((void **)&pair);
    }
    (void)shrewd_basic_rbtree_destroy(map);
}

static int reflect_check_node(const shrewd_basic_reflect_type_t *type, const void *value,
                              const shrewd_basic_reflect_ops_t *ops,
                              shrewd_basic_rbtree_t *visited, size_t depth);

/* Check one field list - a struct's fields or an active union variant's. */
static int reflect_check_fields(const shrewd_basic_reflect_field_t *fields, size_t count,
                                const unsigned char *base,
                                shrewd_basic_rbtree_t *visited, size_t depth)
{
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        reflect_array_view_t view = reflect_array_view(base, field);
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            /* Only the active variant takes part. */
            const shrewd_basic_reflect_variant_t *variant =
                    reflect_union_variant(field, base + field->sb_refl_offset);
            if (NULL != variant) {
                int status = reflect_check_fields(variant->sb_refl_fields,
                                                  variant->sb_refl_field_count,
                                                  base + field->sb_refl_offset,
                                                  visited, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            continue;
        }
        for (size_t j = 0; j < view.count; ++j) {
            const unsigned char *elem = view.base + (j * view.stride);
            const void *target = elem;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = NULL;
                int seen = 0;
                memcpy(&pointer, elem, sizeof(pointer));
                if (NULL == pointer) {
                    continue;
                }
                int status = reflect_visited_check(visited, pointer, &seen);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                if (0 != seen) {
                    continue;
                }
                target = pointer;
            }
            int status = reflect_check_node(field->sb_refl_type, target, field_ops,
                                            visited, depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static int reflect_check_node(const shrewd_basic_reflect_type_t *type, const void *value,
                              const shrewd_basic_reflect_ops_t *ops,
                              shrewd_basic_rbtree_t *visited, size_t depth)
{
    if (REFLECT_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->check)) {
        return ops->check(value);
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            int status = reflect_check_node(
                    type->sb_refl_elem,
                    (const unsigned char *)value + (i * type->sb_refl_elem_stride),
                    type->sb_refl_elem->sb_refl_ops, visited, depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    return reflect_check_fields(type->sb_refl_fields, type->sb_refl_field_count,
                                (const unsigned char *)value, visited, depth + 1);
}

int shrewd_basic_reflect_check(const void *object,
                               const shrewd_basic_reflect_type_t *type)
{
    if ((NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "check: object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_rbtree_t *visited = NULL;
    int status = shrewd_basic_rbtree_create(reflect_cmp_addr, NULL, &visited);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "field", "check: allocation failed\n");
        return status;
    }
    int seen = 0;
    status = reflect_visited_check(visited, object, &seen);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = reflect_check_node(type, object, type->sb_refl_ops, visited, 0);
    }
    (void)shrewd_basic_rbtree_destroy(visited);
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "check ok (%s)\n", type->sb_refl_name);
    } else {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "check failed (%s, %s)\n", type->sb_refl_name,
                         shrewd_basic_error_name(status));
    }
    return status;
}

static int reflect_destroy_value(const shrewd_basic_reflect_type_t *type, void *value,
                                 shrewd_basic_rbtree_t *visited, size_t depth);
static int reflect_copy_value(const shrewd_basic_reflect_type_t *type,
                              const unsigned char *src, unsigned char *dst,
                              shrewd_basic_rbtree_t *map, size_t depth);

/*
 * Clone an object (a pointee, or the root of copy): ops->copy handles
 * opaque/complex targets, struct and scalar targets are cloned (shared
 * sources map to shared clones, so cycles and diamonds stay sound),
 * string targets stay borrowed (aliased) and past the depth cap pointers
 * are aliased defensively.
 */
static int reflect_clone_target(const shrewd_basic_reflect_type_t *type, const void *src,
                                const shrewd_basic_reflect_ops_t *ops,
                                void **out, shrewd_basic_rbtree_t *map, size_t depth)
{
    int found = 0;
    void *existing = NULL;
    int status = reflect_map_lookup(map, src, &existing, &found);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 != found) {
        *out = existing;
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->copy)) {
        status = ops->copy(out, src);
        if (SHREWD_BASIC_ERR_OK == status) {
            /* Tracked so failure cleanup can call ops->destroy; the
               insert is best-effort (a lost record would only leak the
               clone of a copy that is being discarded anyway). */
            (void)reflect_map_insert(map, src, *out, ops);
        }
        return status;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
        *out = (void *)(uintptr_t)src;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_OPAQUE == type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
    }
    if (REFLECT_MAX_DEPTH <= depth) {
        /* Defensive: past the cap the pointer is aliased. Such aliases
           sit beyond the destroy walker's cap as well, so they are never
           freed - the clone graph and its destruction stay consistent. */
        *out = (void *)(uintptr_t)src;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        void *block = NULL;
        status = shrewd_basic_platform_calloc(&block, 1,
                                              reflect_object_size((const unsigned char *)src, type));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        /* Register before recursing so back-edges find the clone; after
           registration the map owns cleanup on failure. */
        status = reflect_map_insert(map, src, block, NULL);
        if (SHREWD_BASIC_ERR_OK != status) {
            (void)shrewd_basic_platform_free(&block);
            return status;
        }
        status = reflect_copy_value(type, (const unsigned char *)src,
                                    (unsigned char *)block, map, depth);
        if (SHREWD_BASIC_ERR_OK == status) {
            *out = block;
        }
        return status;
    }
    void *block = NULL;
    status = shrewd_basic_platform_alloc(&block, type->sb_refl_size);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(block, src, type->sb_refl_size);
    status = reflect_map_insert(map, src, block, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_platform_free(&block);
        return status;
    }
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

/* Copy one field list - a struct's fields or an active union variant's. */
static int reflect_copy_fields(const shrewd_basic_reflect_field_t *fields, size_t count,
                               const unsigned char *src, unsigned char *dst,
                               shrewd_basic_rbtree_t *map, size_t depth)
{
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        unsigned char *dst_at = dst + field->sb_refl_offset;
        reflect_array_view_t view = reflect_array_view(src, field);
        unsigned char *dst_base = dst_at;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            /*
             * Union: copy the raw storage first (unowned bytes stay
             * byte-identical), then clone the active variant's fields in
             * place. Inactive slots may hold stale pointers that are
             * never followed - destroy only walks the active variant.
             */
            const shrewd_basic_reflect_variant_t *variant =
                    reflect_union_variant(field, src + field->sb_refl_offset);
            memcpy(dst_at, src + field->sb_refl_offset, field->sb_refl_stride);
            if (NULL != variant) {
                int status = reflect_copy_fields(
                        variant->sb_refl_fields, variant->sb_refl_field_count,
                        src + field->sb_refl_offset, dst_at, map, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
            /*
             * Dynamic array: the table itself is a heap block and joins
             * the clone map, so tables shared between fields stay shared
             * and the failure cleanup of copy knows about them.
             */
            if (NULL == view.base) {
                continue;
            }
            void *existing = NULL;
            int found = 0;
            int status = reflect_map_lookup(map, view.base, &existing, &found);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (0 != found) {
                memcpy(dst_at, &existing, sizeof(existing));
                continue;
            }
            size_t bytes = view.count * view.stride;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL)) {
                bytes += view.stride; /* keep the NULL terminator slot */
            }
            void *table = NULL;
            status = shrewd_basic_platform_calloc(&table, 1, bytes);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            status = reflect_map_insert(map, view.base, table, NULL);
            if (SHREWD_BASIC_ERR_OK != status) {
                (void)shrewd_basic_platform_free(&table);
                return status;
            }
            memcpy(dst_at, &table, sizeof(table));
            dst_base = (unsigned char *)table;
            /* Keep the clone self-consistent even when the count member
               is not described as a field of its own. */
            if (0 != field->sb_refl_limit_size) {
                uint64_t bound = (uint64_t)view.count;
                memcpy(dst + field->sb_refl_limit_offset, &bound,
                       field->sb_refl_limit_size);
            }
        }
        for (size_t j = 0; j < view.count; ++j) {
            const unsigned char *src_elem = view.base + (j * view.stride);
            unsigned char *dst_elem = dst_base + (j * view.stride);
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = NULL;
                memcpy(&pointer, src_elem, sizeof(pointer));
                if (NULL == pointer) {
                    continue;
                }
                void *clone = NULL;
                int status = reflect_clone_target(field->sb_refl_type, pointer,
                                                  reflect_field_ops(field),
                                                  &clone, map, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                memcpy(dst_elem, &clone, sizeof(clone));
            } else if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind)
                       || (SHREWD_BASIC_REFLECT_KIND_ARRAY
                           == field->sb_refl_type->sb_refl_kind)) {
                int status = reflect_copy_value(field->sb_refl_type, src_elem,
                                                dst_elem, map, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            } else if ((SHREWD_BASIC_REFLECT_KIND_STRING
                        == field->sb_refl_type->sb_refl_kind)
                       && (0u != (field->sb_refl_flags
                                  & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED))) {
                /*
                 * Owned string: clone the content through the clone map,
                 * so strings shared between fields stay shared. NULL
                 * stays NULL (the clone storage is zero-initialized).
                 */
                const char *text = reflect_load_str(src_elem);
                if (NULL == text) {
                    continue;
                }
                void *existing = NULL;
                int found = 0;
                int status = reflect_map_lookup(map, text, &existing, &found);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                if (0 != found) {
                    memcpy(dst_elem, &existing, sizeof(existing));
                    continue;
                }
                void *copy = NULL;
                size_t length = strlen(text) + 1;
                status = shrewd_basic_platform_alloc(&copy, length);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                memcpy(copy, text, length);
                status = reflect_map_insert(map, text, copy, NULL);
                if (SHREWD_BASIC_ERR_OK != status) {
                    (void)shrewd_basic_platform_free(&copy);
                    return status;
                }
                memcpy(dst_elem, &copy, sizeof(copy));
            } else {
                memcpy(dst_elem, src_elem, view.stride);
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Copy a value's contents into existing dst storage (zero-initialized). */
static int reflect_copy_value(const shrewd_basic_reflect_type_t *type,
                              const unsigned char *src, unsigned char *dst,
                              shrewd_basic_rbtree_t *map, size_t depth)
{
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        /* A fixed array value (a row): element-wise like a field array. */
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            const unsigned char *src_elem =
                    src + (i * type->sb_refl_elem_stride);
            unsigned char *dst_elem = dst + (i * type->sb_refl_elem_stride);
            if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_elem->sb_refl_kind)
                    || (SHREWD_BASIC_REFLECT_KIND_ARRAY
                        == type->sb_refl_elem->sb_refl_kind)) {
                int status = reflect_copy_value(type->sb_refl_elem, src_elem,
                                                dst_elem, map, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            } else {
                memcpy(dst_elem, src_elem, type->sb_refl_elem_stride);
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        memcpy(dst, src, type->sb_refl_size);
        return SHREWD_BASIC_ERR_OK;
    }
    return reflect_copy_fields(type->sb_refl_fields, type->sb_refl_field_count,
                               src, dst, map, depth);
}

int shrewd_basic_reflect_copy(void **out, const void *object,
                              const shrewd_basic_reflect_type_t *type)
{
    if ((NULL == out) || (NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "copy: out, object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    shrewd_basic_rbtree_t *map = NULL;
    int status = shrewd_basic_rbtree_create(reflect_cmp_pair, NULL, &map);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "field", "copy: allocation failed\n");
        return status;
    }
    if ((NULL != type->sb_refl_ops) && (NULL != type->sb_refl_ops->copy)) {
        status = type->sb_refl_ops->copy(out, object);
        reflect_map_release(map, (SHREWD_BASIC_ERR_OK != status) ? 1 : 0);
        if (SHREWD_BASIC_ERR_OK == status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                             "field", "copy ok (%s)\n", type->sb_refl_name);
        }
        return status;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "copy: type \"%s\" needs ops->copy\n", type->sb_refl_name);
        (void)shrewd_basic_rbtree_destroy(map);
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
    }
    void *block = NULL;
    status = shrewd_basic_platform_calloc(&block, 1,
                                          reflect_object_size((const unsigned char *)object, type));
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_rbtree_destroy(map);
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "field", "copy: allocation failed\n");
        return status;
    }
    status = reflect_map_insert(map, object, block, NULL);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = reflect_copy_value(type, (const unsigned char *)object,
                                    (unsigned char *)block, map, 0);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        /* The map is the authoritative record of every clone made;
           releasing it frees the partial graph (ops clones included). */
        reflect_map_release(map, 1);
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "copy: failed (%s)\n",
                         shrewd_basic_error_name(status));
        return status;
    }
    reflect_map_release(map, 0);
    *out = block;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "copy ok (%s)\n", type->sb_refl_name);
    return SHREWD_BASIC_ERR_OK;
}

/*
 * Destroy an object: ops->destroy handles complex targets, struct
 * targets are released recursively plus freed, scalar targets are freed,
 * string targets stay borrowed and opaque targets without ops are not
 * ours to free. Past the depth cap targets were aliased, so nothing is
 * released.
 */
static int reflect_destroy_target(const shrewd_basic_reflect_type_t *type, void *pointee,
                                  const shrewd_basic_reflect_ops_t *ops,
                                  shrewd_basic_rbtree_t *visited, size_t depth)
{
    if (REFLECT_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_OK;
    }
    int seen = 0;
    int status = reflect_visited_check(visited, pointee, &seen);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 != seen) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->destroy)) {
        ops->destroy(pointee);
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        status = reflect_destroy_value(type, pointee, visited, depth);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        return shrewd_basic_platform_free(&pointee);
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_OPAQUE == type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    return shrewd_basic_platform_free(&pointee);
}

/* Release one field list's targets - a struct's or a union variant's. */
static int reflect_destroy_fields(const shrewd_basic_reflect_field_t *fields, size_t count,
                                  void *value, shrewd_basic_rbtree_t *visited, size_t depth)
{
    unsigned char *base = (unsigned char *)value;
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        unsigned char *at = base + field->sb_refl_offset;
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        reflect_array_view_t view = reflect_array_view(value, field);
        unsigned char *table = NULL;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            /* Only the active variant's targets are ours to release. */
            const shrewd_basic_reflect_variant_t *variant =
                    reflect_union_variant(field, at);
            if (NULL != variant) {
                int status = reflect_destroy_fields(variant->sb_refl_fields,
                                                    variant->sb_refl_field_count,
                                                    at, visited, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
            if (NULL == view.base) {
                continue;
            }
            /* The table is a heap block like any pointee: freed once. */
            int seen = 0;
            int status = reflect_visited_check(visited, view.base, &seen);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (0 != seen) {
                continue;
            }
            table = (unsigned char *)(uintptr_t)view.base;
        }
        for (size_t j = 0; j < view.count; ++j) {
            unsigned char *elem = table ? (table + (j * view.stride))
                                       : (at + (j * view.stride));
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = NULL;
                memcpy(&pointer, elem, sizeof(pointer));
                if (NULL == pointer) {
                    continue;
                }
                int status = reflect_destroy_target(field->sb_refl_type, pointer,
                                                    field_ops, visited, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            } else if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind)
                       || (SHREWD_BASIC_REFLECT_KIND_ARRAY
                           == field->sb_refl_type->sb_refl_kind)) {
                int status = reflect_destroy_value(field->sb_refl_type, elem,
                                                   visited, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            } else if ((SHREWD_BASIC_REFLECT_KIND_STRING
                        == field->sb_refl_type->sb_refl_kind)
                       && (0u != (field->sb_refl_flags
                                  & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED))) {
                /* Owned string: freed once (shared strings are visited once). */
                void *target = reflect_load_ptr(elem);
                if (NULL != target) {
                    int seen = 0;
                    int status = reflect_visited_check(visited, target, &seen);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        return status;
                    }
                    if (0 == seen) {
                        status = shrewd_basic_platform_free(&target);
                        if (SHREWD_BASIC_ERR_OK != status) {
                            return status;
                        }
                    }
                }
            }
        }
        if (NULL != table) {
            int status = shrewd_basic_platform_free((void **)&table);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Release the pointer targets of a value; the value storage is untouched. */
static int reflect_destroy_value(const shrewd_basic_reflect_type_t *type, void *value,
                                 shrewd_basic_rbtree_t *visited, size_t depth)
{
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            if ((SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_elem->sb_refl_kind)
                    && (SHREWD_BASIC_REFLECT_KIND_ARRAY
                        != type->sb_refl_elem->sb_refl_kind)) {
                break;
            }
            int status = reflect_destroy_value(
                    type->sb_refl_elem,
                    (unsigned char *)value + (i * type->sb_refl_elem_stride),
                    visited, depth + 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    return reflect_destroy_fields(type->sb_refl_fields, type->sb_refl_field_count,
                                  value, visited, depth);
}

int shrewd_basic_reflect_destroy(void *object,
                                 const shrewd_basic_reflect_type_t *type)
{
    if ((NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "destroy: object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((NULL != type->sb_refl_ops) && (NULL != type->sb_refl_ops->destroy)) {
        type->sb_refl_ops->destroy(object);
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "destroy ok (%s)\n", type->sb_refl_name);
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "destroy: type \"%s\" needs ops->destroy\n",
                         type->sb_refl_name);
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
    }
    shrewd_basic_rbtree_t *visited = NULL;
    int status = shrewd_basic_rbtree_create(reflect_cmp_addr, NULL, &visited);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "field", "destroy: allocation failed\n");
        return status;
    }
    /* Mark the root first: cycles back to it must not free it twice. */
    int seen = 0;
    status = reflect_visited_check(visited, object, &seen);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = reflect_destroy_value(type, object, visited, 0);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_platform_free(&object);
    }
    (void)shrewd_basic_rbtree_destroy(visited);
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "destroy ok (%s)\n", type->sb_refl_name);
    } else {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "field", "destroy: free failed\n");
    }
    return status;
}

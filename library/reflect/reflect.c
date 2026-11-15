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
    .sb_refl_size = sizeof(_Bool), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_char = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_CHAR, .sb_refl_name = "char",
    .sb_refl_size = sizeof(char), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int8 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT8, .sb_refl_name = "int8",
    .sb_refl_size = sizeof(int8_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint8 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT8, .sb_refl_name = "uint8",
    .sb_refl_size = sizeof(uint8_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int16 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT16, .sb_refl_name = "int16",
    .sb_refl_size = sizeof(int16_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint16 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT16, .sb_refl_name = "uint16",
    .sb_refl_size = sizeof(uint16_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int32 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT32, .sb_refl_name = "int32",
    .sb_refl_size = sizeof(int32_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint32 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT32, .sb_refl_name = "uint32",
    .sb_refl_size = sizeof(uint32_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int64 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT64, .sb_refl_name = "int64",
    .sb_refl_size = sizeof(int64_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint64 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_UINT64, .sb_refl_name = "uint64",
    .sb_refl_size = sizeof(uint64_t), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_float = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_FLOAT, .sb_refl_name = "float",
    .sb_refl_size = sizeof(float), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_double = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_DOUBLE, .sb_refl_name = "double",
    .sb_refl_size = sizeof(double), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_string = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_STRING, .sb_refl_name = "string",
    .sb_refl_size = sizeof(const char *), .sb_refl_field_count = 0, .sb_refl_fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_void = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_OPAQUE, .sb_refl_name = "void",
    .sb_refl_size = 0, .sb_refl_field_count = 0, .sb_refl_fields = NULL };

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

int shrewd_basic_reflect_type_find_field(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         const shrewd_basic_reflect_field_t **out)
{
    if ((NULL == type) || (NULL == name) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "find_field: type, name or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        if (0 == strcmp(type->sb_refl_fields[i].sb_refl_name, name)) {
            *out = &type->sb_refl_fields[i];
            return SHREWD_BASIC_ERR_OK;
        }
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "find_field: no field \"%s\"\n", name);
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
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
    if (value_size != field->sb_refl_size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "field_get: size mismatch (%zu != %zu)\n",
                         value_size, field->sb_refl_size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(out_value, (const unsigned char *)object + field->sb_refl_offset, field->sb_refl_size);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "field_get ok (%s, %zu bytes)\n",
                     field->sb_refl_name, field->sb_refl_size);
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
    if (value_size != field->sb_refl_size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "field_set: size mismatch (%zu != %zu)\n",
                         value_size, field->sb_refl_size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy((unsigned char *)object + field->sb_refl_offset, value, field->sb_refl_size);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "field_set ok (%s, %zu bytes)\n",
                     field->sb_refl_name, field->sb_refl_size);
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
static int reflect_path_walk(const unsigned char *object,
                             const shrewd_basic_reflect_type_t *type,
                             const char *path, int follow,
                             const unsigned char **out_at, size_t *out_offset,
                             const shrewd_basic_reflect_field_t **out_field,
                             size_t *out_size)
{
    const unsigned char *base = object;
    size_t offset = 0;
    const shrewd_basic_reflect_type_t *cur = type;
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
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT != cur->sb_refl_kind) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        field = NULL;
        for (size_t i = 0; i < cur->sb_refl_field_count; ++i) {
            const char *candidate = cur->sb_refl_fields[i].sb_refl_name;
            if ((strlen(candidate) == name_len)
                    && (0 == memcmp(candidate, name, name_len))) {
                field = &cur->sb_refl_fields[i];
                break;
            }
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
            if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            if (index >= field->sb_refl_count) {
                return SHREWD_BASIC_ERR_KEY_NOTFOUND;
            }
            offset += index * (field->sb_refl_size / field->sb_refl_count);
            size = field->sb_refl_size / field->sb_refl_count;
            indexed = 1;
        }

        if ('.' == *p) {
            ++p;
            if ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY))
                    && (0 == indexed)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
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
            cur = field->sb_refl_type;
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
    int status = reflect_path_walk(NULL, type, path, 0, NULL, out_offset,
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
    int status = reflect_path_walk((const unsigned char *)object, type, path, 1,
                                   &at, NULL, &field, &size);
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
    int status = reflect_path_walk((const unsigned char *)object, type, path, 1,
                                   &at, NULL, &field, &size);
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
    memcpy((unsigned char *)(uintptr_t)at, value, size);
    reflect_path_log(SHREWD_BASIC_ERR_OK, "path_set", path, size);
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
                              shrewd_basic_rbtree_t *visited, size_t depth)
{
    if (REFLECT_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->check)) {
        return ops->check(value);
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    const unsigned char *base = (const unsigned char *)value;
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const unsigned char *at = base + field->sb_refl_offset;
        size_t stride = field->sb_refl_size / field->sb_refl_count;
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        for (size_t j = 0; j < field->sb_refl_count; ++j) {
            const unsigned char *elem = at + (j * stride);
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
        status = shrewd_basic_platform_calloc(&block, 1, type->sb_refl_size);
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

/* Copy a value's contents into existing dst storage (zero-initialized). */
static int reflect_copy_value(const shrewd_basic_reflect_type_t *type,
                              const unsigned char *src, unsigned char *dst,
                              shrewd_basic_rbtree_t *map, size_t depth)
{
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        memcpy(dst, src, type->sb_refl_size);
        return SHREWD_BASIC_ERR_OK;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const unsigned char *src_at = src + field->sb_refl_offset;
        unsigned char *dst_at = dst + field->sb_refl_offset;
        size_t stride = field->sb_refl_size / field->sb_refl_count;
        for (size_t j = 0; j < field->sb_refl_count; ++j) {
            const unsigned char *src_elem = src_at + (j * stride);
            unsigned char *dst_elem = dst_at + (j * stride);
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
            } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
                int status = reflect_copy_value(field->sb_refl_type, src_elem, dst_elem, map, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            } else {
                memcpy(dst_elem, src_elem, stride);
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
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
    status = shrewd_basic_platform_calloc(&block, 1, type->sb_refl_size);
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

/* Release the pointer targets of a value; the value storage is untouched. */
static int reflect_destroy_value(const shrewd_basic_reflect_type_t *type, void *value,
                                 shrewd_basic_rbtree_t *visited, size_t depth)
{
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    unsigned char *base = (unsigned char *)value;
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        unsigned char *at = base + field->sb_refl_offset;
        size_t stride = field->sb_refl_size / field->sb_refl_count;
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        for (size_t j = 0; j < field->sb_refl_count; ++j) {
            unsigned char *elem = at + (j * stride);
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
            } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
                int status = reflect_destroy_value(field->sb_refl_type, elem,
                                                   visited, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
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

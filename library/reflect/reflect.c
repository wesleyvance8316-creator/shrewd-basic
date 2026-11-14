#include "shrewd-basic/reflect/reflect.h"

#include <string.h>

#include "shrewd-basic/log/log.h"

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

/* Primitive descriptors: canonical widths; type->size is informational. */
const shrewd_basic_reflect_type_t shrewd_basic_reflect_bool = {
    .kind = SHREWD_BASIC_REFLECT_KIND_BOOL, .name = "bool",
    .size = sizeof(_Bool), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_char = {
    .kind = SHREWD_BASIC_REFLECT_KIND_CHAR, .name = "char",
    .size = sizeof(char), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int8 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_INT8, .name = "int8",
    .size = sizeof(int8_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint8 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_UINT8, .name = "uint8",
    .size = sizeof(uint8_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int16 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_INT16, .name = "int16",
    .size = sizeof(int16_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint16 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_UINT16, .name = "uint16",
    .size = sizeof(uint16_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int32 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_INT32, .name = "int32",
    .size = sizeof(int32_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint32 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_UINT32, .name = "uint32",
    .size = sizeof(uint32_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_int64 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_INT64, .name = "int64",
    .size = sizeof(int64_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_uint64 = {
    .kind = SHREWD_BASIC_REFLECT_KIND_UINT64, .name = "uint64",
    .size = sizeof(uint64_t), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_float = {
    .kind = SHREWD_BASIC_REFLECT_KIND_FLOAT, .name = "float",
    .size = sizeof(float), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_double = {
    .kind = SHREWD_BASIC_REFLECT_KIND_DOUBLE, .name = "double",
    .size = sizeof(double), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_string = {
    .kind = SHREWD_BASIC_REFLECT_KIND_STRING, .name = "string",
    .size = sizeof(const char *), .field_count = 0, .fields = NULL };
const shrewd_basic_reflect_type_t shrewd_basic_reflect_void = {
    .kind = SHREWD_BASIC_REFLECT_KIND_OPAQUE, .name = "void",
    .size = 0, .field_count = 0, .fields = NULL };

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

int shrewd_basic_reflect_type_find_field(const shrewd_basic_reflect_type_t *type,
                                         const char *name,
                                         const shrewd_basic_reflect_field_t **out)
{
    if ((NULL == type) || (NULL == name) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "find_field: type, name or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->field_count; ++i) {
        if (0 == strcmp(type->fields[i].name, name)) {
            *out = &type->fields[i];
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
    if (value_size != field->size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "field_get: size mismatch (%zu != %zu)\n",
                         value_size, field->size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(out_value, (const unsigned char *)object + field->offset, field->size);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "field_get ok (%s, %zu bytes)\n",
                     field->name, field->size);
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
    if (value_size != field->size) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "field_set: size mismatch (%zu != %zu)\n",
                         value_size, field->size);
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy((unsigned char *)object + field->offset, value, field->size);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "field_set ok (%s, %zu bytes)\n",
                     field->name, field->size);
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
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT != cur->kind) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        field = NULL;
        for (size_t i = 0; i < cur->field_count; ++i) {
            const char *candidate = cur->fields[i].name;
            if ((strlen(candidate) == name_len)
                    && (0 == memcmp(candidate, name, name_len))) {
                field = &cur->fields[i];
                break;
            }
        }
        if (NULL == field) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        offset += field->offset;
        size = field->size;

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
            if (0u == (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            if (index >= field->count) {
                return SHREWD_BASIC_ERR_KEY_NOTFOUND;
            }
            offset += index * (field->size / field->count);
            size = field->size / field->count;
            indexed = 1;
        }

        if ('.' == *p) {
            ++p;
            if ((0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY))
                    && (0 == indexed)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
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
            cur = field->type;
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

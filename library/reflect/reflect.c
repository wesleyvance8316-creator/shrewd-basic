#include "shrewd-basic/reflect/reflect.h"

#include <string.h>

#include "shrewd-basic/log/log.h"
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

/* Effective ops of a field: the field-level override wins over type ops. */
static const shrewd_basic_reflect_ops_t *reflect_field_ops(
        const shrewd_basic_reflect_field_t *field)
{
    return (NULL != field->ops) ? field->ops : field->type->ops;
}

/* Defensive recursion cap for the graph walkers (tree-only contract). */
#define REFLECT_MAX_DEPTH 16

/* Target address of a single-value field (deref one pointer level). */
static int reflect_field_target(const void *object,
                                const shrewd_basic_reflect_field_t *field,
                                const unsigned char **out_at)
{
    const unsigned char *at = (const unsigned char *)object + field->offset;
    if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        void *pointer = NULL;
        memcpy(&pointer, at, sizeof(pointer));
        if (NULL == pointer) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                             "field", "value: field \"%s\" is NULL\n", field->name);
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
                         field->name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_get: field \"%s\" is an array\n",
                         field->name);
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
                         field->name, value_size);
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
                         field->name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "value_set: field \"%s\" is an array\n",
                         field->name);
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
                         field->name, value_size);
    }
    return status;
}

/* --- graph walkers: check / copy / destroy ---------------------------- */

static int reflect_check_node(const shrewd_basic_reflect_type_t *type, const void *value,
                              const shrewd_basic_reflect_ops_t *ops, size_t depth)
{
    if (REFLECT_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->check)) {
        return ops->check(value);
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    const unsigned char *base = (const unsigned char *)value;
    for (size_t i = 0; i < type->field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->fields[i];
        const unsigned char *at = base + field->offset;
        size_t stride = field->size / field->count;
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        for (size_t j = 0; j < field->count; ++j) {
            const unsigned char *elem = at + (j * stride);
            const void *target = elem;
            if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = NULL;
                memcpy(&pointer, elem, sizeof(pointer));
                if (NULL == pointer) {
                    continue;
                }
                target = pointer;
            }
            int status = reflect_check_node(field->type, target, field_ops, depth + 1);
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
    int status = reflect_check_node(type, object, type->ops, 0);
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "check ok (%s)\n", type->name);
    } else {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "check failed (%s, %s)\n", type->name,
                         shrewd_basic_error_name(status));
    }
    return status;
}

static int reflect_destroy_value(const shrewd_basic_reflect_type_t *type, void *value,
                                 size_t depth);
static int reflect_copy_value(const shrewd_basic_reflect_type_t *type,
                              const unsigned char *src, unsigned char *dst,
                              size_t depth);

/*
 * Clone an object (a pointee, or the root of copy): ops->copy handles
 * opaque/complex targets, struct targets are cloned recursively, scalar
 * targets at their canonical width, string targets stay borrowed
 * (aliased) and past the depth cap pointers are aliased defensively.
 */
static int reflect_clone_target(const shrewd_basic_reflect_type_t *type, const void *src,
                                const shrewd_basic_reflect_ops_t *ops,
                                void **out, size_t depth)
{
    if ((NULL != ops) && (NULL != ops->copy)) {
        return ops->copy(out, src);
    }
    if (REFLECT_MAX_DEPTH <= depth) {
        *out = (void *)(uintptr_t)src;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->kind) {
        void *block = NULL;
        int status = shrewd_basic_platform_calloc(&block, 1, type->size);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = reflect_copy_value(type, (const unsigned char *)src,
                                    (unsigned char *)block, depth);
        if (SHREWD_BASIC_ERR_OK != status) {
            (void)reflect_destroy_value(type, block, depth);
            (void)shrewd_basic_platform_free(&block);
            return status;
        }
        *out = block;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->kind) {
        *out = (void *)(uintptr_t)src;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_OPAQUE == type->kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_alloc(&block, type->size);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(block, src, type->size);
    *out = block;
    return SHREWD_BASIC_ERR_OK;
}

/* Copy a value's contents into existing dst storage (zero-initialized). */
static int reflect_copy_value(const shrewd_basic_reflect_type_t *type,
                              const unsigned char *src, unsigned char *dst, size_t depth)
{
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->kind) {
        memcpy(dst, src, type->size);
        return SHREWD_BASIC_ERR_OK;
    }
    for (size_t i = 0; i < type->field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->fields[i];
        const unsigned char *src_at = src + field->offset;
        unsigned char *dst_at = dst + field->offset;
        size_t stride = field->size / field->count;
        for (size_t j = 0; j < field->count; ++j) {
            const unsigned char *src_elem = src_at + (j * stride);
            unsigned char *dst_elem = dst_at + (j * stride);
            if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = NULL;
                memcpy(&pointer, src_elem, sizeof(pointer));
                if (NULL == pointer) {
                    continue;
                }
                void *clone = NULL;
                int status = reflect_clone_target(field->type, pointer,
                                                  reflect_field_ops(field),
                                                  &clone, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                memcpy(dst_elem, &clone, sizeof(clone));
            } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->type->kind) {
                int status = reflect_copy_value(field->type, src_elem, dst_elem, depth + 1);
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
    if ((NULL != type->ops) && (NULL != type->ops->copy)) {
        int status = type->ops->copy(out, object);
        if (SHREWD_BASIC_ERR_OK == status) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                             "field", "copy ok (%s)\n", type->name);
        }
        return status;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->kind) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "copy: type \"%s\" needs ops->copy\n", type->name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1, type->size);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "field", "copy: allocation failed\n");
        return status;
    }
    status = reflect_copy_value(type, (const unsigned char *)object,
                                (unsigned char *)block, 0);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)reflect_destroy_value(type, block, 0);
        (void)shrewd_basic_platform_free(&block);
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "copy: failed (%s)\n",
                         shrewd_basic_error_name(status));
        return status;
    }
    *out = block;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "copy ok (%s)\n", type->name);
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
                                  const shrewd_basic_reflect_ops_t *ops, size_t depth)
{
    if ((NULL != ops) && (NULL != ops->destroy)) {
        ops->destroy(pointee);
        return SHREWD_BASIC_ERR_OK;
    }
    if (REFLECT_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->kind) {
        int status = reflect_destroy_value(type, pointee, depth);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        return shrewd_basic_platform_free(&pointee);
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_OPAQUE == type->kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    return shrewd_basic_platform_free(&pointee);
}

/* Release the pointer targets of a value; the value storage is untouched. */
static int reflect_destroy_value(const shrewd_basic_reflect_type_t *type, void *value,
                                 size_t depth)
{
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    unsigned char *base = (unsigned char *)value;
    for (size_t i = 0; i < type->field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->fields[i];
        unsigned char *at = base + field->offset;
        size_t stride = field->size / field->count;
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        for (size_t j = 0; j < field->count; ++j) {
            unsigned char *elem = at + (j * stride);
            if (0u != (field->flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = NULL;
                memcpy(&pointer, elem, sizeof(pointer));
                if (NULL == pointer) {
                    continue;
                }
                int status = reflect_destroy_target(field->type, pointer,
                                                    field_ops, depth + 1);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->type->kind) {
                int status = reflect_destroy_value(field->type, elem, depth + 1);
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
    if ((NULL != type->ops) && (NULL != type->ops->destroy)) {
        type->ops->destroy(object);
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "destroy ok (%s)\n", type->name);
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->kind) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "destroy: type \"%s\" needs ops->destroy\n",
                         type->name);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    int status = reflect_destroy_value(type, object, 0);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_platform_free(&object);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                         "field", "destroy ok (%s)\n", type->name);
    } else {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, shrewd_basic_reflect_log,
                         "field", "destroy: free failed\n");
    }
    return status;
}

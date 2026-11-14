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

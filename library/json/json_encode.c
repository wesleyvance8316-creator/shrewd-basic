#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * Struct -> DOM mapping over reflect metadata. Keys are member names in
 * descriptor order; enums by constant name; bit groups as numbers; char
 * as one-character strings; every array shape as a JSON array (rows of
 * multi-dimension arrays nest); pointers followed (NULL -> null).
 * Tagged unions use the F1 nest-preserving form: the tag member is its
 * own key, the union member is an object with the active variant's
 * fields as keys. Cycles fail (JSON cannot express them).
 */
#define JSON_MAP_MAX_DEPTH 32

typedef struct json_map_frame {
    const void *object;
    const shrewd_basic_reflect_type_t *type;
} json_map_frame_t;

typedef struct json_map_ctx {
    char path[256];
    size_t path_len;
    const shrewd_basic_json_options_t *options;
    shrewd_basic_json_error_t *error;
    json_map_frame_t stack[JSON_MAP_MAX_DEPTH];
} json_map_ctx_t;

static int json_map_fail(json_map_ctx_t *ctx, int status, const char *fmt, ...)
{
    if (NULL != ctx->error) {
        va_list ap;
        va_start(ap, fmt);
        (void)vsnprintf(ctx->error->message, sizeof(ctx->error->message), fmt, ap);
        va_end(ap);
        (void)snprintf(ctx->error->path, sizeof(ctx->error->path), "%s",
                       (0 != ctx->path_len) ? ctx->path : "(root)");
    }
    return status;
}

static size_t json_map_mark(const json_map_ctx_t *ctx)
{
    return ctx->path_len;
}

static void json_map_restore(json_map_ctx_t *ctx, size_t mark)
{
    ctx->path_len = mark;
    ctx->path[mark] = '\0';
}

static void json_map_append(json_map_ctx_t *ctx, const char *text)
{
    size_t room = (sizeof(ctx->path) - 1) - ctx->path_len;
    size_t used = strlen(text);
    if (used > room) {
        used = room;
    }
    memcpy(ctx->path + ctx->path_len, text, used);
    ctx->path_len += used;
    ctx->path[ctx->path_len] = '\0';
}

static void json_map_member(json_map_ctx_t *ctx, const char *name)
{
    if (0 != ctx->path_len) {
        json_map_append(ctx, ".");
    }
    json_map_append(ctx, name);
}

static void json_map_index(json_map_ctx_t *ctx, size_t index)
{
    char text[24];
    (void)snprintf(text, sizeof(text), "[%zu]", index);
    json_map_append(ctx, text);
}

static int json_map_seen(const json_map_ctx_t *ctx, size_t depth,
                         const void *object,
                         const shrewd_basic_reflect_type_t *type)
{
    for (size_t i = 0; i < depth; ++i) {
        if ((ctx->stack[i].object == object) && (ctx->stack[i].type == type)) {
            return 1;
        }
    }
    return 0;
}

static int json_encode_fields(json_map_ctx_t *ctx,
                              const shrewd_basic_reflect_field_t *fields,
                              size_t count, const unsigned char *object,
                              size_t depth, shrewd_basic_json_value_t *container);

static int json_encode_node(json_map_ctx_t *ctx,
                            const shrewd_basic_reflect_type_t *type,
                            const unsigned char *value, size_t nbytes,
                            const shrewd_basic_reflect_ops_t *ops, size_t depth,
                            shrewd_basic_json_value_t **out)
{
    *out = NULL;
    if ((NULL != ops) && (NULL != ops->render)) {
        /* Custom rendering (opaque targets and friends). */
        char text[256];
        if (SHREWD_BASIC_ERR_OK != ops->render(value, text, sizeof(text))) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                 "ops->render failed");
        }
        return shrewd_basic_json_new_string(text, out);
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return shrewd_basic_json_new_bool(
                0 != reflect_load_u64(value, nbytes), out);
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        return shrewd_basic_json_new_i64(reflect_load_i64(value, nbytes), out);
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = reflect_load_i64(value, nbytes);
        const char *name = NULL;
        if ((SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_enum_find_name(type, number,
                                                                        &name))
                && (NULL != name)) {
            return shrewd_basic_json_new_string(name, out);
        }
        return shrewd_basic_json_new_i64(number, out);
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        return shrewd_basic_json_new_u64(reflect_load_u64(value, nbytes), out);
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double number = reflect_load_f64(value, nbytes);
        if ((number != number) || (number > 1.7976931348623157e308)
                || (number < -1.7976931348623157e308)) {
            if (0 != ctx->options->allow_nan) {
                return shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, out);
            }
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                 "non-finite number has no JSON form");
        }
        return shrewd_basic_json_new_f64(number, out);
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = reflect_load_str(value);
        if (NULL == text) {
            return shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, out);
        }
        return shrewd_basic_json_new_string(text, out);
    }
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        memcpy(&c, value, 1);
        return shrewd_basic_json_new_string_n(&c, 1, out);
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "opaque target needs ops->render");
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        shrewd_basic_json_value_t *array = NULL;
        int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &array);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            shrewd_basic_json_value_t *item = NULL;
            size_t mark = json_map_mark(ctx);
            json_map_index(ctx, i);
            status = json_encode_node(ctx, type->sb_refl_elem,
                                      value + (i * type->sb_refl_elem_stride),
                                      type->sb_refl_elem_stride,
                                      type->sb_refl_elem->sb_refl_ops,
                                      depth + 1, &item);
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(array);
                return status;
            }
            status = shrewd_basic_json_append(array, item);
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(item);
                shrewd_basic_json_value_free(array);
                return status;
            }
        }
        *out = array;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "kind %d has no JSON form", (int)type->sb_refl_kind);
    }
    if (JSON_MAP_MAX_DEPTH <= depth) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_DEPTH, "nesting too deep");
    }
    if (0 != json_map_seen(ctx, depth, value, type)) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "cyclic reference");
    }
    ctx->stack[depth].object = value;
    ctx->stack[depth].type = type;
    {
        shrewd_basic_json_value_t *object = NULL;
        int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &object);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        status = json_encode_fields(ctx, type->sb_refl_fields,
                                    type->sb_refl_field_count, value, depth + 1,
                                    object);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(object);
            return status;
        }
        *out = object;
        return SHREWD_BASIC_ERR_OK;
    }
}

/* Encode one field's value (arrays bracketed, pointers followed). */
static int json_encode_field(json_map_ctx_t *ctx,
                             const shrewd_basic_reflect_field_t *field,
                             const unsigned char *object, size_t depth,
                             shrewd_basic_json_value_t **out)
{
    const unsigned char *at = object + field->sb_refl_offset;
    *out = NULL;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        uint64_t unit = reflect_load_u64(at, field->sb_refl_size);
        return shrewd_basic_json_new_u64(
                reflect_bits_extract(unit, field->sb_refl_bit_offset,
                                     field->sb_refl_bit_width),
                out);
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        return shrewd_basic_json_new_string_n(
                (const char *)at,
                reflect_strn_len((const char *)at, field->sb_refl_count), out);
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        void *pointer = reflect_load_ptr(at);
        if (NULL == pointer) {
            return shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, out);
        }
        return json_encode_node(ctx, field->sb_refl_type,
                                (const unsigned char *)pointer,
                                field->sb_refl_type->sb_refl_size,
                                reflect_field_ops(field), depth, out);
    }
    if ((0u != (field->sb_refl_flags
                & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                   | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)))) {
        reflect_array_view_t view = reflect_array_view(object, field);
        shrewd_basic_json_value_t *array = NULL;
        int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &array);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        for (size_t i = 0; i < view.count; ++i) {
            const unsigned char *elem = view.base + (i * view.stride);
            shrewd_basic_json_value_t *item = NULL;
            size_t mark = json_map_mark(ctx);
            json_map_index(ctx, i);
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                void *pointer = reflect_load_ptr(elem);
                if (NULL == pointer) {
                    status = shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, &item);
                } else {
                    status = json_encode_node(ctx, field->sb_refl_type,
                                              (const unsigned char *)pointer,
                                              field->sb_refl_type->sb_refl_size,
                                              reflect_field_ops(field),
                                              depth + 1, &item);
                }
            } else {
                status = json_encode_node(ctx, field->sb_refl_type, elem,
                                          view.stride, reflect_field_ops(field),
                                          depth + 1, &item);
            }
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(array);
                return status;
            }
            status = shrewd_basic_json_append(array, item);
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(item);
                shrewd_basic_json_value_free(array);
                return status;
            }
        }
        *out = array;
        return SHREWD_BASIC_ERR_OK;
    }
    return json_encode_node(ctx, field->sb_refl_type, at, field->sb_refl_stride,
                            reflect_field_ops(field), depth, out);
}

static int json_encode_fields(json_map_ctx_t *ctx,
                              const shrewd_basic_reflect_field_t *fields,
                              size_t count, const unsigned char *object,
                              size_t depth, shrewd_basic_json_value_t *container)
{
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        shrewd_basic_json_value_t *item = NULL;
        size_t mark = json_map_mark(ctx);
        int status;
        json_map_member(ctx, field->sb_refl_name);
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            /* F1 form: body object = the active variant's fields. */
            const unsigned char *storage = object + field->sb_refl_offset;
            const shrewd_basic_reflect_variant_t *variant =
                    reflect_union_variant(field, storage);
            shrewd_basic_json_value_t *body = NULL;
            if (NULL == variant) {
                if (0 != ctx->options->allow_unknown_union) {
                    status = shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, &body);
                } else {
                    status = json_map_fail(
                            ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                            "union tag names no variant");
                }
            } else {
                status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &body);
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = json_encode_fields(ctx, variant->sb_refl_fields,
                                                variant->sb_refl_field_count,
                                                storage, depth, body);
                }
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(body);
                json_map_restore(ctx, mark);
                return status;
            }
            status = shrewd_basic_json_set(container, field->sb_refl_name, body);
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(body);
                json_map_restore(ctx, mark);
                return status;
            }
            json_map_restore(ctx, mark);
            continue;
        }
        status = json_encode_field(ctx, field, object, depth, &item);
        json_map_restore(ctx, mark);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(item);
            return status;
        }
        status = shrewd_basic_json_set(container, field->sb_refl_name, item);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(item);
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_encode_value(const void *object,
                                   const shrewd_basic_reflect_type_t *type,
                                   shrewd_basic_json_value_t **out,
                                   const shrewd_basic_json_options_t *options,
                                   shrewd_basic_json_error_t *error)
{
    static const shrewd_basic_json_options_t plain = {0, 0, 0, 0, 0, 0, 0};
    json_map_ctx_t ctx;
    int status = 0;
    if ((NULL == object) || (NULL == type) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_json_log,
                         "encode", "encode: object, type or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if (NULL != error) {
        error->offset = 0;
        error->path[0] = '\0';
        error->message[0] = '\0';
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = (NULL != options) ? options : &plain;
    ctx.error = error;
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return json_map_fail(&ctx, SHREWD_BASIC_ERR_ARG_INVALID,
                             "type \"%s\" is not a struct", type->sb_refl_name);
    }
    status = json_encode_node(&ctx, type, (const unsigned char *)object,
                              type->sb_refl_size, type->sb_refl_ops, 0, out);
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_json_log,
                         "encode", "encode ok (%s)\n", type->sb_refl_name);
    }
    return status;
}

int shrewd_basic_json_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             char **out_text,
                             const shrewd_basic_json_options_t *options,
                             shrewd_basic_json_error_t *error)
{
    shrewd_basic_json_value_t *tree = NULL;
    int status = shrewd_basic_json_encode_value(object, type, &tree, options,
                                                error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = shrewd_basic_json_serialize(tree, out_text, options, error);
    shrewd_basic_json_value_free(tree);
    return status;
}

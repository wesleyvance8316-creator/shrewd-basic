#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * DOM -> struct mapping. The target is zeroed first (missing keys keep
 * zero defaults), then JSON members fill fields; count members are
 * DERIVED from array lengths (JSON count keys are ignored). Arrays may
 * be shorter than their slots (the tail stays zero) but not longer.
 * Strings and pointer targets are allocated through platform_alloc, so
 * shrewd_basic_json_free() - the exact inverse - releases everything.
 */
#define JSON_MAP_MAX_DEPTH 32

typedef struct json_map_ctx {
    char path[256];
    size_t path_len;
    const shrewd_basic_json_options_t *options;
    shrewd_basic_json_error_t *error;
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

/* Find a field by name in a bare field list (struct or union variant). */
static const shrewd_basic_reflect_field_t *json_map_find(
        const shrewd_basic_reflect_field_t *fields, size_t count,
        const char *name)
{
    for (size_t i = 0; i < count; ++i) {
        if (0 == strcmp(fields[i].sb_refl_name, name)) {
            return &fields[i];
        }
    }
    return NULL;
}

/*
 * A field that is the count member of an array-shaped field is derived
 * data: JSON count keys are ignored, the array length is authoritative.
 */
static int json_map_is_count_member(const shrewd_basic_reflect_field_t *fields,
                                    size_t count,
                                    const shrewd_basic_reflect_field_t *field)
{
    for (size_t i = 0; i < count; ++i) {
        if ((0u != (fields[i].sb_refl_flags
                    & (SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                       | SHREWD_BASIC_REFLECT_FIELD_F_FLEX)))
                && (0 != fields[i].sb_refl_limit_size)
                && (fields[i].sb_refl_limit_offset == field->sb_refl_offset)) {
            return 1;
        }
    }
    return 0;
}

static void json_map_store_count(const shrewd_basic_reflect_field_t *field,
                                 unsigned char *object, size_t n)
{
    if (0 != field->sb_refl_limit_size) {
        uint64_t bound = (uint64_t)n;
        memcpy(object + field->sb_refl_limit_offset, &bound,
               field->sb_refl_limit_size);
    }
}

static int json_decode_fields(json_map_ctx_t *ctx,
                              const shrewd_basic_json_value_t *value,
                              const shrewd_basic_reflect_field_t *fields,
                              size_t count, unsigned char *object, size_t depth);

static int json_decode_node(json_map_ctx_t *ctx,
                            const shrewd_basic_json_value_t *value,
                            const shrewd_basic_reflect_type_t *type,
                            unsigned char *target, size_t nbytes, size_t depth)
{
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL: {
        bool flag = false;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_bool(value, &flag)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected a boolean");
        }
        reflect_store_u64(target, nbytes, flag ? 1u : 0u);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64: {
        int64_t number = 0;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected an integer");
        }
        reflect_store_u64(target, nbytes, (uint64_t)number);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = 0;
        if (SHREWD_BASIC_JSON_STRING == shrewd_basic_json_kind(value)) {
            const char *name = shrewd_basic_json_to_string(value);
            if (SHREWD_BASIC_ERR_OK
                    != shrewd_basic_reflect_enum_find_value(type, name, &number)) {
                return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                     "unknown enum constant \"%s\"", name);
            }
        } else if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected an enum name or number");
        }
        reflect_store_u64(target, nbytes, (uint64_t)number);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64: {
        uint64_t number = 0;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_u64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected an unsigned integer");
        }
        reflect_store_u64(target, nbytes, number);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double number = 0.0;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_f64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected a number");
        }
        if (4 == nbytes) {
            float narrowed = (float)number;
            memcpy(target, &narrowed, sizeof(narrowed));
        } else {
            memcpy(target, &number, sizeof(number));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = NULL;
        void *block = NULL;
        if (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(value)) {
            memset(target, 0, nbytes);
            return SHREWD_BASIC_ERR_OK;
        }
        if (SHREWD_BASIC_JSON_STRING != shrewd_basic_json_kind(value)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected a string or null");
        }
        text = shrewd_basic_json_to_string(value);
        if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_alloc(&block, strlen(text) + 1)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        memcpy(block, text, strlen(text) + 1);
        memcpy(target, &block, sizeof(block));
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        if (SHREWD_BASIC_JSON_STRING == shrewd_basic_json_kind(value)) {
            const char *text = shrewd_basic_json_to_string(value);
            if (1 != strlen(text)) {
                return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                     "expected a one-character string");
            }
            c = text[0];
        } else {
            int64_t number = 0;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &number)) {
                return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                     "expected a character or number");
            }
            c = (char)number;
        }
        memcpy(target, &c, 1);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "opaque targets cannot be decoded");
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        size_t count = 0;
        if (SHREWD_BASIC_JSON_ARRAY != shrewd_basic_json_kind(value)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected an array");
        }
        count = shrewd_basic_json_count(value);
        if (count > type->sb_refl_elem_count) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "array longer than %zu",
                                 type->sb_refl_elem_count);
        }
        for (size_t i = 0; i < count; ++i) {
            size_t mark = json_map_mark(ctx);
            int status;
            json_map_index(ctx, i);
            status = json_decode_node(ctx, shrewd_basic_json_at(value, i),
                                      type->sb_refl_elem,
                                      target + (i * type->sb_refl_elem_stride),
                                      type->sb_refl_elem_stride, depth + 1);
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "kind %d cannot be decoded",
                             (int)type->sb_refl_kind);
    }
    if (JSON_MAP_MAX_DEPTH <= depth) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_DEPTH, "nesting too deep");
    }
    if (SHREWD_BASIC_JSON_OBJECT != shrewd_basic_json_kind(value)) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                             "expected an object");
    }
    memset(target, 0, type->sb_refl_size);
    return json_decode_fields(ctx, value, type->sb_refl_fields,
                              type->sb_refl_field_count, target, depth + 1);
}

/* Allocation size of a decode target: the header plus FAM tails from JSON. */
static size_t json_target_size(const shrewd_basic_json_value_t *value,
                               const shrewd_basic_reflect_type_t *type)
{
    size_t size = type->sb_refl_size;
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const shrewd_basic_json_value_t *member = NULL;
        if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
            continue;
        }
        member = shrewd_basic_json_get(value, field->sb_refl_name);
        if (SHREWD_BASIC_JSON_ARRAY == shrewd_basic_json_kind(member)) {
            size += shrewd_basic_json_count(member) * field->sb_refl_stride;
        }
    }
    return size;
}

/* Decode one pointer target (allocates; writes the pointer into slot). */
static int json_decode_pointer(json_map_ctx_t *ctx,
                               const shrewd_basic_json_value_t *value,
                               const shrewd_basic_reflect_type_t *type,
                               unsigned char *slot, size_t depth)
{
    void *target = NULL;
    size_t size = 0;
    int status = 0;
    if (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(value)) {
        memset(slot, 0, sizeof(void *));
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_OPAQUE == type->sb_refl_kind) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "opaque targets cannot be decoded");
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        size = json_target_size(value, type);
    } else {
        size = type->sb_refl_size;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&target, 1, size)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    status = json_decode_node(ctx, value, type, (unsigned char *)target, size,
                              depth + 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_platform_free(&target);
        return status;
    }
    memcpy(slot, &target, sizeof(target));
    return SHREWD_BASIC_ERR_OK;
}

/* Decode one field's value into the owner at @p object. */
static int json_decode_field(json_map_ctx_t *ctx,
                             const shrewd_basic_json_value_t *value,
                             const shrewd_basic_reflect_field_t *field,
                             unsigned char *object, size_t depth)
{
    unsigned char *at = object + field->sb_refl_offset;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        uint64_t number = 0;
        uint64_t unit = 0;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_u64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected a number");
        }
        unit = reflect_load_u64(at, field->sb_refl_size);
        reflect_bits_insert(&unit, field->sb_refl_bit_offset,
                            field->sb_refl_bit_width, number);
        reflect_store_u64(at, field->sb_refl_size, unit);
        return SHREWD_BASIC_ERR_OK;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        const char *text = NULL;
        size_t length = 0;
        if (SHREWD_BASIC_JSON_STRING != shrewd_basic_json_kind(value)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected a string");
        }
        text = shrewd_basic_json_to_string(value);
        length = strlen(text);
        if (length > field->sb_refl_count) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "string longer than %zu", field->sb_refl_count);
        }
        memcpy(at, text, length);
        if (length < field->sb_refl_count) {
            at[length] = '\0';
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        return json_decode_pointer(ctx, value, field->sb_refl_type, at, depth);
    }
    if (0u != (field->sb_refl_flags
               & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                  | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
        unsigned char *table = NULL;
        size_t count = 0;
        size_t stride = field->sb_refl_stride;
        int dynamic = (0u != (field->sb_refl_flags
                              & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY));
        int flex = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX));
        if (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(value)) {
            if (0 == dynamic) {
                return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                     "expected an array");
            }
            return SHREWD_BASIC_ERR_OK; /* NULL table */
        }
        if (SHREWD_BASIC_JSON_ARRAY != shrewd_basic_json_kind(value)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "expected an array");
        }
        count = shrewd_basic_json_count(value);
        if (0 != dynamic) {
            size_t bytes = (count + ((0u != (field->sb_refl_flags
                                             & SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL))
                                     ? 1u : 0u)) * stride;
            if (SHREWD_BASIC_ERR_OK
                    != shrewd_basic_platform_calloc((void **)&table, 1, bytes)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            memcpy(at, &table, sizeof(table));
            at = table;
            json_map_store_count(field, object, count);
        } else if (0 != flex) {
            /* Inline tail: the caller sized the allocation from the JSON
               (the documented FAM contract); the count member follows. */
            json_map_store_count(field, object, count);
        } else {
            if (count > field->sb_refl_count) {
                return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                     "array longer than %zu",
                                     field->sb_refl_count);
            }
        }
        for (size_t i = 0; i < count; ++i) {
            unsigned char *slot = at + (i * stride);
            const shrewd_basic_json_value_t *item = shrewd_basic_json_at(value, i);
            size_t mark = json_map_mark(ctx);
            int status;
            json_map_index(ctx, i);
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                status = json_decode_pointer(ctx, item, field->sb_refl_type,
                                             slot, depth);
            } else {
                status = json_decode_node(ctx, item, field->sb_refl_type, slot,
                                          stride, depth + 1);
            }
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    return json_decode_node(ctx, value, field->sb_refl_type, at,
                            field->sb_refl_stride, depth + 1);
}

static int json_decode_fields(json_map_ctx_t *ctx,
                              const shrewd_basic_json_value_t *value,
                              const shrewd_basic_reflect_field_t *fields,
                              size_t count, unsigned char *object, size_t depth)
{
    /* Pass 1: JSON members fill their fields (unions and count members
       wait; unknown keys are ignored). */
    for (size_t i = 0; i < shrewd_basic_json_count(value); ++i) {
        const char *key = shrewd_basic_json_key_at(value, i);
        const shrewd_basic_reflect_field_t *field =
                json_map_find(fields, count, key);
        shrewd_basic_json_value_t *member = NULL;
        size_t mark = 0;
        int status;
        if ((NULL == field)
                || (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION))
                || (0 != json_map_is_count_member(fields, count, field))) {
            continue;
        }
        member = shrewd_basic_json_at(value, i);
        mark = json_map_mark(ctx);
        json_map_member(ctx, field->sb_refl_name);
        status = json_decode_field(ctx, member, field, object, depth);
        json_map_restore(ctx, mark);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    /* Pass 2: union bodies (the tag member is filled by pass 1). */
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const shrewd_basic_json_value_t *member = NULL;
        const shrewd_basic_reflect_variant_t *variant = NULL;
        unsigned char *storage = NULL;
        size_t mark = 0;
        int status;
        if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            continue;
        }
        member = shrewd_basic_json_get(value, field->sb_refl_name);
        if ((NULL == member)
                || (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(member))) {
            continue;
        }
        if (SHREWD_BASIC_JSON_OBJECT != shrewd_basic_json_kind(member)) {
            mark = json_map_mark(ctx);
            json_map_member(ctx, field->sb_refl_name);
            status = json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                   "expected an object or null");
            json_map_restore(ctx, mark);
            return status;
        }
        storage = object + field->sb_refl_offset;
        variant = reflect_union_variant(field, storage);
        if (NULL == variant) {
            mark = json_map_mark(ctx);
            json_map_member(ctx, field->sb_refl_name);
            status = json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                   "union tag names no variant");
            json_map_restore(ctx, mark);
            return status;
        }
        mark = json_map_mark(ctx);
        json_map_member(ctx, field->sb_refl_name);
        status = json_decode_fields(ctx, member, variant->sb_refl_fields,
                                    variant->sb_refl_field_count, storage,
                                    depth + 1);
        json_map_restore(ctx, mark);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_decode_value(const shrewd_basic_json_value_t *value,
                                   void *object,
                                   const shrewd_basic_reflect_type_t *type,
                                   const shrewd_basic_json_options_t *options,
                                   shrewd_basic_json_error_t *error)
{
    static const shrewd_basic_json_options_t plain = {0, 0, 0, 0, 0, 0, 0};
    json_map_ctx_t ctx;
    if ((NULL == value) || (NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_json_log,
                         "decode", "decode: value, object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
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
    return json_decode_node(&ctx, value, type, (unsigned char *)object,
                            type->sb_refl_size, 0);
}

int shrewd_basic_json_decode(const char *text, size_t length, void *object,
                             const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_options_t *options,
                             shrewd_basic_json_error_t *error)
{
    shrewd_basic_json_value_t *tree = NULL;
    int status = shrewd_basic_json_parse(text, length, &tree, options, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = shrewd_basic_json_decode_value(tree, object, type, options, error);
    shrewd_basic_json_value_free(tree);
    return status;
}

/* --- json_free: the inverse of decode --------------------------------- */

static int json_free_fields(const shrewd_basic_reflect_field_t *fields,
                            size_t count, void *object);

/* Release one decoded value in place (its storage is not freed here). */
static void json_free_node(const shrewd_basic_reflect_type_t *type,
                           void *value, size_t nbytes)
{
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
        void *text = NULL;
        memcpy(&text, value, sizeof(text));
        (void)shrewd_basic_platform_free(&text);
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            json_free_node(type->sb_refl_elem,
                           (unsigned char *)value
                                   + (i * type->sb_refl_elem_stride),
                           type->sb_refl_elem_stride);
        }
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        (void)json_free_fields(type->sb_refl_fields, type->sb_refl_field_count,
                               value);
        return;
    }
    (void)nbytes;
}

static void json_free_pointer(const shrewd_basic_reflect_type_t *type,
                              void *pointer)
{
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
        /* A followed string: the pointee cell holds the content. */
        void *text = NULL;
        memcpy(&text, pointer, sizeof(text));
        (void)shrewd_basic_platform_free(&text);
    } else if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind)) {
        json_free_node(type, pointer, type->sb_refl_size);
    }
    (void)shrewd_basic_platform_free(&pointer);
}

static int json_free_fields(const shrewd_basic_reflect_field_t *fields,
                            size_t count, void *object)
{
    unsigned char *base = (unsigned char *)object;
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        unsigned char *at = base + field->sb_refl_offset;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            const shrewd_basic_reflect_variant_t *variant =
                    reflect_union_variant(field, at);
            if (NULL != variant) {
                (void)json_free_fields(variant->sb_refl_fields,
                                       variant->sb_refl_field_count, at);
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            void *pointer = NULL;
            memcpy(&pointer, at, sizeof(pointer));
            if (NULL != pointer) {
                json_free_pointer(field->sb_refl_type, pointer);
            }
            continue;
        }
        if (0u != (field->sb_refl_flags
                   & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                      | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
            reflect_array_view_t view = reflect_array_view(base, field);
            unsigned char *table = NULL;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                if (NULL == view.base) {
                    continue;
                }
                table = (unsigned char *)(uintptr_t)view.base;
            }
            for (size_t j = 0; j < view.count; ++j) {
                unsigned char *slot = (NULL != table) ? (table + (j * view.stride))
                                                      : (at + (j * view.stride));
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    void *pointer = NULL;
                    memcpy(&pointer, slot, sizeof(pointer));
                    if (NULL != pointer) {
                        json_free_pointer(field->sb_refl_type, pointer);
                    }
                } else {
                    json_free_node(field->sb_refl_type, slot, view.stride);
                }
            }
            if (NULL != table) {
                (void)shrewd_basic_platform_free((void **)&table);
            }
            continue;
        }
        json_free_node(field->sb_refl_type, at, field->sb_refl_stride);
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_free(void *object, const shrewd_basic_reflect_type_t *type)
{
    if ((NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_json_log,
                         "free", "free: object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    (void)json_free_fields(type->sb_refl_fields, type->sb_refl_field_count,
                           object);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_json_log,
                     "free", "free ok (%s)\n", type->sb_refl_name);
    return SHREWD_BASIC_ERR_OK;
}

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
 * DOM -> struct mapping. The target is zeroed first, then JSON members
 * fill fields; count members are DERIVED from array lengths (JSON count
 * keys are ignored). Arrays may be shorter than their slots (the tail
 * stays zero) but not longer. Strings and pointer targets are allocated
 * through platform_alloc, and every pointer decode writes is decode-
 * owned (policy BORROWED fields excepted), so
 * shrewd_basic_json_free() - the exact inverse - releases every non-
 * NULL pointer. Consequence: never decode over an object that still
 * holds decoded allocations (free first), and hand-install pointers
 * only into BORROWED fields.
 */
#define JSON_MAP_MAX_DEPTH 32

typedef struct json_map_ctx {
    char path[256];
    size_t path_len;
    const shrewd_basic_json_options_t *options;
    const shrewd_basic_json_info_t *info;
    shrewd_basic_json_error_t *error;
} json_map_ctx_t;

/* Release walk (below): frees what decode allocated in a partial tree. */
static void json_release_value(const shrewd_basic_reflect_type_t *type,
                               void *value,
                               const shrewd_basic_json_info_t *info);

static int json_map_fail(json_map_ctx_t *ctx, int status, const char *fmt, ...) {
    if (NULL != ctx->error) {
        va_list ap;
        va_start(ap, fmt);
        (void)vsnprintf(ctx->error->message, sizeof(ctx->error->message), fmt, ap);
        va_end(ap);
        (void)snprintf(ctx->error->path,
                       sizeof(ctx->error->path),
                       "%s",
                       (0 != ctx->path_len) ? ctx->path : "(root)");
    }
    return status;
}

static size_t json_map_mark(const json_map_ctx_t *ctx) {
    return ctx->path_len;
}

static void json_map_restore(json_map_ctx_t *ctx, size_t mark) {
    ctx->path_len = mark;
    ctx->path[mark] = '\0';
}

static void json_map_append(json_map_ctx_t *ctx, const char *text) {
    size_t room = (sizeof(ctx->path) - 1) - ctx->path_len;
    size_t used = strlen(text);
    if (used > room) {
        used = room;
    }
    memcpy(ctx->path + ctx->path_len, text, used);
    ctx->path_len += used;
    ctx->path[ctx->path_len] = '\0';
}

static void json_map_member(json_map_ctx_t *ctx, const char *name) {
    if (0 != ctx->path_len) {
        json_map_append(ctx, ".");
    }
    json_map_append(ctx, name);
}

static void json_map_index(json_map_ctx_t *ctx, size_t index) {
    char text[24] = {0};
    (void)snprintf(text, sizeof(text), "[%zu]", index);
    json_map_append(ctx, text);
}

/*
 * Key equivalence: exact by default, or ignoring case and '_' in the
 * loose mode (so name_id / nameID / NameID all match).
 */
static int json_key_equiv(const shrewd_basic_json_options_t *options,
                          const char *a,
                          const char *b) {
    if (SHREWD_BASIC_JSON_KEY_MATCH_LOOSE != options->key_match) {
        return (0 == strcmp(a, b)) ? 1 : 0;
    }
    while (('\0' != *a) && ('\0' != *b)) {
        if ('_' == *a) {
            ++a;
            continue;
        }
        if ('_' == *b) {
            ++b;
            continue;
        }
        {
            int ca = (('A' <= *a) && ('Z' >= *a)) ? (*a - 'A') + 'a' : *a;
            int cb = (('A' <= *b) && ('Z' >= *b)) ? (*b - 'A') + 'a' : *b;
            if (ca != cb) {
                return 0;
            }
        }
        ++a;
        ++b;
    }
    while ('_' == *a) {
        ++a;
    }
    while ('_' == *b) {
        ++b;
    }
    return (('\0' == *a) && ('\0' == *b)) ? 1 : 0;
}

/* Two fields collide under the key policy when some accepted key matches. */
static int json_policy_collide(const shrewd_basic_json_info_t *info,
                               const shrewd_basic_json_options_t *options,
                               const shrewd_basic_reflect_field_t *fields,
                               size_t count) {
    if (SHREWD_BASIC_JSON_KEY_MATCH_LOOSE != options->key_match) {
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        shrewd_basic_json_key_info_t a = {0};
        json_policy_view(info, &fields[i], &a);
        for (size_t j = i + 1; j < count; ++j) {
            shrewd_basic_json_key_info_t b = {0};
            json_policy_view(info, &fields[j], &b);
            for (size_t ka = 0; ka < a.key_count + 1; ++ka) {
                const char *na = (ka < a.key_count) ? a.keys[ka] : fields[i].sb_refl_name;
                for (size_t kb = 0; kb < b.key_count + 1; ++kb) {
                    const char *nb = (kb < b.key_count) ? b.keys[kb] : fields[j].sb_refl_name;
                    if (0 != json_key_equiv(options, na, nb)) {
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

/*
 * Find the field a JSON key maps to: any accepted key of the merged
 * policy view (the member name by default). Returns NULL for unknown
 * keys.
 */
static const shrewd_basic_reflect_field_t *json_map_find(const shrewd_basic_json_info_t *info,
                                                         const shrewd_basic_json_options_t *options,
                                                         const shrewd_basic_reflect_field_t *fields,
                                                         size_t count,
                                                         const char *key) {
    for (size_t i = 0; i < count; ++i) {
        shrewd_basic_json_key_info_t view = {0};
        json_policy_view(info, &fields[i], &view);
        if (0 != json_key_equiv(options, fields[i].sb_refl_name, key)) {
            return &fields[i];
        }
        for (size_t k = 0; k < view.key_count; ++k) {
            if (0 != json_key_equiv(options, view.keys[k], key)) {
                return &fields[i];
            }
        }
    }
    return NULL;
}

/* Does any accepted key of @p field match @p key? */
static int json_map_key_matches(const shrewd_basic_json_info_t *info,
                                const shrewd_basic_json_options_t *options,
                                const shrewd_basic_reflect_field_t *field,
                                const char *key) {
    shrewd_basic_json_key_info_t view = {0};
    json_policy_view(info, field, &view);
    if (0 != json_key_equiv(options, field->sb_refl_name, key)) {
        return 1;
    }
    for (size_t k = 0; k < view.key_count; ++k) {
        if (0 != json_key_equiv(options, view.keys[k], key)) {
            return 1;
        }
    }
    return 0;
}

/*
 * A field that is the count member of an array-shaped field is derived
 * data: JSON count keys are ignored, the array length is authoritative.
 */
static int json_map_is_count_member(const shrewd_basic_reflect_field_t *fields,
                                    size_t count,
                                    const shrewd_basic_reflect_field_t *field) {
    for (size_t i = 0; i < count; ++i) {
        if ((0u
             != (fields[i].sb_refl_flags
                 & (SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_FLEX)))
            && (0 != fields[i].sb_refl_limit_size)
            && (fields[i].sb_refl_limit_offset == field->sb_refl_offset)) {
            return 1;
        }
    }
    return 0;
}

static void json_map_store_count(const shrewd_basic_reflect_field_t *field,
                                 unsigned char *object,
                                 size_t n) {
    if (0 != field->sb_refl_limit_size) {
        uint64_t bound = (uint64_t)n;
        memcpy(object + field->sb_refl_limit_offset, &bound, field->sb_refl_limit_size);
    }
}

static int json_decode_fields(json_map_ctx_t *ctx,
                              const shrewd_basic_json_value_t *value,
                              const shrewd_basic_reflect_field_t *fields,
                              size_t count,
                              unsigned char *object,
                              size_t depth);

/* Release one field's decoded content (BORROWED fields excluded). */
static void json_release_field(const shrewd_basic_reflect_field_t *field,
                               void *object,
                               const shrewd_basic_json_info_t *info);

static int json_decode_node(json_map_ctx_t *ctx,
                            const shrewd_basic_json_value_t *value,
                            const shrewd_basic_reflect_type_t *type,
                            unsigned char *target,
                            size_t nbytes,
                            size_t depth) {
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL: {
        bool flag = false;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_bool(value, &flag)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a boolean");
        }
        reflect_store_u64(target, nbytes, (flag) ? 1u : 0u);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64: {
        int64_t number = 0;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an integer");
        }
        reflect_store_u64(target, nbytes, (uint64_t)number);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = 0;
        if (SHREWD_BASIC_JSON_STRING == shrewd_basic_json_kind(value)) {
            const char *name = shrewd_basic_json_to_string(value);
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_enum_find_value(type, name, &number)) {
                return json_map_fail(
                    ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "unknown enum constant \"%s\"", name);
            }
        } else if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &number)) {
            return json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an enum name or number");
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
            return json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an unsigned integer");
        }
        reflect_store_u64(target, nbytes, number);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double number = 0.0;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_f64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a number");
        }
        if (4 == nbytes) {
            float narrowed = (float)number;
            memcpy(target, &narrowed, sizeof(narrowed));
        } else {
            memcpy(target, &number, sizeof(number));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        double number = 0.0;
        long double wide = 0.0L;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_f64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a number");
        }
        wide = (long double)number;
        memcpy(target, &wide, sizeof(wide));
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_COMPLEX: {
        size_t half = nbytes / 2;
        if (SHREWD_BASIC_JSON_ARRAY != shrewd_basic_json_kind(value)) {
            return json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an [re, im] array");
        }
        if (2 != shrewd_basic_json_count(value)) {
            return json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an [re, im] pair");
        }
        for (size_t i = 0; i < 2; ++i) {
            double number = 0.0;
            if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_json_to_f64(shrewd_basic_json_at(value, i), &number)) {
                return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a number");
            }
            if (4 == half) {
                float single = (float)number;
                memcpy(target + (i * half), &single, sizeof(single));
            } else if (8 == half) {
                memcpy(target + (i * half), &number, sizeof(number));
            } else {
                long double wide = (long double)number;
                memcpy(target + (i * half), &wide, sizeof(wide));
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_BIT_INT:
        return json_map_fail(ctx,
                             SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "bit-int values cannot be decoded (use an ops->decode hook)");
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = NULL;
        void *block = NULL;
        if (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(value)) {
            memset(target, 0, nbytes);
            return SHREWD_BASIC_ERR_OK;
        }
        if (SHREWD_BASIC_JSON_STRING != shrewd_basic_json_kind(value)) {
            return json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a string or null");
        }
        text = shrewd_basic_json_to_string(value);
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, strlen(text) + 1)) {
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
                return json_map_fail(
                    ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a one-character string");
            }
            c = text[0];
        } else {
            int64_t number = 0;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_i64(value, &number)) {
                return json_map_fail(
                    ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a character or number");
            }
            c = (char)number;
        }
        memcpy(target, &c, 1);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        return json_map_fail(
            ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "opaque targets cannot be decoded");
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        size_t count = 0;
        if (SHREWD_BASIC_JSON_ARRAY != shrewd_basic_json_kind(value)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an array");
        }
        count = shrewd_basic_json_count(value);
        if (count > type->sb_refl_elem_count) {
            return json_map_fail(ctx,
                                 SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "array longer than %zu",
                                 type->sb_refl_elem_count);
        }
        for (size_t i = 0; i < count; ++i) {
            size_t mark = json_map_mark(ctx);
            int status = 0;
            json_map_index(ctx, i);
            status = json_decode_node(ctx,
                                      shrewd_basic_json_at(value, i),
                                      type->sb_refl_elem,
                                      target + (i * type->sb_refl_elem_stride),
                                      type->sb_refl_elem_stride,
                                      depth + 1);
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return json_map_fail(ctx,
                             SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "kind %d cannot be decoded",
                             (int)type->sb_refl_kind);
    }
    if (JSON_MAP_MAX_DEPTH <= depth) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_DEPTH, "nesting too deep");
    }
    if (SHREWD_BASIC_JSON_OBJECT != shrewd_basic_json_kind(value)) {
        return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an object");
    }
    memset(target, 0, type->sb_refl_size);
    return json_decode_fields(
        ctx, value, type->sb_refl_fields, type->sb_refl_field_count, target, depth + 1);
}

/* Allocation size of a decode target: the header plus FAM tails from JSON. */
static size_t json_target_size(const shrewd_basic_json_value_t *value,
                               const shrewd_basic_reflect_type_t *type) {
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
                               unsigned char *slot,
                               size_t depth) {
    void *target = NULL;
    size_t size = 0;
    int status = 0;
    if (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(value)) {
        memset(slot, 0, sizeof(void *));
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_OPAQUE == type->sb_refl_kind) {
        return json_map_fail(
            ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "opaque targets cannot be decoded");
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        size = json_target_size(value, type);
    } else {
        size = type->sb_refl_size;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&target, 1, size)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    status = json_decode_node(ctx, value, type, (unsigned char *)target, size, depth + 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        json_release_value(type, target, ctx->info);
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
                             unsigned char *object,
                             size_t depth,
                             const shrewd_basic_json_key_info_t *view) {
    unsigned char *at = object + field->sb_refl_offset;
    if ((NULL != view->ops) && (NULL != view->ops->decode) && (0 == view->elem_ops)) {
        /* Custom hook replaces the metadata mapping: the field storage
           for value fields, the pointer slot for pointer fields (the ops
           allocates the target - json_free releases it via ops->destroy). */
        return view->ops->decode(value, at);
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
        uint64_t number = 0;
        uint64_t unit = 0;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_json_to_u64(value, &number)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a number");
        }
        unit = reflect_load_u64(at, field->sb_refl_size);
        reflect_bits_insert(&unit, field->sb_refl_bit_offset, field->sb_refl_bit_width, number);
        reflect_store_u64(at, field->sb_refl_size, unit);
        return SHREWD_BASIC_ERR_OK;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        const char *text = NULL;
        size_t length = 0;
        if (1 != field->sb_refl_stride) {
            /* Wide buffer: needs the UTF-8 mapping (not yet). */
            return json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "wide string buffer has no JSON form yet");
        }
        if (SHREWD_BASIC_JSON_STRING != shrewd_basic_json_kind(value)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected a string");
        }
        text = shrewd_basic_json_to_string(value);
        length = strlen(text);
        if (length > field->sb_refl_count) {
            return json_map_fail(ctx,
                                 SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "string longer than %zu",
                                 field->sb_refl_count);
        }
        memcpy(at, text, length);
        if (length < field->sb_refl_count) {
            at[length] = '\0';
        }
        return SHREWD_BASIC_ERR_OK;
    }
    /* Array shapes come before the single-pointer case: a pointer table
       is an array first and a pointer second. */
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
        unsigned char *table = NULL;
        size_t count = 0;
        size_t stride = field->sb_refl_stride;
        int dynamic = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY));
        int flex = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX));
        if (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(value)) {
            if (0 == dynamic) {
                return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an array");
            }
            return SHREWD_BASIC_ERR_OK; /* NULL table */
        }
        if (SHREWD_BASIC_JSON_ARRAY != shrewd_basic_json_kind(value)) {
            return json_map_fail(ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an array");
        }
        count = shrewd_basic_json_count(value);
        if (0 != dynamic) {
            size_t bytes
                = (count
                   + ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL)) ? 1u
                                                                                             : 0u))
                  * stride;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc((void **)&table, 1, bytes)) {
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
                return json_map_fail(ctx,
                                     SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                     "array longer than %zu",
                                     field->sb_refl_count);
            }
        }
        for (size_t i = 0; i < count; ++i) {
            unsigned char *slot = at + (i * stride);
            const shrewd_basic_json_value_t *item = shrewd_basic_json_at(value, i);
            size_t mark = json_map_mark(ctx);
            int status = 0;
            json_map_index(ctx, i);
            if ((NULL != view->ops) && (NULL != view->ops->decode) && (0 != view->elem_ops)) {
                /* Element-scope hook: the slot per element (the ops owns
                   pointer allocations exactly like at field level). */
                status = view->ops->decode(item, slot);
                if ((SHREWD_BASIC_ERR_OK == status) && (NULL != view->ops->check)) {
                    status = view->ops->check(slot);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    (void)json_map_fail(ctx, status, "element %zu rejected by ops", i);
                }
            } else if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                status = json_decode_pointer(ctx, item, field->sb_refl_type, slot, depth);
            } else {
                status = json_decode_node(ctx, item, field->sb_refl_type, slot, stride, depth + 1);
            }
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        return json_decode_pointer(ctx, value, field->sb_refl_type, at, depth);
    }
    return json_decode_node(ctx, value, field->sb_refl_type, at, field->sb_refl_stride, depth + 1);
}

/*
 * Materialize a policy default into a missing field (use_defaults).
 * Phase one covers plain scalars and strings without ops hooks; other
 * shapes stay zero. Returns OK with the field untouched when the row is
 * out of scope.
 */
static int json_decode_default(json_map_ctx_t *ctx,
                               const shrewd_basic_reflect_field_t *field,
                               unsigned char *object,
                               const shrewd_basic_json_key_info_t *view,
                               size_t depth) {
    unsigned char *at = object + field->sb_refl_offset;
    if (SHREWD_BASIC_JSON_DEF_JSON == view->def_kind) {
        /* Phase two: any shape. Parse the text and run the field's own
           mapping over the DOM (skipping flexible tails: the root is
           sized for the JSON that is present, not for defaults). */
        shrewd_basic_json_value_t *dom = NULL;
        int status = 0;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_FLEX)) {
            return SHREWD_BASIC_ERR_OK;
        }
        if ((NULL == view->def_s)
            || (SHREWD_BASIC_ERR_OK
                != shrewd_basic_json_parse(view->def_s, strlen(view->def_s), &dom, NULL, NULL))) {
            return json_map_fail(ctx,
                                 SHREWD_BASIC_ERR_JSON_SYNTAX,
                                 "default for \"%s\" is not valid JSON",
                                 field->sb_refl_name);
        }
        status = json_decode_field(ctx, dom, field, object, depth, view);
        shrewd_basic_json_value_free(dom);
        return status;
    }
    if (NULL != view->ops) {
        return SHREWD_BASIC_ERR_OK;
    }
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
               | SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_BITS
               | SHREWD_BASIC_REFLECT_FIELD_F_UNION))) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_STRING == field->sb_refl_type->sb_refl_kind)
        && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))) {
        if ((SHREWD_BASIC_JSON_DEF_STR != view->def_kind) || (NULL == view->def_s)) {
            return SHREWD_BASIC_ERR_OK;
        }
        size_t length = strlen(view->def_s);
        if (length > field->sb_refl_count) {
            return json_map_fail(ctx,
                                 SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                 "default longer than %zu",
                                 field->sb_refl_count);
        }
        memcpy(at, view->def_s, length);
        if (length < field->sb_refl_count) {
            at[length] = '\0';
        }
        return SHREWD_BASIC_ERR_OK;
    }
    switch (field->sb_refl_type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        void *block = NULL;
        if ((SHREWD_BASIC_JSON_DEF_STR != view->def_kind) || (NULL == view->def_s)) {
            return SHREWD_BASIC_ERR_OK;
        }
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_calloc(&block, 1, strlen(view->def_s) + 1)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        memcpy(block, view->def_s, strlen(view->def_s) + 1);
        memcpy(at, &block, sizeof(block));
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        if (SHREWD_BASIC_JSON_DEF_NUM != view->def_kind) {
            return SHREWD_BASIC_ERR_OK;
        }
        reflect_store_u64(at, field->sb_refl_stride, (0 != view->def_u) ? 1u : 0u);
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        if (SHREWD_BASIC_JSON_DEF_NUM != view->def_kind) {
            return SHREWD_BASIC_ERR_OK;
        }
        if (4 == field->sb_refl_stride) {
            float narrowed = (float)view->def_d;
            memcpy(at, &narrowed, sizeof(narrowed));
        } else {
            memcpy(at, &view->def_d, sizeof(view->def_d));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        if (SHREWD_BASIC_JSON_DEF_NUM != view->def_kind) {
            return SHREWD_BASIC_ERR_OK;
        }
        {
            long double wide = (long double)view->def_d;
            memcpy(at, &wide, sizeof(wide));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        if (SHREWD_BASIC_JSON_DEF_NUM != view->def_kind) {
            return SHREWD_BASIC_ERR_OK;
        }
        reflect_store_u64(at, field->sb_refl_stride, view->def_u);
        return SHREWD_BASIC_ERR_OK;
    default:
        return SHREWD_BASIC_ERR_OK;
    }
}

static int json_decode_fields(json_map_ctx_t *ctx,
                              const shrewd_basic_json_value_t *value,
                              const shrewd_basic_reflect_field_t *fields,
                              size_t count,
                              unsigned char *object,
                              size_t depth) {
    if (0 != json_policy_collide(ctx->info, ctx->options, fields, count)) {
        return json_map_fail(ctx,
                             SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                             "loose key matching would collide between fields");
    }
    /* Pass 1: JSON members fill their fields. Key aliases are accepted
       (last occurrence wins), unknown keys are ignored (strict: error),
       count members stay derived. */
    for (size_t i = 0; i < shrewd_basic_json_count(value); ++i) {
        const char *key = shrewd_basic_json_key_at(value, i);
        const shrewd_basic_reflect_field_t *field
            = json_map_find(ctx->info, ctx->options, fields, count, key);
        shrewd_basic_json_key_info_t view = {0};
        shrewd_basic_json_value_t *member = NULL;
        size_t mark = 0;
        int status = 0;
        if (NULL == field) {
            if (0 != ctx->options->strict) {
                (void)json_map_fail(
                    ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "unknown key \"%s\"", key);
                return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
            }
            continue;
        }
        json_policy_view(ctx->info, field, &view);
        if ((0 != (view.flags & (SHREWD_BASIC_JSON_F_HIDDEN | SHREWD_BASIC_JSON_F_BORROWED)))
            || (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION))
            || (0 != json_map_is_count_member(fields, count, field))) {
            continue;
        }
        if (0 != ctx->options->strict) {
            for (size_t j = 0; j < i; ++j) {
                if (0
                    != json_map_key_matches(
                        ctx->info, ctx->options, field, shrewd_basic_json_key_at(value, j))) {
                    return json_map_fail(ctx,
                                         SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                         "conflicting keys for field \"%s\"",
                                         field->sb_refl_name);
                }
            }
        }
        member = shrewd_basic_json_at(value, i);
        mark = json_map_mark(ctx);
        json_map_member(ctx, field->sb_refl_name);
        /* Refill (duplicate keys/aliases within one document): release
           what decode put there earlier so "last wins" never leaks. The
           clean-target contract makes the field's content ours. */
        json_release_field(field, object, ctx->info);
        status = json_decode_field(ctx, member, field, object, depth, &view);
        if ((SHREWD_BASIC_ERR_OK == status) && (0 != (view.flags & SHREWD_BASIC_JSON_F_RANGE))
            && (SHREWD_BASIC_JSON_NUMBER == shrewd_basic_json_kind(member))) {
            double number = 0.0;
            (void)shrewd_basic_json_to_f64(member, &number);
            if ((number < view.min) || (number > view.max)) {
                status = json_map_fail(ctx,
                                       SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                       "value %g outside [%g, %g]",
                                       number,
                                       view.min,
                                       view.max);
            }
        }
        if ((SHREWD_BASIC_ERR_OK == status) && (NULL != view.ops) && (NULL != view.ops->check)
            && (0 == view.elem_ops)) {
            void *target = object + field->sb_refl_offset;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                memcpy(&target, target, sizeof(target));
            }
            status = view.ops->check(target);
            if (SHREWD_BASIC_ERR_OK != status) {
                (void)json_map_fail(ctx, status, "ops->check rejected the value");
            }
        }
        json_map_restore(ctx, mark);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    /* Pass 2: union bodies (the tag member is filled by pass 1). */
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        shrewd_basic_json_key_info_t view = {0};
        const shrewd_basic_json_value_t *member = NULL;
        const shrewd_basic_reflect_variant_t *variant = NULL;
        unsigned char *storage = NULL;
        size_t mark = 0;
        int status = 0;
        if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            continue;
        }
        json_policy_view(ctx->info, field, &view);
        if ((0 != (view.flags & (SHREWD_BASIC_JSON_F_HIDDEN | SHREWD_BASIC_JSON_F_BORROWED)))) {
            continue;
        }
        for (size_t j = 0; j < shrewd_basic_json_count(value); ++j) {
            if (0
                != json_map_key_matches(
                    ctx->info, ctx->options, field, shrewd_basic_json_key_at(value, j))) {
                member = shrewd_basic_json_at(value, j); /* last wins */
            }
        }
        if ((NULL == member) || (SHREWD_BASIC_JSON_NULL == shrewd_basic_json_kind(member))) {
            /* FLAT carries no body key at all - the variant's fields
               live in the parent object, so fall through to them. */
            if (SHREWD_BASIC_JSON_UNION_FLAT != view.union_style) {
                continue;
            }
            member = NULL;
        }
        storage = object + field->sb_refl_offset;
        variant = reflect_union_variant(field, storage);
        mark = json_map_mark(ctx);
        json_map_member(ctx, field->sb_refl_name);
        if (NULL == variant) {
            status = json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "union tag names no variant");
            json_map_restore(ctx, mark);
            return status;
        }
        if (SHREWD_BASIC_JSON_UNION_FLAT == view.union_style) {
            /* F4: the variant's fields live in the parent object. */
            status = json_decode_fields(ctx,
                                        value,
                                        variant->sb_refl_fields,
                                        variant->sb_refl_field_count,
                                        storage,
                                        depth + 1);
            (void)member;
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (SHREWD_BASIC_JSON_UNION_UNWRAP == view.union_style) {
            /* F2: a single-field variant carries the value directly. */
            shrewd_basic_json_key_info_t inner = {0};
            if (1 != variant->sb_refl_field_count) {
                json_map_restore(ctx, mark);
                return json_map_fail(ctx,
                                     SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                     "unwrap style needs a single-field variant");
            }
            json_policy_view(ctx->info, &variant->sb_refl_fields[0], &inner);
            status = json_decode_field(
                ctx, member, &variant->sb_refl_fields[0], storage, depth + 1, &inner);
            json_map_restore(ctx, mark);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        /* F1: the body object keys the variant's fields. */
        if (SHREWD_BASIC_JSON_OBJECT != shrewd_basic_json_kind(member)) {
            status = json_map_fail(
                ctx, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "expected an object or null");
        } else {
            status = json_decode_fields(ctx,
                                        member,
                                        variant->sb_refl_fields,
                                        variant->sb_refl_field_count,
                                        storage,
                                        depth + 1);
        }
        json_map_restore(ctx, mark);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    /* Pass 3: materialize policy defaults for missing keys
       (use_defaults); a JSON key or a materialized default satisfies
       REQUIRED. */
    for (size_t i = 0; i < count; ++i) {
        shrewd_basic_json_key_info_t view = {0};
        int present = 0;
        json_policy_view(ctx->info, &fields[i], &view);
        if ((0 != (view.flags & (SHREWD_BASIC_JSON_F_HIDDEN | SHREWD_BASIC_JSON_F_BORROWED)))
            || (0u != (fields[i].sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION))
            || (0 != json_map_is_count_member(fields, count, &fields[i]))) {
            continue;
        }
        for (size_t j = 0; j < shrewd_basic_json_count(value); ++j) {
            if (0
                != json_map_key_matches(
                    ctx->info, ctx->options, &fields[i], shrewd_basic_json_key_at(value, j))) {
                present = 1;
                break;
            }
        }
        if ((0 == present) && (0 != ctx->options->use_defaults)
            && (SHREWD_BASIC_JSON_DEF_NONE != view.def_kind)) {
            int status = json_decode_default(ctx, &fields[i], object, &view, depth);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            present = 1;
        }
        if ((0 == present) && (0 != (view.flags & SHREWD_BASIC_JSON_F_REQUIRED))) {
            size_t mark = json_map_mark(ctx);
            int status = 0;
            json_map_member(ctx, fields[i].sb_refl_name);
            status = json_map_fail(ctx, SHREWD_BASIC_ERR_KEY_NOTFOUND, "required key missing");
            json_map_restore(ctx, mark);
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_decode_value(const shrewd_basic_json_value_t *value,
                                   void *object,
                                   const shrewd_basic_reflect_type_t *type,
                                   const shrewd_basic_json_info_t *info,
                                   const shrewd_basic_json_options_t *options,
                                   shrewd_basic_json_error_t *error) {
    static const shrewd_basic_json_options_t plain = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    json_map_ctx_t ctx = {0};
    if ((NULL == value) || (NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_json_log,
                         "decode",
                         "decode: value, object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL != error) {
        error->offset = 0;
        error->path[0] = '\0';
        error->message[0] = '\0';
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = (NULL != options) ? options : &plain;
    ctx.info = info;
    ctx.error = error;
    if ((NULL != info) && (info->type != type)) {
        return json_map_fail(&ctx,
                             SHREWD_BASIC_ERR_ARG_INVALID,
                             "info describes \"%s\", not \"%s\"",
                             info->type->sb_refl_name,
                             type->sb_refl_name);
    }
    return json_decode_node(&ctx, value, type, (unsigned char *)object, type->sb_refl_size, 0);
}

int shrewd_basic_json_value_size(const shrewd_basic_json_value_t *value,
                                 const shrewd_basic_reflect_type_t *type,
                                 size_t *out_size) {
    if ((NULL == value) || (NULL == type) || (NULL == out_size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        *out_size = type->sb_refl_size;
        return SHREWD_BASIC_ERR_OK;
    }
    *out_size = json_target_size(value, type);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_decode(const char *text,
                             size_t length,
                             void *object,
                             const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_info_t *info,
                             const shrewd_basic_json_options_t *options,
                             shrewd_basic_json_error_t *error) {
    shrewd_basic_json_value_t *tree = NULL;
    int status = shrewd_basic_json_parse(text, length, &tree, options, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = shrewd_basic_json_decode_value(tree, object, type, info, options, error);
    shrewd_basic_json_value_free(tree);
    return status;
}

/* --- cleanup: ownership-gated release + json_free --------------------- */

/*
 * Only blocks the decode registry owns are freed: pre-set defaults -
 * static literals or caller allocations - are never touched. Targets
 * produced by an ops->decode hook are released through its destroy
 * callback instead (they are not registry-tracked).
 */
static void json_release_fields(const shrewd_basic_reflect_field_t *fields,
                                size_t count,
                                void *object,
                                const shrewd_basic_json_info_t *info);

static void json_release_pointer(const shrewd_basic_reflect_type_t *type,
                                 void (*destroy)(void *target),
                                 void *pointer,
                                 const shrewd_basic_json_info_t *info) {
    if (NULL == pointer) {
        return;
    }
    if (NULL != destroy) {
        destroy(pointer);
        return;
    }
    if (0 != reflect_kind_is_string(type->sb_refl_kind)) {
        /* A followed string: the pointee cell holds the content. */
        void *text = NULL;
        memcpy(&text, pointer, sizeof(text));
        shrewd_basic_platform_free(&text);
    } else if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind)
               || (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind)) {
        json_release_value(type, pointer, info);
    }
    (void)shrewd_basic_platform_free(&pointer);
}

static void json_release_value(const shrewd_basic_reflect_type_t *type,
                               void *value,
                               const shrewd_basic_json_info_t *info) {
    if (0 != reflect_kind_is_string(type->sb_refl_kind)) {
        void *text = NULL;
        memcpy(&text, value, sizeof(text));
        shrewd_basic_platform_free(&text);
        memset(value, 0, sizeof(void *));
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            json_release_value(
                type->sb_refl_elem, (unsigned char *)value + (i * type->sb_refl_elem_stride), info);
        }
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        json_release_fields(type->sb_refl_fields, type->sb_refl_field_count, value, info);
    }
}

static void json_release_field(const shrewd_basic_reflect_field_t *field,
                               void *object,
                               const shrewd_basic_json_info_t *info) {
    unsigned char *base = (unsigned char *)object;
    unsigned char *at = base + field->sb_refl_offset;
    shrewd_basic_json_key_info_t view = {0};
    void (*destroy)(void *target) = NULL;
    json_policy_view(info, field, &view);
    if (0 != (view.flags & SHREWD_BASIC_JSON_F_BORROWED)) {
        return; /* application-owned: never ours to release */
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
        const shrewd_basic_reflect_variant_t *variant = reflect_union_variant(field, at);
        if (NULL != variant) {
            json_release_fields(variant->sb_refl_fields, variant->sb_refl_field_count, at, info);
        }
        return;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        return;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        void *pointer = NULL;
        if (NULL != view.ops) {
            destroy = view.ops->destroy;
        }
        memcpy(&pointer, at, sizeof(pointer));
        json_release_pointer(field->sb_refl_type, destroy, pointer, info);
        memset(at, 0, sizeof(void *));
        return;
    }
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
        reflect_array_view_t slots = reflect_array_view(base, field);
        unsigned char *table = NULL;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
            if (NULL == slots.base) {
                return;
            }
            table = (unsigned char *)(uintptr_t)slots.base;
        }
        {
            void (*elem_destroy)(void *target) = NULL;
            if (0 != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                if ((NULL != view.ops) && (0 != view.elem_ops)) {
                    elem_destroy = view.ops->destroy;
                }
            }
            for (size_t j = 0; j < slots.count; ++j) {
                unsigned char *slot
                    = (NULL != table) ? (table + (j * slots.stride)) : (at + (j * slots.stride));
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    void *pointer = NULL;
                    memcpy(&pointer, slot, sizeof(pointer));
                    json_release_pointer(field->sb_refl_type, elem_destroy, pointer, info);
                    memset(slot, 0, sizeof(void *));
                } else {
                    json_release_value(field->sb_refl_type, slot, info);
                }
            }
        }
        if (NULL != table) {
            (void)shrewd_basic_platform_free((void **)&table);
            memset(at, 0, sizeof(void *));
        }
        return;
    }
    json_release_value(field->sb_refl_type, at, info);
}

static void json_release_fields(const shrewd_basic_reflect_field_t *fields,
                                size_t count,
                                void *object,
                                const shrewd_basic_json_info_t *info) {
    for (size_t i = 0; i < count; ++i) {
        json_release_field(&fields[i], object, info);
    }
}

/* ops->destroy owns untracked targets: run the hooks before the sweep. */
static void json_destroy_ops_fields(const shrewd_basic_reflect_field_t *fields,
                                    size_t count,
                                    void *object,
                                    const shrewd_basic_json_info_t *info) {
    unsigned char *base = (unsigned char *)object;
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        unsigned char *at = base + field->sb_refl_offset;
        shrewd_basic_json_key_info_t view = {0};
        void *pointer = NULL;
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            const shrewd_basic_reflect_variant_t *variant = reflect_union_variant(field, at);
            if (NULL != variant) {
                json_destroy_ops_fields(
                    variant->sb_refl_fields, variant->sb_refl_field_count, at, info);
            }
            continue;
        }
        if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            continue;
        }
        json_policy_view(info, field, &view);
        if ((0 != (view.flags & SHREWD_BASIC_JSON_F_BORROWED)) || (NULL == view.ops)
            || (NULL == view.ops->destroy)) {
            continue;
        }
        if ((0 != view.elem_ops)
            && (0u
                != (field->sb_refl_flags
                    & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                       | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)))) {
            reflect_array_view_t slots = reflect_array_view(base, field);
            for (size_t j = 0; j < slots.count; ++j) {
                unsigned char *slot = (unsigned char *)(uintptr_t)(slots.base + (j * slots.stride));
                if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    continue;
                }
                memcpy(&pointer, slot, sizeof(pointer));
                if (NULL != pointer) {
                    view.ops->destroy(pointer);
                    memset(slot, 0, sizeof(void *));
                }
            }
            continue;
        }
        memcpy(&pointer, at, sizeof(pointer));
        if (NULL != pointer) {
            view.ops->destroy(pointer);
            memset(at, 0, sizeof(void *));
        }
    }
}

int shrewd_basic_json_decode_pointer(const shrewd_basic_json_value_t *value,
                                     const char *pointer,
                                     void *object,
                                     const shrewd_basic_reflect_type_t *type,
                                     const shrewd_basic_json_info_t *info,
                                     const shrewd_basic_json_options_t *options,
                                     shrewd_basic_json_error_t *error) {
    const shrewd_basic_json_value_t *sub = NULL;
    int status = 0;
    if (NULL != error) {
        error->offset = 0;
        (void)snprintf(error->path, sizeof(error->path), "%s", (NULL != pointer) ? pointer : "");
        error->message[0] = '\0';
    }
    status = shrewd_basic_json_pointer(value, pointer, &sub);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return shrewd_basic_json_decode_value(sub, object, type, info, options, error);
}

int shrewd_basic_json_free(void *object,
                           const shrewd_basic_reflect_type_t *type,
                           const shrewd_basic_json_info_t *info) {
    if ((NULL == object) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_json_log,
                         "free",
                         "free: object or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    json_destroy_ops_fields(type->sb_refl_fields, type->sb_refl_field_count, object, info);
    json_release_fields(type->sb_refl_fields, type->sb_refl_field_count, object, info);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_json_log,
                     "free",
                     "free ok (%s)\n",
                     type->sb_refl_name);
    return SHREWD_BASIC_ERR_OK;
}

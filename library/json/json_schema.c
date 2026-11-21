#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/* --- JSON Pointer (RFC 6901) ------------------------------------------ */

int shrewd_basic_json_pointer(const shrewd_basic_json_value_t *root,
                              const char *pointer,
                              const shrewd_basic_json_value_t **out) {
    const shrewd_basic_json_value_t *cur = root;
    const char *p = pointer;
    if ((NULL == root) || (NULL == pointer) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if ('\0' == *p) {
        *out = root;
        return SHREWD_BASIC_ERR_OK;
    }
    if ('/' != *p) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    ++p;
    for (;;) {
        char segment[256] = {0};
        size_t n = 0;
        while (('\0' != *p) && ('/' != *p)) {
            if ('~' == *p) {
                ++p;
                if (('0' != *p) && ('1' != *p)) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                segment[n++] = ('0' == *p) ? '~' : '/';
                ++p;
            } else {
                segment[n++] = *p++;
            }
            if (n >= sizeof(segment)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
        }
        segment[n] = '\0';
        if (SHREWD_BASIC_JSON_ARRAY == shrewd_basic_json_kind(cur)) {
            size_t index = 0;
            if ('\0' == segment[0]) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            for (size_t i = 0; i < n; ++i) {
                if (('0' > segment[i]) || ('9' < segment[i])) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                index = (index * 10) + (size_t)(segment[i] - '0');
            }
            cur = shrewd_basic_json_at(cur, index);
        } else if (SHREWD_BASIC_JSON_OBJECT == shrewd_basic_json_kind(cur)) {
            cur = shrewd_basic_json_get(cur, segment);
        } else {
            cur = NULL;
        }
        if (NULL == cur) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        if ('\0' == *p) {
            break;
        }
        ++p; /* '/' */
    }
    *out = cur;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_pointer_set(shrewd_basic_json_value_t *root,
                                  const char *pointer,
                                  shrewd_basic_json_value_t *item) {
    shrewd_basic_json_value_t *cur = root;
    const char *p = pointer;
    char segment[256] = {0};
    char last[256] = {0};
    size_t n = 0;
    if ((NULL == root) || (NULL == pointer) || (NULL == item)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ('/' != *p) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    ++p;
    for (;;) {
        /* Decode one segment into "segment"; "last" remembers the one
           before the cursor advances (the final step applies the set). */
        n = 0;
        while (('\0' != *p) && ('/' != *p)) {
            if ('~' == *p) {
                ++p;
                if (('0' != *p) && ('1' != *p)) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                segment[n++] = ('0' == *p) ? '~' : '/';
                ++p;
            } else {
                segment[n++] = *p++;
            }
            if (n >= sizeof(segment)) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
        }
        segment[n] = '\0';
        if ('\0' == *p) {
            memcpy(last, segment, n + 1);
            break;
        }
        ++p;
        /* Intermediate step: descend into a container. */
        if (SHREWD_BASIC_JSON_ARRAY == shrewd_basic_json_kind(cur)) {
            size_t index = 0;
            if ('\0' == segment[0]) {
                return SHREWD_BASIC_ERR_ARG_INVALID;
            }
            for (size_t i = 0; i < n; ++i) {
                if (('0' > segment[i]) || ('9' < segment[i])) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                index = (index * 10) + (size_t)(segment[i] - '0');
            }
            cur = shrewd_basic_json_at(cur, index);
        } else if (SHREWD_BASIC_JSON_OBJECT == shrewd_basic_json_kind(cur)) {
            cur = shrewd_basic_json_get(cur, segment);
        } else {
            cur = NULL;
        }
        if (NULL == cur) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
    }
    /* Apply the last step. */
    if (SHREWD_BASIC_JSON_OBJECT == shrewd_basic_json_kind(cur)) {
        return shrewd_basic_json_set(cur, last, item);
    }
    if (SHREWD_BASIC_JSON_ARRAY == shrewd_basic_json_kind(cur)) {
        if (0 == strcmp("-", last)) {
            return shrewd_basic_json_append(cur, item);
        }
        {
            size_t index = 0;
            shrewd_basic_json_value_t *old = NULL;
            for (size_t i = 0; last[i] != '\0'; ++i) {
                if (('0' > last[i]) || ('9' < last[i])) {
                    return SHREWD_BASIC_ERR_ARG_INVALID;
                }
                index = (index * 10) + (size_t)(last[i] - '0');
            }
            old = shrewd_basic_json_at(cur, index);
            if (NULL == old) {
                return SHREWD_BASIC_ERR_KEY_NOTFOUND;
            }
            /* Replace in place: drop the old value, keep the slot. */
            return shrewd_basic_json_replace_at(cur, index, item);
        }
    }
    return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
}

/* --- JSON Schema export ----------------------------------------------- */

typedef struct schema_ctx {
    shrewd_basic_json_value_t *defs;
} schema_ctx_t;

static int schema_type(schema_ctx_t *ctx,
                       const shrewd_basic_reflect_type_t *type,
                       const shrewd_basic_json_info_t *info,
                       shrewd_basic_json_value_t **out);
static int schema_value(schema_ctx_t *ctx,
                        const shrewd_basic_reflect_field_t *field,
                        const shrewd_basic_json_info_t *info,
                        shrewd_basic_json_value_t **out);

/* A plain {"type": ...} node with optional RANGE bounds. */
static int schema_scalar(const shrewd_basic_reflect_type_t *type,
                         const shrewd_basic_json_key_info_t *view,
                         shrewd_basic_json_value_t **out) {
    const char *name = "number";
    shrewd_basic_json_value_t *node = NULL;
    shrewd_basic_json_value_t *kind = NULL;
    int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &node);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        name = "boolean";
        break;
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
        name = "integer";
        break;
    case SHREWD_BASIC_REFLECT_KIND_STRING:
        name = "string";
        break;
    default:
        break;
    }
    (void)shrewd_basic_json_new_string(name, &kind);
    (void)shrewd_basic_json_set(node, "type", kind);
    if ((NULL != view) && (0 != (view->flags & SHREWD_BASIC_JSON_F_RANGE))) {
        shrewd_basic_json_value_t *low = NULL;
        shrewd_basic_json_value_t *high = NULL;
        (void)shrewd_basic_json_new_f64(view->min, &low);
        (void)shrewd_basic_json_set(node, "minimum", low);
        (void)shrewd_basic_json_new_f64(view->max, &high);
        (void)shrewd_basic_json_set(node, "maximum", high);
    }
    if (SHREWD_BASIC_REFLECT_KIND_ENUM == type->sb_refl_kind) {
        shrewd_basic_json_value_t *list = NULL;
        shrewd_basic_json_value_t *as = NULL;
        (void)shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &list);
        for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
            shrewd_basic_json_value_t *one = NULL;
            (void)shrewd_basic_json_new_string(type->sb_refl_enum[i].sb_refl_name, &one);
            (void)shrewd_basic_json_append(list, one);
        }
        (void)shrewd_basic_json_set(node, "enum", list);
        (void)shrewd_basic_json_new_string("string", &as);
        (void)shrewd_basic_json_set(node, "type", as);
    }
    *out = node;
    return SHREWD_BASIC_ERR_OK;
}

/* oneOf of the union's variants (the F1 body shapes). */
static int schema_union(schema_ctx_t *ctx,
                        const shrewd_basic_reflect_field_t *field,
                        const shrewd_basic_json_info_t *info,
                        shrewd_basic_json_value_t **out) {
    const shrewd_basic_reflect_type_t *type = field->sb_refl_type;
    shrewd_basic_json_key_info_t view = {0};
    shrewd_basic_json_value_t *wrapper = NULL;
    shrewd_basic_json_value_t *cases = NULL;
    int status = 0;
    json_policy_view(info, field, &view);
    status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &wrapper);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (SHREWD_BASIC_JSON_UNION_NESTED != view.union_style) {
        /* Record the style: FLAT lifts the variant fields to the parent
           level, UNWRAP carries the value directly. */
        shrewd_basic_json_value_t *style = NULL;
        (void)shrewd_basic_json_new_string(
            (SHREWD_BASIC_JSON_UNION_FLAT == view.union_style) ? "flat" : "unwrap", &style);
        (void)shrewd_basic_json_set(wrapper, "x-union-style", style);
    }
    status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &cases);
    for (size_t v = 0; (SHREWD_BASIC_ERR_OK == status) && (v < type->sb_refl_variant_count); ++v) {
        const shrewd_basic_reflect_variant_t *variant = &type->sb_refl_variants[v];
        shrewd_basic_json_value_t *body = NULL;
        shrewd_basic_json_value_t *props = NULL;
        if ((SHREWD_BASIC_JSON_UNION_UNWRAP == view.union_style)
            && (1 == variant->sb_refl_field_count)) {
            /* Unwrapped variant: the body is the bare value. */
            shrewd_basic_json_value_t *bare = NULL;
            status = schema_value(ctx, &variant->sb_refl_fields[0], info, &bare);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = shrewd_basic_json_append(cases, bare);
            } else {
                shrewd_basic_json_value_free(bare);
            }
            continue;
        }
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &body);
        if (SHREWD_BASIC_ERR_OK == status) {
            shrewd_basic_json_value_t *kind = NULL;
            (void)shrewd_basic_json_new_string("object", &kind);
            (void)shrewd_basic_json_set(body, "type", kind);
            status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &props);
        }
        for (size_t f = 0; (SHREWD_BASIC_ERR_OK == status) && (f < variant->sb_refl_field_count);
             ++f) {
            shrewd_basic_json_value_t *one = NULL;
            status = schema_value(ctx, &variant->sb_refl_fields[f], info, &one);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = shrewd_basic_json_set(props, variant->sb_refl_fields[f].sb_refl_name, one);
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            /* Variant fields are required in their case so oneOf can
               discriminate between overlapping shapes. */
            shrewd_basic_json_value_t *need = NULL;
            (void)shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &need);
            for (size_t f = 0; f < variant->sb_refl_field_count; ++f) {
                shrewd_basic_json_value_t *mark = NULL;
                (void)shrewd_basic_json_new_string(variant->sb_refl_fields[f].sb_refl_name, &mark);
                (void)shrewd_basic_json_append(need, mark);
            }
            (void)shrewd_basic_json_set(body, "required", need);
            status = shrewd_basic_json_set(body, "properties", props);
        } else {
            shrewd_basic_json_value_free(props);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_append(cases, body);
        } else {
            shrewd_basic_json_value_free(body);
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_json_set(wrapper, "oneOf", cases);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_json_value_free(cases);
        shrewd_basic_json_value_free(wrapper);
        return status;
    }
    *out = wrapper;
    return SHREWD_BASIC_ERR_OK;
}

/* Schema of one member value (pointers nullable, arrays nested). */
static int schema_value_inner(schema_ctx_t *ctx,
                              const shrewd_basic_reflect_field_t *field,
                              const shrewd_basic_json_info_t *info,
                              const shrewd_basic_json_key_info_t *view,
                              shrewd_basic_json_value_t **out) {
    const shrewd_basic_reflect_type_t *type = field->sb_refl_type;
    shrewd_basic_json_value_t *inner = NULL;
    int status = 0;
    *out = NULL;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        shrewd_basic_json_value_t *kind = NULL;
        shrewd_basic_json_value_t *max = NULL;
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &inner);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        (void)shrewd_basic_json_new_string("string", &kind);
        (void)shrewd_basic_json_set(inner, "type", kind);
        (void)shrewd_basic_json_new_u64(field->sb_refl_count, &max);
        (void)shrewd_basic_json_set(inner, "maxLength", max);
    } else if (0u
               != (field->sb_refl_flags
                   & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
        shrewd_basic_json_value_t *items = NULL;
        shrewd_basic_json_value_t *kind = NULL;
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &inner);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        (void)shrewd_basic_json_new_string("array", &kind);
        (void)shrewd_basic_json_set(inner, "type", kind);
        if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
            shrewd_basic_json_value_t *max = NULL;
            (void)shrewd_basic_json_new_u64(field->sb_refl_count, &max);
            (void)shrewd_basic_json_set(inner, "maxItems", max);
        }
        status = schema_type(ctx, type, info, &items);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_set(inner, "items", items);
        }
    } else if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind)
               || (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind)) {
        status = schema_type(ctx, type, info, &inner);
    } else {
        status = schema_scalar(type, view, &inner);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_json_value_free(inner);
        return status;
    }
    if (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        *out = inner;
        return SHREWD_BASIC_ERR_OK;
    }
    {
        /* Pointer fields also accept null. */
        shrewd_basic_json_value_t *either = NULL;
        shrewd_basic_json_value_t *cases = NULL;
        shrewd_basic_json_value_t *nothing = NULL;
        shrewd_basic_json_value_t *kind = NULL;
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &either);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &cases);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_append(cases, inner);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &nothing);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            (void)shrewd_basic_json_new_string("null", &kind);
            (void)shrewd_basic_json_set(nothing, "type", kind);
            status = shrewd_basic_json_append(cases, nothing);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_set(either, "anyOf", cases);
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(either);
            shrewd_basic_json_value_free(cases);
            shrewd_basic_json_value_free(nothing);
            shrewd_basic_json_value_free(inner);
            return status;
        }
        *out = either;
        return SHREWD_BASIC_ERR_OK;
    }
}

static int schema_value(schema_ctx_t *ctx,
                        const shrewd_basic_reflect_field_t *field,
                        const shrewd_basic_json_info_t *info,
                        shrewd_basic_json_value_t **out) {
    shrewd_basic_json_key_info_t view = {0};
    json_policy_view(info, field, &view);
    return schema_value_inner(ctx, field, info, &view, out);
}

/*
 * Schema of one type: structs become $defs entries with $ref aliases
 * (cycles stay finite), rows nest as arrays, scalars use type names.
 */
static int schema_type(schema_ctx_t *ctx,
                       const shrewd_basic_reflect_type_t *type,
                       const shrewd_basic_json_info_t *info,
                       shrewd_basic_json_value_t **out) {
    *out = NULL;
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        char path[128] = {0};
        shrewd_basic_json_value_t *schema = NULL;
        shrewd_basic_json_value_t *props = NULL;
        shrewd_basic_json_value_t *required = NULL;
        int status = 0;
        if (NULL != shrewd_basic_json_get(ctx->defs, type->sb_refl_name)) {
            (void)snprintf(path, sizeof(path), "#/$defs/%s", type->sb_refl_name);
            return shrewd_basic_json_new_string(path, out);
        }
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &schema);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        /* Register before recursing: self-references hit the $ref path. */
        status = shrewd_basic_json_set(ctx->defs, type->sb_refl_name, schema);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(schema);
            return status;
        }
        {
            shrewd_basic_json_value_t *kind = NULL;
            (void)shrewd_basic_json_new_string("object", &kind);
            (void)shrewd_basic_json_set(schema, "type", kind);
        }
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &props);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &required);
        }
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_field_count);
             ++i) {
            const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
            shrewd_basic_json_key_info_t view = {0};
            shrewd_basic_json_value_t *one = NULL;
            json_policy_view(info, field, &view);
            if (0 != (view.flags & SHREWD_BASIC_JSON_F_HIDDEN)) {
                continue;
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
                status = schema_union(ctx, field, info, &one);
            } else {
                status = schema_value_inner(ctx, field, info, &view, &one);
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                /* Every accepted key except the canonical is an alias -
                   including the member name decode always accepts. */
                shrewd_basic_json_value_t *aliases = NULL;
                int extra = (0 != strcmp(view.keys[0], field->sb_refl_name)) ? 1 : 0;
                if ((size_t)(1 + extra) <= view.key_count) {
                    (void)shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &aliases);
                    for (size_t k = 1; k < view.key_count; ++k) {
                        shrewd_basic_json_value_t *alias = NULL;
                        (void)shrewd_basic_json_new_string(view.keys[k], &alias);
                        (void)shrewd_basic_json_append(aliases, alias);
                    }
                    if (0 != extra) {
                        shrewd_basic_json_value_t *alias = NULL;
                        (void)shrewd_basic_json_new_string(field->sb_refl_name, &alias);
                        (void)shrewd_basic_json_append(aliases, alias);
                    }
                    if (0 < shrewd_basic_json_count(aliases)) {
                        (void)shrewd_basic_json_set(one, "x-aliases", aliases);
                    } else {
                        shrewd_basic_json_value_free(aliases);
                    }
                }
            }
            if ((SHREWD_BASIC_ERR_OK == status) && (NULL != view.format)) {
                shrewd_basic_json_value_t *fmt = NULL;
                (void)shrewd_basic_json_new_string(view.format, &fmt);
                (void)shrewd_basic_json_set(one, "format", fmt);
            }
            if ((SHREWD_BASIC_ERR_OK == status) && (NULL != view.pattern)) {
                shrewd_basic_json_value_t *pat = NULL;
                (void)shrewd_basic_json_new_string(view.pattern, &pat);
                (void)shrewd_basic_json_set(one, "pattern", pat);
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                status = shrewd_basic_json_set(props, view.keys[0], one);
            } else {
                shrewd_basic_json_value_free(one);
            }
            if ((SHREWD_BASIC_ERR_OK == status)
                && (0 != (view.flags & SHREWD_BASIC_JSON_F_REQUIRED))) {
                shrewd_basic_json_value_t *mark = NULL;
                (void)shrewd_basic_json_new_string(view.keys[0], &mark);
                status = shrewd_basic_json_append(required, mark);
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_set(schema, "properties", props);
        } else {
            shrewd_basic_json_value_free(props);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            if (0 < shrewd_basic_json_count(required)) {
                status = shrewd_basic_json_set(schema, "required", required);
            } else {
                shrewd_basic_json_value_free(required);
            }
        } else {
            shrewd_basic_json_value_free(required);
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        (void)snprintf(path, sizeof(path), "#/$defs/%s", type->sb_refl_name);
        return shrewd_basic_json_new_string(path, out);
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        /* Row of a multi-dimension array: nested arrays. */
        shrewd_basic_json_value_t *node = NULL;
        shrewd_basic_json_value_t *kind = NULL;
        shrewd_basic_json_value_t *items = NULL;
        shrewd_basic_json_value_t *max = NULL;
        int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &node);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        (void)shrewd_basic_json_new_string("array", &kind);
        (void)shrewd_basic_json_set(node, "type", kind);
        (void)shrewd_basic_json_new_u64(type->sb_refl_elem_count, &max);
        (void)shrewd_basic_json_set(node, "maxItems", max);
        status = schema_type(ctx, type->sb_refl_elem, NULL, &items);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = shrewd_basic_json_set(node, "items", items);
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(node);
            return status;
        }
        *out = node;
        return SHREWD_BASIC_ERR_OK;
    }
    return schema_scalar(type, NULL, out);
}

int shrewd_basic_json_schema(const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_info_t *info,
                             char **out_text,
                             const shrewd_basic_json_options_t *options,
                             shrewd_basic_json_error_t *error) {
    schema_ctx_t ctx = {0};
    shrewd_basic_json_value_t *root = NULL;
    shrewd_basic_json_value_t *ref = NULL;
    shrewd_basic_json_value_t *uri = NULL;
    shrewd_basic_json_value_t *title = NULL;
    char path[128] = {0};
    int status = 0;
    if ((NULL == type) || (NULL == out_text)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((NULL != info) && (info->type != type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_text = NULL;
    if (NULL != error) {
        error->offset = 0;
        error->path[0] = '\0';
        error->message[0] = '\0';
    }
    status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &ctx.defs);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = schema_type(&ctx, type, info, &ref);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &root);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        /* A self-contained document: top-level $ref + the $defs bodies. */
        (void)shrewd_basic_json_new_string("http://json-schema.org/draft-07/schema#", &uri);
        (void)shrewd_basic_json_set(root, "$schema", uri);
        (void)shrewd_basic_json_new_string(type->sb_refl_name, &title);
        (void)shrewd_basic_json_set(root, "title", title);
        status = shrewd_basic_json_set(root, "$ref", ref);
    } else {
        shrewd_basic_json_value_free(ref);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_json_set(root, "$defs", ctx.defs);
    } else {
        shrewd_basic_json_value_free(ctx.defs);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_json_serialize(root, out_text, options, error);
    }
    shrewd_basic_json_value_free(root);
    (void)snprintf(path, sizeof(path), "%s", type->sb_refl_name);
    shrewd_basic_log(
        SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_json_log, "schema", "schema ok (%s)\n", path);
    return status;
}

/* --- schema validation ------------------------------------------------ */

/*
 * Validates a document against a schema produced by
 * shrewd_basic_json_schema() - the supported keywords are exactly the
 * emitted set (type/enum/minimum/maximum/maxLength/maxItems/required/
 * properties/items/oneOf/anyOf/$ref into $defs), so "exported schemas
 * always validate". format/pattern pass through unvalidated.
 */
typedef struct schema_check_ctx {
    const shrewd_basic_json_value_t *root;
    shrewd_basic_json_error_t *error;
    char path[256];
    size_t path_len;
} schema_check_ctx_t;

static int schema_check_value(schema_check_ctx_t *ctx,
                              const shrewd_basic_json_value_t *doc,
                              const shrewd_basic_json_value_t *schema);

/* Numeric bound in a schema keyword (0 when absent). */
static double schema_number(const shrewd_basic_json_value_t *node, const char *key) {
    double value = 0.0;
    const shrewd_basic_json_value_t *item = shrewd_basic_json_get(node, key);
    if (NULL != item) {
        (void)shrewd_basic_json_to_f64(item, &value);
    }
    return value;
}

/* Count bound in a schema keyword (0 when absent). */
static size_t
schema_count_bound(const shrewd_basic_json_value_t *node, const char *key, int *present) {
    uint64_t value = 0;
    const shrewd_basic_json_value_t *item = shrewd_basic_json_get(node, key);
    *present = (NULL != item) ? 1 : 0;
    if (NULL != item) {
        (void)shrewd_basic_json_to_u64(item, &value);
    }
    return (size_t)value;
}

static void schema_check_push(schema_check_ctx_t *ctx, const char *text) {
    size_t room = (sizeof(ctx->path) - 1) - ctx->path_len;
    size_t used = strlen(text);
    if (used > room) {
        used = room;
    }
    memcpy(ctx->path + ctx->path_len, text, used);
    ctx->path_len += used;
    ctx->path[ctx->path_len] = '\0';
}

static int schema_check_fail(schema_check_ctx_t *ctx, const char *fmt, ...) {
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
    return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
}

static int schema_value_equals(const shrewd_basic_json_value_t *a,
                               const shrewd_basic_json_value_t *b) {
    if (shrewd_basic_json_kind(a) != shrewd_basic_json_kind(b)) {
        return 0;
    }
    switch (shrewd_basic_json_kind(a)) {
    case SHREWD_BASIC_JSON_STRING:
        return (0 == strcmp(shrewd_basic_json_to_string(a), shrewd_basic_json_to_string(b))) ? 1
                                                                                             : 0;
    case SHREWD_BASIC_JSON_BOOL: {
        bool x = false;
        bool y = true;
        (void)shrewd_basic_json_to_bool(a, &x);
        (void)shrewd_basic_json_to_bool(b, &y);
        return (x == y) ? 1 : 0;
    }
    case SHREWD_BASIC_JSON_NUMBER: {
        double x = 0.0;
        double y = 1.0;
        (void)shrewd_basic_json_to_f64(a, &x);
        (void)shrewd_basic_json_to_f64(b, &y);
        return (x == y) ? 1 : 0;
    }
    default:
        return 1; /* containers are not compared by value here */
    }
}

static const shrewd_basic_json_value_t *schema_resolve(schema_check_ctx_t *ctx,
                                                       const shrewd_basic_json_value_t *schema) {
    const char *ref = NULL;
    const shrewd_basic_json_value_t *defs = NULL;
    if (SHREWD_BASIC_JSON_STRING != shrewd_basic_json_kind(shrewd_basic_json_get(schema, "$ref"))) {
        return schema;
    }
    ref = shrewd_basic_json_to_string(shrewd_basic_json_get(schema, "$ref"));
    defs = shrewd_basic_json_get(ctx->root, "$defs");
    if ((NULL == defs) || (0 != strncmp(ref, "#/$defs/", 8))) {
        return NULL;
    }
    return shrewd_basic_json_get(defs, ref + 8);
}

/*
 * The document member matching a property: its canonical key or any
 * x-alias (validation honors the same alias set decode does).
 */
static const shrewd_basic_json_value_t *schema_doc_member(const shrewd_basic_json_value_t *doc,
                                                          const char *canonical,
                                                          const shrewd_basic_json_value_t *sub) {
    const shrewd_basic_json_value_t *member = shrewd_basic_json_get(doc, canonical);
    const shrewd_basic_json_value_t *aliases = NULL;
    if (NULL != member) {
        return member;
    }
    aliases = (NULL != sub) ? shrewd_basic_json_get(sub, "x-aliases") : NULL;
    if (NULL != aliases) {
        for (size_t i = 0; i < shrewd_basic_json_count(aliases); ++i) {
            member = shrewd_basic_json_get(
                doc, shrewd_basic_json_to_string(shrewd_basic_json_at(aliases, i)));
            if (NULL != member) {
                return member;
            }
        }
    }
    return NULL;
}

/* The property whose canonical key or x-alias matches @p key. */
static const shrewd_basic_json_value_t *
schema_find_property(const shrewd_basic_json_value_t *properties, const char *key) {
    for (size_t p = 0; p < shrewd_basic_json_count(properties); ++p) {
        const shrewd_basic_json_value_t *sub = shrewd_basic_json_at(properties, p);
        const shrewd_basic_json_value_t *aliases = NULL;
        if (0 == strcmp(shrewd_basic_json_key_at(properties, p), key)) {
            return sub;
        }
        aliases = shrewd_basic_json_get(sub, "x-aliases");
        for (size_t a = 0; (NULL != aliases) && (a < shrewd_basic_json_count(aliases)); ++a) {
            if (0 == strcmp(shrewd_basic_json_to_string(shrewd_basic_json_at(aliases, a)), key)) {
                return sub;
            }
        }
    }
    return NULL;
}

static int schema_check_object(schema_check_ctx_t *ctx,
                               const shrewd_basic_json_value_t *doc,
                               const shrewd_basic_json_value_t *schema) {
    const shrewd_basic_json_value_t *required = shrewd_basic_json_get(schema, "required");
    const shrewd_basic_json_value_t *properties = shrewd_basic_json_get(schema, "properties");
    if (NULL != required) {
        for (size_t i = 0; i < shrewd_basic_json_count(required); ++i) {
            const char *key = shrewd_basic_json_to_string(shrewd_basic_json_at(required, i));
            if (NULL == schema_doc_member(doc, key, shrewd_basic_json_get(properties, key))) {
                size_t mark = ctx->path_len;
                schema_check_push(ctx, "/");
                schema_check_push(ctx, key);
                {
                    int status = schema_check_fail(ctx, "required key missing");
                    ctx->path_len = mark;
                    ctx->path[mark] = '\0';
                    return status;
                }
            }
        }
    }
    if (NULL != properties) {
        for (size_t i = 0; i < shrewd_basic_json_count(doc); ++i) {
            const char *key = shrewd_basic_json_key_at(doc, i);
            const shrewd_basic_json_value_t *sub = schema_find_property(properties, key);
            size_t mark = 0;
            int status = 0;
            if (NULL == sub) {
                continue; /* unknown keys are ignored, like decode */
            }
            mark = ctx->path_len;
            schema_check_push(ctx, "/");
            schema_check_push(ctx, key);
            status = schema_check_value(ctx, shrewd_basic_json_at(doc, i), sub);
            ctx->path_len = mark;
            ctx->path[mark] = '\0';
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

static int schema_check_value(schema_check_ctx_t *ctx,
                              const shrewd_basic_json_value_t *doc,
                              const shrewd_basic_json_value_t *schema) {
    const shrewd_basic_json_value_t *node = schema_resolve(ctx, schema);
    const shrewd_basic_json_value_t *one = NULL;
    const shrewd_basic_json_value_t *kind = NULL;
    if (NULL == node) {
        return schema_check_fail(ctx, "cannot resolve $ref");
    }
    one = shrewd_basic_json_get(node, "oneOf");
    if (NULL == one) {
        one = shrewd_basic_json_get(node, "anyOf");
    }
    if (NULL != one) {
        size_t matches = 0;
        int strict = (NULL != shrewd_basic_json_get(node, "oneOf"));
        for (size_t i = 0; i < shrewd_basic_json_count(one); ++i) {
            schema_check_ctx_t probe = *ctx;
            probe.error = NULL;
            if (SHREWD_BASIC_ERR_OK
                == schema_check_value(&probe, doc, shrewd_basic_json_at(one, i))) {
                ++matches;
            }
        }
        if ((0 != strict) ? (1 != matches) : (0 == matches)) {
            return schema_check_fail(
                ctx, strict ? "oneOf matched %zu variants" : "anyOf matched nothing", matches);
        }
        return SHREWD_BASIC_ERR_OK;
    }
    kind = shrewd_basic_json_get(node, "type");
    if (NULL != kind) {
        const char *want = shrewd_basic_json_to_string(kind);
        const char *have = "null";
        switch (shrewd_basic_json_kind(doc)) {
        case SHREWD_BASIC_JSON_BOOL:
            have = "boolean";
            break;
        case SHREWD_BASIC_JSON_NUMBER: {
            int64_t integral = 0;
            if (SHREWD_BASIC_ERR_OK == shrewd_basic_json_to_i64(doc, &integral)) {
                have = "integer";
            } else {
                have = "number";
            }
            break;
        }
        case SHREWD_BASIC_JSON_STRING:
            have = "string";
            break;
        case SHREWD_BASIC_JSON_ARRAY:
            have = "array";
            break;
        case SHREWD_BASIC_JSON_OBJECT:
            have = "object";
            break;
        default:
            break;
        }
        if (0 != strcmp(want, have)) {
            /* "number" accepts integers too. */
            if (!((0 == strcmp(want, "number")) && (0 == strcmp(have, "integer")))) {
                return schema_check_fail(ctx, "expected %s, got %s", want, have);
            }
        }
    }
    {
        const shrewd_basic_json_value_t *list = shrewd_basic_json_get(node, "enum");
        if (NULL != list) {
            int hit = 0;
            for (size_t i = 0; i < shrewd_basic_json_count(list); ++i) {
                if (0 != schema_value_equals(doc, shrewd_basic_json_at(list, i))) {
                    hit = 1;
                    break;
                }
            }
            if (0 == hit) {
                return schema_check_fail(ctx, "value not in enum");
            }
        }
    }
    if (SHREWD_BASIC_JSON_NUMBER == shrewd_basic_json_kind(doc)) {
        double value = 0.0;
        (void)shrewd_basic_json_to_f64(doc, &value);
        if ((NULL != shrewd_basic_json_get(node, "minimum"))
            && (value < schema_number(node, "minimum"))) {
            return schema_check_fail(ctx, "below minimum");
        }
        if ((NULL != shrewd_basic_json_get(node, "maximum"))
            && (value > schema_number(node, "maximum"))) {
            return schema_check_fail(ctx, "above maximum");
        }
    }
    if (SHREWD_BASIC_JSON_STRING == shrewd_basic_json_kind(doc)) {
        int present = 0;
        size_t bound = schema_count_bound(node, "maxLength", &present);
        if ((0 != present) && (strlen(shrewd_basic_json_to_string(doc)) > bound)) {
            return schema_check_fail(ctx, "string longer than maxLength");
        }
    }
    if (SHREWD_BASIC_JSON_ARRAY == shrewd_basic_json_kind(doc)) {
        const shrewd_basic_json_value_t *items = shrewd_basic_json_get(node, "items");
        int present = 0;
        size_t bound = schema_count_bound(node, "maxItems", &present);
        if ((0 != present) && (shrewd_basic_json_count(doc) > bound)) {
            return schema_check_fail(ctx, "more elements than maxItems");
        }
        if (NULL != items) {
            for (size_t i = 0; i < shrewd_basic_json_count(doc); ++i) {
                char step[24] = {0};
                size_t mark = ctx->path_len;
                int status = 0;
                (void)snprintf(step, sizeof(step), "/%zu", i);
                schema_check_push(ctx, step);
                status = schema_check_value(ctx, shrewd_basic_json_at(doc, i), items);
                ctx->path_len = mark;
                ctx->path[mark] = '\0';
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
        }
    }
    if (SHREWD_BASIC_JSON_OBJECT == shrewd_basic_json_kind(doc)) {
        return schema_check_object(ctx, doc, node);
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_schema_check(const shrewd_basic_json_value_t *document,
                                   const shrewd_basic_json_value_t *schema,
                                   shrewd_basic_json_error_t *error) {
    schema_check_ctx_t ctx = {0};
    if ((NULL == document) || (NULL == schema)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL != error) {
        error->offset = 0;
        error->path[0] = '\0';
        error->message[0] = '\0';
    }
    ctx.root = schema;
    ctx.error = error;
    ctx.path[0] = '\0';
    ctx.path_len = 0;
    return schema_check_value(&ctx, document, schema);
}

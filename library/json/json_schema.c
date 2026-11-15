#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/* --- JSON Pointer (RFC 6901) ------------------------------------------ */

int shrewd_basic_json_pointer(const shrewd_basic_json_value_t *root,
                              const char *pointer,
                              const shrewd_basic_json_value_t **out)
{
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
        char segment[256];
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

/* --- JSON Schema export ----------------------------------------------- */

typedef struct schema_ctx {
    shrewd_basic_json_value_t *defs;
} schema_ctx_t;

static int schema_type(schema_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                       const shrewd_basic_json_info_t *info,
                       shrewd_basic_json_value_t **out);
static int schema_value(schema_ctx_t *ctx,
                        const shrewd_basic_reflect_field_t *field,
                        const shrewd_basic_json_info_t *info,
                        shrewd_basic_json_value_t **out);

/* A plain {"type": ...} node with optional RANGE bounds. */
static int schema_scalar(const shrewd_basic_reflect_type_t *type,
                         const shrewd_basic_json_key_info_t *view,
                         shrewd_basic_json_value_t **out)
{
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
            (void)shrewd_basic_json_new_string(
                    type->sb_refl_enum[i].sb_refl_name, &one);
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
                        shrewd_basic_json_value_t **out)
{
    const shrewd_basic_reflect_type_t *type = field->sb_refl_type;
    shrewd_basic_json_value_t *wrapper = NULL;
    shrewd_basic_json_value_t *cases = NULL;
    int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &wrapper);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &cases);
    for (size_t v = 0; (SHREWD_BASIC_ERR_OK == status)
            && (v < type->sb_refl_variant_count); ++v) {
        const shrewd_basic_reflect_variant_t *variant = &type->sb_refl_variants[v];
        shrewd_basic_json_value_t *body = NULL;
        shrewd_basic_json_value_t *props = NULL;
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &body);
        if (SHREWD_BASIC_ERR_OK == status) {
            shrewd_basic_json_value_t *kind = NULL;
            (void)shrewd_basic_json_new_string("object", &kind);
            (void)shrewd_basic_json_set(body, "type", kind);
            status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &props);
        }
        for (size_t f = 0; (SHREWD_BASIC_ERR_OK == status)
                && (f < variant->sb_refl_field_count); ++f) {
            shrewd_basic_json_value_t *one = NULL;
            status = schema_value(ctx, &variant->sb_refl_fields[f], info, &one);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = shrewd_basic_json_set(
                        props, variant->sb_refl_fields[f].sb_refl_name, one);
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
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
                              shrewd_basic_json_value_t **out)
{
    const shrewd_basic_reflect_type_t *type = field->sb_refl_type;
    shrewd_basic_json_value_t *inner = NULL;
    int status;
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
    } else if (0u != (field->sb_refl_flags
                      & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                         | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
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
                        shrewd_basic_json_value_t **out)
{
    shrewd_basic_json_key_info_t view;
    json_policy_view(info, field, &view);
    return schema_value_inner(ctx, field, info, &view, out);
}

/*
 * Schema of one type: structs become $defs entries with $ref aliases
 * (cycles stay finite), rows nest as arrays, scalars use type names.
 */
static int schema_type(schema_ctx_t *ctx, const shrewd_basic_reflect_type_t *type,
                       const shrewd_basic_json_info_t *info,
                       shrewd_basic_json_value_t **out)
{
    *out = NULL;
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        char path[128];
        shrewd_basic_json_value_t *schema = NULL;
        shrewd_basic_json_value_t *props = NULL;
        shrewd_basic_json_value_t *required = NULL;
        int status;
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
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status)
                && (i < type->sb_refl_field_count); ++i) {
            const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
            shrewd_basic_json_key_info_t view;
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
            if ((SHREWD_BASIC_ERR_OK == status) && (1 < view.key_count)) {
                shrewd_basic_json_value_t *aliases = NULL;
                (void)shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &aliases);
                for (size_t k = 1; k < view.key_count; ++k) {
                    shrewd_basic_json_value_t *alias = NULL;
                    (void)shrewd_basic_json_new_string(view.keys[k], &alias);
                    (void)shrewd_basic_json_append(aliases, alias);
                }
                (void)shrewd_basic_json_set(one, "x-aliases", aliases);
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
                             shrewd_basic_json_error_t *error)
{
    schema_ctx_t ctx;
    shrewd_basic_json_value_t *root = NULL;
    shrewd_basic_json_value_t *ref = NULL;
    shrewd_basic_json_value_t *uri = NULL;
    shrewd_basic_json_value_t *title = NULL;
    char path[128];
    int status;
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
        (void)shrewd_basic_json_new_string(
                "http://json-schema.org/draft-07/schema#", &uri);
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
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_json_log,
                     "schema", "schema ok (%s)\n", path);
    return status;
}

#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * Policy runtime: merges the per-field rows of a SHREWD_BASIC_JSON_DEFINE
 * table into one view per member (later rows win per aspect) and backs
 * the json_keys() schema export. Policy only shapes text - the
 * structural truth stays in the reflect descriptor.
 */

void json_policy_view(const shrewd_basic_json_info_t *info,
                      const shrewd_basic_reflect_field_t *field,
                      shrewd_basic_json_key_info_t *out)
{
    out->field = field->sb_refl_name;
    out->keys = &field->sb_refl_name;
    out->key_count = 1;
    out->flags = 0;
    out->min = 0.0;
    out->max = 0.0;
    out->ops = NULL;
    out->elem_ops = 0;
    out->union_style = SHREWD_BASIC_JSON_UNION_NESTED;
    out->format = NULL;
    out->pattern = NULL;
    out->def_kind = SHREWD_BASIC_JSON_DEF_NONE;
    out->def_u = 0;
    out->def_d = 0.0;
    out->def_s = NULL;
    if (NULL == info) {
        return;
    }
    for (size_t i = 0; i < info->field_count; ++i) {
        const shrewd_basic_json_field_policy_t *row = &info->fields[i];
        if (0 != strcmp(row->field, field->sb_refl_name)) {
            continue;
        }
        out->flags |= row->flags;
        if (NULL != row->keys) {
            out->keys = row->keys;
            out->key_count = row->key_count;
        }
        if (0 != (row->flags & SHREWD_BASIC_JSON_F_RANGE)) {
            out->min = row->min;
            out->max = row->max;
        }
        if (NULL != row->ops) {
            out->ops = row->ops;
            out->elem_ops = row->elem_ops;
        }
        if (0 != row->has_union_style) {
            out->union_style = row->union_style;
        }
        if (NULL != row->format) {
            out->format = row->format;
        }
        if (NULL != row->pattern) {
            out->pattern = row->pattern;
        }
        if (SHREWD_BASIC_JSON_DEF_NONE != row->def_kind) {
            out->def_kind = row->def_kind;
            out->def_u = row->def_u;
            out->def_d = row->def_d;
            out->def_s = row->def_s;
        }
    }
}

int shrewd_basic_json_keys(const shrewd_basic_reflect_type_t *type,
                           const shrewd_basic_json_info_t *info,
                           shrewd_basic_json_keys_fn fn, void *ctx)
{
    if ((NULL == type) || (NULL == fn)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_json_log,
                         "policy", "keys: type or fn is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL != info) {
        if (info->type != type) {
            shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_json_log,
                             "policy", "keys: info describes \"%s\", not \"%s\"\n",
                             info->type->sb_refl_name, type->sb_refl_name);
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        type = info->type;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        shrewd_basic_json_key_info_t view = {0};
        json_policy_view(info, &type->sb_refl_fields[i], &view);
        if (0 != fn(ctx, &view)) {
            return SHREWD_BASIC_ERR_OK;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

/*
 * Caller-hosted lookup lists: entry primitives and registration.
 *
 * The library owns only the per-type sorted field index (a derived,
 * rebuildable view of the const descriptor); the container - storage,
 * data structure, keys, locking - belongs to the application and is
 * reached through the shrewd_basic_reflect_list_ops_t callbacks. No
 * global state lives here.
 */

#include <string.h>

#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

int shrewd_basic_reflect_lookup_init(shrewd_basic_reflect_lookup_t *look,
                                     const shrewd_basic_reflect_type_t *type,
                                     const char *name)
{
    if ((NULL == look) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "lookup", "lookup_init: look or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(look, 0, sizeof(*look));
    look->sb_refl_name = (NULL != name) ? name : type->sb_refl_name;
    look->sb_refl_type = type;
    if ((SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_LOOKUP_FIELDS < type->sb_refl_field_count)) {
        return SHREWD_BASIC_ERR_OK; /* sb_refl_count stays 0: no index. */
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        size_t j = i;
        while ((0 < j) && (0 > strcmp(field->sb_refl_name,
                                      look->sb_refl_sorted[j - 1]->sb_refl_name))) {
            look->sb_refl_sorted[j] = look->sb_refl_sorted[j - 1];
            --j;
        }
        look->sb_refl_sorted[j] = field;
    }
    look->sb_refl_count = type->sb_refl_field_count;
    return SHREWD_BASIC_ERR_OK;
}

const shrewd_basic_reflect_field_t *shrewd_basic_reflect_lookup_find(
        const shrewd_basic_reflect_lookup_t *look, const char *name,
        size_t name_len)
{
    size_t low = 0;
    size_t high = 0;
    if ((NULL == look) || (NULL == name)) {
        return NULL;
    }
    high = look->sb_refl_count;
    while (low < high) {
        size_t mid = low + ((high - low) / 2);
        const char *field_name = look->sb_refl_sorted[mid]->sb_refl_name;
        size_t field_len = strlen(field_name);
        size_t shared = (name_len < field_len) ? name_len : field_len;
        int result = memcmp(name, field_name, shared);
        if (0 == result) {
            result = (name_len < field_len) ? -1 : ((name_len > field_len) ? 1 : 0);
        }
        if (0 == result) {
            return look->sb_refl_sorted[mid];
        }
        if (0 > result) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    return NULL;
}

int shrewd_basic_reflect_list_register(const shrewd_basic_reflect_list_t *list,
                                       const shrewd_basic_reflect_type_t *type,
                                       const char *name)
{
    shrewd_basic_reflect_lookup_t *slot = NULL;
    int status = 0;
    if ((NULL == list) || (NULL == list->sb_refl_ops)
            || (NULL == list->sb_refl_ops->insert) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "lookup", "list_register: list, ops or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = list->sb_refl_ops->insert(
            list->sb_refl_ctx,
            (NULL != name) ? name : type->sb_refl_name, type, &slot);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (NULL == slot) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "lookup", "list_register: insert gave no entry slot\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    return shrewd_basic_reflect_lookup_init(slot, type, name);
}

/**
 * @file env.c
 * @brief Environment variables and command-line arguments -> struct
 *        (leaves through the shared text layer).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/env.h"
#include "shrewd-basic/platform/memory.h"

/* APP_ + log_level -> APP_LOG_LEVEL (length into buf). */
static int env_name(char *buf, size_t cap, const char *prefix, const char *member) {
    size_t i = 0;
    size_t at = 0;
    size_t n = (NULL != prefix) ? strlen(prefix) : 0;
    if ((n + strlen(member) + 1) > cap) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memcpy(buf, prefix, n);
    at = n;
    for (i = 0; '\0' != member[i]; ++i) {
        char c = member[i];
        if (('a' <= c) && ('z' >= c)) {
            c = (char)(c - 'a' + 'A');
        }
        buf[at] = c;
        ++at;
    }
    buf[at] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static int env_apply(void *object,
                     const shrewd_basic_reflect_type_t *type,
                     const char *prefix,
                     const char *name,
                     size_t name_len,
                     const char *value,
                     size_t value_len) {
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        char want[256];
        if (SHREWD_BASIC_ERR_OK != env_name(want, sizeof(want), prefix, field->sb_refl_name)) {
            continue;
        }
        if ((strlen(want) != name_len) || (0 != memcmp(want, name, name_len))) {
            continue;
        }
        if ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR))
            || ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY))
                && (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)))
            || (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind)) {
            return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
        }
        return shrewd_basic_reflect_scan_text(
            field, (unsigned char *)object + field->sb_refl_offset, value, value_len);
    }
    return SHREWD_BASIC_ERR_OK; /* unknown names are ignored */
}

int shrewd_basic_env_scan(void *object,
                          const shrewd_basic_reflect_type_t *type,
                          const char *prefix) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        char want[256];
        const char *value = NULL;
        int status = 0;
        if (SHREWD_BASIC_ERR_OK != env_name(want, sizeof(want), prefix, field->sb_refl_name)) {
            continue;
        }
        value = getenv(want);
        if (NULL == value) {
            continue; /* absent names leave the field untouched */
        }
        status = env_apply(object, type, prefix, want, strlen(want), value, strlen(value));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_args_scan(void *object,
                           const shrewd_basic_reflect_type_t *type,
                           const char *prefix,
                           int argc,
                           char **argv) {
    if ((NULL == object) || (NULL == type) || ((0 > argc) || ((0 != argc) && (NULL == argv)))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (int i = 0; i < argc; ++i) {
        const char *entry = argv[i];
        const char *name = entry;
        const char *eq = NULL;
        int status = 0;
        if ((NULL == entry) || ('-' == entry[0])) {
            if ((NULL == entry) || ('-' != entry[0]) || ('-' != entry[1]) || ('\0' == entry[2])) {
                continue;
            }
            name = entry + 2; /* --NAME=VALUE */
        }
        eq = strchr(name, '=');
        if (NULL == eq) {
            continue;
        }
        status = env_apply(object, type, prefix, name, (size_t)(eq - name), eq + 1, strlen(eq + 1));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_env_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        unsigned char *at = (unsigned char *)object + field->sb_refl_offset;
        if ((SHREWD_BASIC_REFLECT_KIND_STRING == field->sb_refl_type->sb_refl_kind)
            && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED))) {
            void *pointer = NULL;
            memcpy(&pointer, at, sizeof(pointer));
            if (NULL != pointer) {
                (void)shrewd_basic_platform_free(&pointer);
                memset(at, 0, sizeof(void *));
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

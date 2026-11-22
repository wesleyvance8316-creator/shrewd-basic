/**
 * @file ini.c
 * @brief struct <-> INI text (sections = nested structs, keys = leaf
 *        fields through the shared text layer).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/ini.h"
#include "shrewd-basic/formats/json.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* --- helpers ----------------------------------------------------------- */

static void ini_fail(shrewd_basic_ini_error_t *error, size_t line, const char *what) {
    if (NULL != error) {
        error->line = line;
        snprintf(error->message, sizeof(error->message), "%s", what);
    }
}

static const char *trim(const char *s, size_t *len) {
    size_t n = *len;
    while ((0 < n) && ((' ' == s[0]) || ('\t' == s[0]))) {
        ++s;
        --n;
    }
    while ((0 < n) && ((' ' == s[n - 1]) || ('\t' == s[n - 1]))) {
        --n;
    }
    *len = n;
    return s;
}

/* One text buffer for encode output (grows by doubling). */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} ini_buf_t;

static int ini_buf_add(ini_buf_t *buf, const char *text, size_t n) {
    if ((buf->len + n + 1) > buf->cap) {
        size_t cap = (0 != buf->cap) ? buf->cap : 256;
        void *block = NULL;
        while (cap < (buf->len + n + 1)) {
            cap *= 2;
        }
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, cap)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        if (0 < buf->len) {
            memcpy(block, buf->data, buf->len);
        }
        if (NULL != buf->data) {
            void *old = buf->data;
            (void)shrewd_basic_platform_free(&old);
        }
        buf->data = block;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, text, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static int ini_buf_line(ini_buf_t *buf, const char *line) {
    int status = ini_buf_add(buf, line, strlen(line));
    if (SHREWD_BASIC_ERR_OK == status) {
        status = ini_buf_add(buf, "\n", 1);
    }
    return status;
}

/* Char text buffers count as leaves; arrays/pointers/aggregates do not. */
/* Merged policy aspects for one member (rows merge per aspect, later
   rows win - the json policy semantics). */
static void ini_policy_get(const shrewd_basic_json_info_t *info,
                           const char *member,
                           shrewd_basic_json_field_policy_t *out) {
    memset(out, 0, sizeof(*out));
    out->field = member;
    if ((NULL == info) || (NULL == member)) {
        return;
    }
    for (size_t i = 0; i < info->field_count; ++i) {
        const shrewd_basic_json_field_policy_t *row = &info->fields[i];
        if (0 != strcmp(row->field, member)) {
            continue;
        }
        if ((NULL != row->keys) && (0 < row->key_count)) {
            out->keys = row->keys;
            out->key_count = row->key_count;
        }
        out->flags |= row->flags;
    }
}

/* Canonical wire key: KEY row's first name, else the member name. */
static const char *ini_leaf_name(const shrewd_basic_json_field_policy_t *policy,
                                 const char *member) {
    if ((NULL != policy) && (NULL != policy->keys) && (0 < policy->key_count)) {
        return policy->keys[0];
    }
    return member;
}

/* The wire key matches the member name or any KEY alias. */
static int ini_key_matches(const shrewd_basic_json_field_policy_t *policy,
                           const char *member,
                           const char *name,
                           size_t len) {
    size_t n = strlen(member);
    if ((len == n) && (0 == memcmp(name, member, len))) {
        return 1;
    }
    if ((NULL != policy) && (NULL != policy->keys)) {
        for (size_t k = 0; k < policy->key_count; ++k) {
            n = strlen(policy->keys[k]);
            if ((len == n) && (0 == memcmp(name, policy->keys[k], len))) {
                return 1;
            }
        }
    }
    return 0;
}

/* Array-shaped field whose elements are text leaves (scalars/enums or
   owned strings); buffers, structs, wide strings and pointer arrays
   stay refused. */
static int ini_array_is_text(const shrewd_basic_reflect_field_t *field) {
    const shrewd_basic_reflect_type_t *elem = NULL;
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        return 0;
    }
    if (0u
        == (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
        return 0;
    }
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL | SHREWD_BASIC_REFLECT_FIELD_F_FLEX
               | SHREWD_BASIC_REFLECT_FIELD_F_PTR))) {
        return 0;
    }
    elem = field->sb_refl_type;
    switch (elem->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE:
        return 1;
    case SHREWD_BASIC_REFLECT_KIND_STRING:
        return (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED)) ? 1 : 0;
    default:
        return 0;
    }
}

/* Current element count of an array field (fixed or dynamic). */
static size_t ini_array_count(const shrewd_basic_reflect_field_t *field,
                              const unsigned char *object) {
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
        size_t count = 0;
        if (0 != field->sb_refl_limit_size) {
            uint64_t bound = 0;
            memcpy(&bound, object + field->sb_refl_limit_offset, field->sb_refl_limit_size);
            count = (size_t)bound;
        }
        return count;
    }
    return field->sb_refl_count;
}

static int ini_field_is_leaf(const shrewd_basic_reflect_field_t *field) {
    if ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))
        && (1 == field->sb_refl_stride)) {
        return 1;
    }
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
               | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY | SHREWD_BASIC_REFLECT_FIELD_F_BITS))) {
        return 0;
    }
    return (SHREWD_BASIC_REFLECT_KIND_STRUCT != field->sb_refl_type->sb_refl_kind) ? 1 : 0;
}

/* --- free (contents only - the root stays the caller's) ---------------- */

static int ini_free_fields(const shrewd_basic_reflect_type_t *type, void *object) {
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_OK;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        unsigned char *at = (unsigned char *)object + field->sb_refl_offset;
        if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind)
            && (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR))) {
            (void)ini_free_fields(field->sb_refl_type, at);
            continue;
        }
        if (0 != ini_array_is_text(field)) {
            /* Owned strings inside array elements, then the dynamic
               table itself (slots zeroed: cross-module free hand-off). */
            size_t count = ini_array_count(field, object);
            unsigned char *table = at;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                memcpy(&table, at, sizeof(table));
            }
            if ((SHREWD_BASIC_REFLECT_KIND_STRING == field->sb_refl_type->sb_refl_kind)
                && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED))
                && (NULL != table)) {
                for (size_t j = 0; j < count; ++j) {
                    void *pointer = NULL;
                    memcpy(&pointer, table + (j * field->sb_refl_stride), sizeof(pointer));
                    if (NULL != pointer) {
                        (void)shrewd_basic_platform_free(&pointer);
                        memset(table + (j * field->sb_refl_stride), 0, sizeof(pointer));
                    }
                }
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                void *old = table;
                (void)shrewd_basic_platform_free(&old);
                memset(at, 0, sizeof(void *));
            }
            continue;
        }
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

int shrewd_basic_ini_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    return ini_free_fields(type, object);
}

void shrewd_basic_ini_text_free(char *text) {
    if (NULL != text) {
        (void)shrewd_basic_platform_free((void **)&text);
    }
}

/* --- encode ------------------------------------------------------------ */

static int ini_encode_fields(ini_buf_t *buf,
                             const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_info_t *info,
                             const void *object,
                             char *path,
                             size_t path_len,
                             shrewd_basic_ini_error_t *error);

static int ini_encode_leaf(ini_buf_t *buf,
                           const shrewd_basic_reflect_field_t *field,
                           const void *at,
                           const char *line_key,
                           shrewd_basic_ini_error_t *error) {
    char value[256];
    char line[512];
    int status = shrewd_basic_reflect_render_text(field, at, value, sizeof(value), NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        ini_fail(error, 0, "field has no text form");
        return status;
    }
    snprintf(line, sizeof(line), "%s = %s", line_key, value);
    return ini_buf_line(buf, line);
}

static int ini_encode_fields(ini_buf_t *buf,
                             const shrewd_basic_reflect_type_t *type,
                             const shrewd_basic_json_info_t *info,
                             const void *object,
                             char *path,
                             size_t path_len,
                             shrewd_basic_ini_error_t *error) {
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        shrewd_basic_json_field_policy_t policy;
        const unsigned char *at = (const unsigned char *)object + field->sb_refl_offset;
        ini_policy_get(info, field->sb_refl_name, &policy);
        if (0u != (policy.flags & SHREWD_BASIC_JSON_F_HIDDEN)) {
            continue; /* HIDDEN never surfaces */
        }
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
            size_t used = path_len;
            int status = 0;
            if (0u
                != (field->sb_refl_flags
                    & (SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY
                       | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))) {
                ini_fail(error, 0, "pointer/array struct fields have no INI form");
                return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
            }
            if (0 < used) {
                path[used] = '.';
                ++used;
            }
            snprintf(path + used, 128 - used, "%s", field->sb_refl_name);
            {
                char header[192];
                snprintf(header, sizeof(header), "[%s]", path);
                status = ini_buf_line(buf, header);
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                status = ini_encode_fields(
                    buf, field->sb_refl_type, info, at, path, strlen(path), error);
            }
            path[path_len] = '\0';
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0 != ini_array_is_text(field)) {
            /* One key line per element (repeated keys). */
            const char *key = ini_leaf_name(&policy, field->sb_refl_name);
            size_t count = ini_array_count(field, object);
            const unsigned char *table = at;
            int status = 0;
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                memcpy(&table, at, sizeof(table));
            }
            for (size_t j = 0; (SHREWD_BASIC_ERR_OK == status) && (j < count); ++j) {
                status
                    = ini_encode_leaf(buf, field, table + (j * field->sb_refl_stride), key, error);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (0 == ini_field_is_leaf(field)) {
            ini_fail(error, 0, "only leaf fields map to INI keys");
            return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
        }
        {
            int status = ini_encode_leaf(
                buf, field, at, ini_leaf_name(&policy, field->sb_refl_name), error);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_ini_encode(const void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_json_info_t *info,
                            char **out_text,
                            const shrewd_basic_ini_options_t *options,
                            shrewd_basic_ini_error_t *error) {
    ini_buf_t buf;
    char path[128];
    int status = 0;
    (void)options;
    if ((NULL == object) || (NULL == type) || (NULL == out_text)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&buf, 0, sizeof(buf));
    path[0] = '\0';
    status = ini_encode_fields(&buf, type, info, object, path, 0, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_ini_text_free(buf.data);
        return status;
    }
    if (NULL == buf.data) {
        /* Empty struct: hand back a plain empty string. */
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&buf.data, 1)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        buf.data[0] = '\0';
    }
    *out_text = buf.data;
    return SHREWD_BASIC_ERR_OK;
}

/* --- decode ------------------------------------------------------------ */

/* Resolve a dotted section path to (type, storage) inside object. */
static int ini_resolve_section(const shrewd_basic_reflect_type_t *root_type,
                               void *root_object,
                               const char *path,
                               size_t path_len,
                               const shrewd_basic_reflect_type_t **out_type,
                               void **out_object) {
    const shrewd_basic_reflect_type_t *type = root_type;
    void *object = root_object;
    size_t start = 0;
    while (start < path_len) {
        size_t end = start;
        while ((end < path_len) && ('.' != path[end])) {
            ++end;
        }
        {
            char name[128];
            const shrewd_basic_reflect_field_t *member = NULL;
            if ((end - start) >= sizeof(name)) {
                return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
            }
            memcpy(name, path + start, end - start);
            name[end - start] = '\0';
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_type_find_field(type, name, &member)) {
                return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
            }
            if ((SHREWD_BASIC_REFLECT_KIND_STRUCT != member->sb_refl_type->sb_refl_kind)
                || (0u
                    != (member->sb_refl_flags
                        & (SHREWD_BASIC_REFLECT_FIELD_F_PTR
                           | SHREWD_BASIC_REFLECT_FIELD_F_ARRAY)))) {
                return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
            }
            object = (unsigned char *)object + member->sb_refl_offset;
            type = member->sb_refl_type;
        }
        start = end + 1;
    }
    *out_type = type;
    *out_object = object;
    return SHREWD_BASIC_ERR_OK;
}

/* REQUIRED rows naming members of @p type must have been seen. */
static int ini_required_flush(const shrewd_basic_json_info_t *info,
                              const shrewd_basic_reflect_type_t *type,
                              const void **seen,
                              size_t seen_count,
                              size_t line_no,
                              shrewd_basic_ini_error_t *error) {
    if ((NULL == info) || (NULL == type)) {
        return SHREWD_BASIC_ERR_OK;
    }
    for (size_t r = 0; r < info->field_count; ++r) {
        const shrewd_basic_json_field_policy_t *row = &info->fields[r];
        const shrewd_basic_reflect_field_t *member = NULL;
        int present = 0;
        if (0u == (row->flags & SHREWD_BASIC_JSON_F_REQUIRED)) {
            continue;
        }
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_reflect_type_find_field(type, row->field, &member)) {
            continue; /* not a member of this section */
        }
        for (size_t s = 0; s < seen_count; ++s) {
            if (seen[s] == member) {
                present = 1;
                break;
            }
        }
        if (0 == present) {
            ini_fail(error, line_no, "missing required key");
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_ini_decode(const char *text,
                            size_t length,
                            void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_json_info_t *info,
                            const shrewd_basic_ini_options_t *options,
                            shrewd_basic_ini_error_t *error) {
    const shrewd_basic_reflect_type_t *cur_type = NULL;
    void *cur_object = NULL;
    size_t pos = 0;
    size_t line_no = 0;
    struct {
        const shrewd_basic_reflect_field_t *field;
        size_t next;
    } fills[128];
    size_t fill_count = 0;
    const void *seen[128];
    size_t seen_count = 0;
    int status = 0;
    int strict = (NULL != options) ? options->strict : 0;
    if ((NULL == text) || (NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(object, 0, type->sb_refl_size);
    cur_type = type;
    cur_object = object;
    while (pos < length) {
        const char *line = text + pos;
        size_t line_len = 0;
        while (((pos + line_len) < length) && ('\n' != text[pos + line_len])) {
            ++line_len;
        }
        pos += line_len + ((pos + line_len) < length ? 1 : 0);
        ++line_no;
        {
            size_t n = line_len;
            const char *s = line;
            while ((0 < n) && (('\r' == s[n - 1]) || (' ' == s[n - 1]) || ('\t' == s[n - 1]))) {
                --n;
            }
            s = trim(s, &n);
            if ((0 == n) || (';' == s[0]) || ('#' == s[0])) {
                continue;
            }
            if ('[' == s[0]) {
                if ((']' != s[n - 1]) || (n < 2)) {
                    ini_fail(error, line_no, "bad section header");
                    return SHREWD_BASIC_ERR_JSON_SYNTAX;
                }
                status = ini_required_flush(info, cur_type, seen, seen_count, line_no, error);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                seen_count = 0;
                if (SHREWD_BASIC_ERR_OK
                    != ini_resolve_section(type, object, s + 1, n - 2, &cur_type, &cur_object)) {
                    ini_fail(error, line_no, "section names no struct field");
                    return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
                }
                continue;
            }
            {
                const char *eq = memchr(s, '=', n);
                const char *key = NULL;
                const char *val = NULL;
                size_t key_len = 0;
                size_t val_len = 0;
                const shrewd_basic_reflect_field_t *field = NULL;
                if (NULL == eq) {
                    ini_fail(error, line_no, "'=' expected");
                    return SHREWD_BASIC_ERR_JSON_SYNTAX;
                }
                key_len = (size_t)(eq - s);
                val_len = (size_t)((s + n) - (eq + 1));
                val = trim(eq + 1, &val_len);
                {
                    size_t unused = key_len;
                    key = trim(s, &unused);
                    key_len = unused;
                }
                /* Resolve by member name or KEY alias. */
                for (size_t f = 0; (NULL == field) && (f < cur_type->sb_refl_field_count); ++f) {
                    const shrewd_basic_reflect_field_t *cand = &cur_type->sb_refl_fields[f];
                    shrewd_basic_json_field_policy_t policy;
                    ini_policy_get(info, cand->sb_refl_name, &policy);
                    if (0 != ini_key_matches(&policy, cand->sb_refl_name, key, key_len)) {
                        field = cand;
                    }
                }
                if (NULL == field) {
                    if (0 != strict) {
                        ini_fail(error, line_no, "unknown key");
                        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
                    }
                    continue;
                }
                {
                    shrewd_basic_json_field_policy_t policy;
                    ini_policy_get(info, field->sb_refl_name, &policy);
                    if (0u != (policy.flags & SHREWD_BASIC_JSON_F_HIDDEN)) {
                        continue; /* HIDDEN keys are never decoded */
                    }
                }
                if (0 != ini_array_is_text(field)) {
                    /* Repeated keys append into the array. */
                    size_t next = 0;
                    unsigned char *table = (unsigned char *)cur_object + field->sb_refl_offset;
                    unsigned char *slot = NULL;
                    for (size_t f = 0; f < fill_count; ++f) {
                        if (fills[f].field == field) {
                            next = fills[f].next;
                            break;
                        }
                    }
                    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                        void *block = NULL;
                        memcpy(&block, table, sizeof(block));
                        if (SHREWD_BASIC_ERR_OK
                            != shrewd_basic_platform_realloc(
                                &block, block, (next + 1) * field->sb_refl_stride)) {
                            ini_fail(error, line_no, "out of memory");
                            return SHREWD_BASIC_ERR_MEM_NOMEM;
                        }
                        memcpy(table, &block, sizeof(block));
                        slot = (unsigned char *)block + (next * field->sb_refl_stride);
                    } else {
                        if (next >= field->sb_refl_count) {
                            ini_fail(error, line_no, "array longer than limit");
                            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
                        }
                        slot = table + (next * field->sb_refl_stride);
                    }
                    status = shrewd_basic_reflect_scan_text(field, slot, val, val_len);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        ini_fail(error, line_no, "value does not fit the element");
                        return status;
                    }
                    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)) {
                        uint64_t bound = (uint64_t)(next + 1);
                        memcpy((unsigned char *)cur_object + field->sb_refl_limit_offset,
                               &bound,
                               field->sb_refl_limit_size);
                    }
                    {
                        int tracked = 0;
                        for (size_t f = 0; f < fill_count; ++f) {
                            if (fills[f].field == field) {
                                fills[f].next = next + 1;
                                tracked = 1;
                                break;
                            }
                        }
                        if ((0 == tracked) && (fill_count < 128)) {
                            fills[fill_count].field = field;
                            fills[fill_count].next = next + 1;
                            fill_count += 1;
                        }
                    }
                    continue;
                }
                if (0 == ini_field_is_leaf(field)) {
                    ini_fail(error, line_no, "key does not name a leaf field");
                    return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
                }
                status = shrewd_basic_reflect_scan_text(
                    field, (unsigned char *)cur_object + field->sb_refl_offset, val, val_len);
                if (SHREWD_BASIC_ERR_OK != status) {
                    ini_fail(error, line_no, "value does not fit the field");
                    return status;
                }
                if ((seen_count < 128) && (NULL != info)) {
                    seen[seen_count] = field;
                    seen_count += 1;
                }
            }
        }
    }
    return ini_required_flush(info, cur_type, seen, seen_count, line_no, error);
}

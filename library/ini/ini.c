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
                             const void *object,
                             char *path,
                             size_t path_len,
                             shrewd_basic_ini_error_t *error) {
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const unsigned char *at = (const unsigned char *)object + field->sb_refl_offset;
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
                status = ini_encode_fields(buf, field->sb_refl_type, at, path, strlen(path), error);
            }
            path[path_len] = '\0';
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
            int status = ini_encode_leaf(buf, field, at, field->sb_refl_name, error);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_ini_encode(const void *object,
                            const shrewd_basic_reflect_type_t *type,
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
    status = ini_encode_fields(&buf, type, object, path, 0, error);
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

int shrewd_basic_ini_decode(const char *text,
                            size_t length,
                            void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_ini_options_t *options,
                            shrewd_basic_ini_error_t *error) {
    const shrewd_basic_reflect_type_t *cur_type = NULL;
    void *cur_object = NULL;
    size_t pos = 0;
    size_t line_no = 0;
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
                int status = 0;
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
                {
                    char name[128];
                    if (key_len >= sizeof(name)) {
                        ini_fail(error, line_no, "key too long");
                        return SHREWD_BASIC_ERR_JSON_SYNTAX;
                    }
                    memcpy(name, key, key_len);
                    name[key_len] = '\0';
                    status = shrewd_basic_reflect_type_find_field(cur_type, name, &field);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    if (0 != strict) {
                        ini_fail(error, line_no, "unknown key");
                        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
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
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

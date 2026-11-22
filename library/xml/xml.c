/**
 * @file xml.c
 * @brief struct <-> XML (strict subset) over the shared text layer.
 *        XXE-safe by construction: no DTD is ever accepted, so no
 *        external entity can ever be resolved.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic-internal/reflect-internal.h"
#include "shrewd-basic/formats/xml.h"
#include "shrewd-basic/platform/memory.h"

#define XML_MAX_DEPTH 64
#define XML_NAME_MAX 128

/* --- helpers -------------------------------------------------------------- */

static void xml_fail(shrewd_basic_xml_error_t *error, size_t line, const char *what) {
    if (NULL != error) {
        error->line = line;
        snprintf(error->message, sizeof(error->message), "%s", what);
    }
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} xml_buf_t;

static int xml_buf_add(xml_buf_t *buf, const char *text, size_t n) {
    if ((buf->len + n + 1) > buf->cap) {
        size_t cap = (0 != buf->cap) ? buf->cap : (size_t)256;
        void *block = NULL;
        while (cap < (buf->len + n + 1)) {
            cap *= 2;
        }
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, buf->data, cap)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        buf->data = block;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, text, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static int xml_buf_str(xml_buf_t *buf, const char *text) {
    return xml_buf_add(buf, text, strlen(text));
}

static int xml_buf_utf8(xml_buf_t *buf, uint32_t cp) {
    char u[4];
    size_t n = 0;
    if (cp < 0x80u) {
        u[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800u) {
        u[0] = (char)(0xC0u | (cp >> 6));
        u[1] = (char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp < 0x10000u) {
        u[0] = (char)(0xE0u | (cp >> 12));
        u[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[2] = (char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else {
        u[0] = (char)(0xF0u | (cp >> 18));
        u[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        u[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[3] = (char)(0x80u | (cp & 0x3Fu));
        n = 4;
    }
    return xml_buf_add(buf, u, n);
}

/* Escape text content (attr = 1 also escapes the quotes). */
static int xml_buf_escaped(xml_buf_t *buf, const char *text, size_t n, int attr) {
    for (size_t i = 0; i < n; ++i) {
        char c = text[i];
        const char *rep = NULL;
        switch (c) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = (0 != attr) ? "&quot;" : NULL;
            break;
        case '\'':
            rep = (0 != attr) ? "&apos;" : NULL;
            break;
        default:
            break;
        }
        if (NULL != rep) {
            int status = xml_buf_str(buf, rep);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        } else {
            int status = xml_buf_add(buf, &c, 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

/* --- policy lookup -------------------------------------------------------- */

static const shrewd_basic_xml_policy_t *xml_policy(const shrewd_basic_xml_policy_t *map,
                                                   size_t count,
                                                   const char *field) {
    for (size_t i = 0; i < count; ++i) {
        if ((NULL != map[i].field) && (0 == strcmp(map[i].field, field))) {
            return &map[i];
        }
    }
    return NULL;
}

static const char *xml_tag(const shrewd_basic_xml_policy_t *row, const char *field) {
    return ((NULL != row) && (NULL != row->name)) ? row->name : field;
}

/* --- leaf conversion (text layer + UTF converters) ------------------------- */

static void xml_leaf_field(shrewd_basic_reflect_field_t *tmp,
                           const shrewd_basic_reflect_type_t *type,
                           uint32_t flags,
                           size_t stride,
                           size_t count) {
    memset(tmp, 0, sizeof(*tmp));
    tmp->sb_refl_name = type->sb_refl_name;
    tmp->sb_refl_type = type;
    tmp->sb_refl_size = stride * count;
    tmp->sb_refl_stride = stride;
    tmp->sb_refl_count = count;
    tmp->sb_refl_flags = flags;
}

static int xml_wide_kind(shrewd_basic_reflect_kind_t kind) {
    return ((SHREWD_BASIC_REFLECT_KIND_STRING16 == kind)
            || (SHREWD_BASIC_REFLECT_KIND_STRING32 == kind))
               ? 1
               : 0;
}

static int xml_wide_render(const shrewd_basic_reflect_type_t *type,
                           size_t stride,
                           const void *value,
                           xml_buf_t *buf) {
    size_t unit = reflect_string_unit(type->sb_refl_kind);
    const void *text = value;
    size_t len = 0;
    char stack[128];
    char *out = stack;
    void *heap = NULL;
    int status = 0;
    if (stride != unit) {
        /* pointer string */
        text = reflect_load_ptr(value);
        if (NULL == text) {
            return xml_buf_str(buf, "");
        }
    }
    status = reflect_utf8_from_units(text, unit, SIZE_MAX, NULL, 0, &len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (len >= sizeof(stack)) {
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&heap, len + 1)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        out = heap;
    }
    status = reflect_utf8_from_units(text, unit, SIZE_MAX, out, len + 1, &len);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_escaped(buf, out, len, 0);
    }
    {
        void *old = heap;
        (void)shrewd_basic_platform_free(&old);
    }
    return status;
}

static int xml_wide_scan(const shrewd_basic_reflect_type_t *type,
                         size_t stride,
                         size_t count,
                         void *value,
                         const char *text,
                         size_t len) {
    size_t unit = reflect_string_unit(type->sb_refl_kind);
    size_t needed = 0;
    int status = reflect_units_from_utf8(text, len, unit, NULL, 0, &needed);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (stride == unit) {
        /* wide buffer: bounded by its unit count */
        if (needed > count) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        memset(value, 0, count * unit);
        return reflect_units_from_utf8(text, len, unit, value, count, &needed);
    }
    {
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, (needed + 1) * unit)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        memset(block, 0, (needed + 1) * unit);
        status = reflect_units_from_utf8(text, len, unit, block, needed + 1, &needed);
        if (SHREWD_BASIC_ERR_OK != status) {
            void *old = block;
            (void)shrewd_basic_platform_free(&old);
            return status;
        }
        memcpy(value, &block, sizeof(block));
        return SHREWD_BASIC_ERR_OK;
    }
}

/* --- field classification -------------------------------------------------- */

#define XML_F_LEAF 1
#define XML_F_STRUCT 2
#define XML_F_ARRAY 3
#define XML_F_PTR 4

static int xml_shape(const shrewd_basic_reflect_field_t *field) {
    uint32_t flags = field->sb_refl_flags;
    shrewd_basic_reflect_kind_t kind = field->sb_refl_type->sb_refl_kind;
    if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
        return XML_F_LEAF; /* char or wide text buffer */
    }
    if ((SHREWD_BASIC_REFLECT_KIND_STRING == kind) || (0 != xml_wide_kind(kind))) {
        return XML_F_LEAF;
    }
    if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        return XML_F_PTR;
    }
    if (0u
        != (flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_FLEX | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
               | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL | SHREWD_BASIC_REFLECT_FIELD_F_BITS
               | SHREWD_BASIC_REFLECT_FIELD_F_UNION))) {
        return 0;
    }
    if ((0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY))
        || (SHREWD_BASIC_REFLECT_KIND_ARRAY == kind)) {
        return XML_F_ARRAY;
    }
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE:
        return XML_F_LEAF;
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == kind) {
        return XML_F_STRUCT;
    }
    return 0;
}

static int xml_leaf_into(const shrewd_basic_reflect_type_t *type,
                         uint32_t flags,
                         size_t stride,
                         size_t count,
                         const void *value,
                         xml_buf_t *buf) {
    shrewd_basic_reflect_field_t tmp;
    size_t len = 0;
    char stack[128];
    char *out = stack;
    void *heap = NULL;
    int status = 0;
    if (0 != xml_wide_kind(type->sb_refl_kind)) {
        return xml_wide_render(type, stride, value, buf);
    }
    xml_leaf_field(&tmp, type, flags, stride, count);
    status = shrewd_basic_reflect_render_text(&tmp, value, NULL, 0, &len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (len >= sizeof(stack)) {
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&heap, len + 1)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        out = heap;
    }
    status = shrewd_basic_reflect_render_text(&tmp, value, out, len + 1, &len);
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_escaped(buf, out, len, 0);
    }
    {
        void *old = heap;
        (void)shrewd_basic_platform_free(&old);
    }
    return status;
}

static int xml_leaf_from(const shrewd_basic_reflect_type_t *type,
                         uint32_t flags,
                         size_t stride,
                         size_t count,
                         void *value,
                         const char *text,
                         size_t len) {
    shrewd_basic_reflect_field_t tmp;
    if (0 != xml_wide_kind(type->sb_refl_kind)) {
        return xml_wide_scan(type, stride, count, value, text, len);
    }
    if ((SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind)
        && (0u == (flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))) {
        flags |= SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED; /* decode allocates */
    }
    xml_leaf_field(&tmp, type, flags, stride, count);
    return shrewd_basic_reflect_scan_text(&tmp, value, text, len);
}

/* Leaf conversion of a real field (carries the buffer bound). */
static int xml_field_into(const shrewd_basic_reflect_field_t *field,
                          const void *value,
                          xml_buf_t *buf) {
    size_t count = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))
                       ? field->sb_refl_count
                       : 1;
    return xml_leaf_into(
        field->sb_refl_type, field->sb_refl_flags, field->sb_refl_stride, count, value, buf);
}

static int xml_field_from(const shrewd_basic_reflect_field_t *field,
                          void *value,
                          const char *text,
                          size_t len) {
    size_t count = (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))
                       ? field->sb_refl_count
                       : 1;
    return xml_leaf_from(
        field->sb_refl_type, field->sb_refl_flags, field->sb_refl_stride, count, value, text, len);
}

/* --- encode --------------------------------------------------------------- */

static int xml_encode_fields(xml_buf_t *buf,
                             const char *tag,
                             const shrewd_basic_reflect_type_t *type,
                             const void *object,
                             const shrewd_basic_xml_policy_t *map,
                             size_t map_count,
                             size_t depth);

static int xml_encode_text_tag(xml_buf_t *buf, const char *tag, xml_buf_t *text) {
    int status = xml_buf_str(buf, "<");
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, tag);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, ">");
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_add(buf, text->data, text->len);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, "</");
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, tag);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, ">");
    }
    return status;
}

static int xml_encode_node(xml_buf_t *buf,
                           const char *tag,
                           const shrewd_basic_reflect_type_t *type,
                           const void *value,
                           size_t stride,
                           const shrewd_basic_xml_policy_t *map,
                           size_t map_count,
                           size_t depth) {
    int status = 0;
    if (XML_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_JSON_DEPTH;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        return xml_encode_fields(buf, tag, type, value, map, map_count, depth + 1);
    }
    {
        xml_buf_t text;
        memset(&text, 0, sizeof(text));
        status = xml_leaf_into(type, 0, stride, 1, value, &text);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = xml_encode_text_tag(buf, tag, &text);
        }
        shrewd_basic_xml_text_free(text.data);
        return status;
    }
}

static int xml_encode_fields(xml_buf_t *buf,
                             const char *tag,
                             const shrewd_basic_reflect_type_t *type,
                             const void *object,
                             const shrewd_basic_xml_policy_t *map,
                             size_t map_count,
                             size_t depth) {
    int status = 0;
    int wrote_open = 0;
    if (XML_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_JSON_DEPTH;
    }
    status = xml_buf_str(buf, "<");
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, tag);
    }
    /* attributes first (leaves with as_attr) */
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_field_count); ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const shrewd_basic_xml_policy_t *row = xml_policy(map, map_count, field->sb_refl_name);
        const void *at = (const unsigned char *)object + field->sb_refl_offset;
        xml_buf_t val;
        if ((NULL == row) || (0 == row->as_attr) || (0u != (row->flags & SHREWD_BASIC_XML_HIDDEN))
            || (XML_F_LEAF != xml_shape(field))) {
            continue;
        }
        memset(&val, 0, sizeof(val));
        status = xml_field_into(field, at, &val);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = xml_buf_str(buf, " ");
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = xml_buf_str(buf, xml_tag(row, field->sb_refl_name));
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = xml_buf_str(buf, "=\"");
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = xml_buf_escaped(buf, val.data, val.len, 1);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = xml_buf_str(buf, "\"");
        }
        shrewd_basic_xml_text_free(val.data);
    }
    /* children */
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_field_count); ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const shrewd_basic_xml_policy_t *row = xml_policy(map, map_count, field->sb_refl_name);
        const unsigned char *at = (const unsigned char *)object + field->sb_refl_offset;
        const char *name = xml_tag(row, field->sb_refl_name);
        int shape = xml_shape(field);
        if ((NULL != row) && (0u != (row->flags & SHREWD_BASIC_XML_HIDDEN))) {
            continue;
        }
        if ((NULL != row) && (0 != row->as_attr)) {
            continue;
        }
        if (0 == shape) {
            return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
        }
        if (0 == wrote_open) {
            status = xml_buf_str(buf, ">");
            wrote_open = 1;
        }
        if (XML_F_LEAF == shape) {
            if ((SHREWD_BASIC_REFLECT_KIND_STRING == field->sb_refl_type->sb_refl_kind)
                && (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))
                && (NULL == reflect_load_ptr(at))) {
                continue; /* NULL string is omitted */
            }
            {
                xml_buf_t text;
                memset(&text, 0, sizeof(text));
                status = xml_field_into(field, at, &text);
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = xml_encode_text_tag(buf, name, &text);
                }
                shrewd_basic_xml_text_free(text.data);
            }
        } else if (XML_F_STRUCT == shape) {
            status
                = xml_encode_fields(buf, name, field->sb_refl_type, at, map, map_count, depth + 1);
        } else if (XML_F_ARRAY == shape) {
            for (size_t j = 0; (SHREWD_BASIC_ERR_OK == status) && (j < field->sb_refl_count); ++j) {
                status = xml_encode_node(buf,
                                         name,
                                         field->sb_refl_type,
                                         at + (j * field->sb_refl_stride),
                                         field->sb_refl_stride,
                                         map,
                                         map_count,
                                         depth + 1);
            }
        } else {
            const void *target = reflect_load_ptr(at);
            if (NULL == target) {
                continue; /* nil pointer is omitted */
            }
            status = xml_encode_node(buf,
                                     name,
                                     field->sb_refl_type,
                                     target,
                                     field->sb_refl_type->sb_refl_size,
                                     map,
                                     map_count,
                                     depth + 1);
        }
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 == wrote_open) {
        return xml_buf_str(buf, "/>");
    }
    status = xml_buf_str(buf, "</");
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, tag);
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = xml_buf_str(buf, ">");
    }
    return status;
}

int shrewd_basic_xml_encode(const void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_xml_policy_t *map,
                            size_t map_count,
                            char **out_text,
                            shrewd_basic_xml_error_t *error) {
    xml_buf_t buf;
    int status = 0;
    if ((NULL == object) || (NULL == type) || (NULL == out_text)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&buf, 0, sizeof(buf));
    status = xml_encode_fields(&buf, type->sb_refl_name, type, object, map, map_count, 0);
    if (SHREWD_BASIC_ERR_OK != status) {
        xml_fail(error, 0, "encode failed");
        shrewd_basic_xml_text_free(buf.data);
        return status;
    }
    if (NULL == buf.data) {
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&buf.data, 1)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        buf.data[0] = '\0';
    }
    *out_text = buf.data;
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_xml_text_free(char *text) {
    if (NULL != text) {
        (void)shrewd_basic_platform_free((void **)&text);
    }
}

/* --- parser --------------------------------------------------------------- */

typedef struct {
    const char *s;
    size_t len;
    size_t pos;
    size_t line;
    shrewd_basic_xml_error_t *error;
} xml_parser_t;

static int xml_syntax(xml_parser_t *p, const char *what) {
    xml_fail(p->error, p->line, what);
    return SHREWD_BASIC_ERR_JSON_SYNTAX;
}

static void xml_skip_ws(xml_parser_t *p) {
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if ('\n' == c) {
            ++p->line;
        }
        if ((' ' != c) && ('\t' != c) && ('\r' != c) && ('\n' != c)) {
            break;
        }
        ++p->pos;
    }
}

static int xml_starts(xml_parser_t *p, const char *prefix) {
    size_t n = strlen(prefix);
    return ((p->pos + n) <= p->len) && (0 == memcmp(p->s + p->pos, prefix, n));
}

static int xml_read_name(xml_parser_t *p, char *out, size_t cap) {
    size_t start = p->pos;
    size_t n = 0;
    if ((p->pos >= p->len)
        || (!(((p->s[p->pos] >= 'A') && (p->s[p->pos] <= 'Z'))
              || ((p->s[p->pos] >= 'a') && (p->s[p->pos] <= 'z')) || ('_' == p->s[p->pos])
              || (':' == p->s[p->pos])))) {
        return xml_syntax(p, "bad name start");
    }
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9'))
            || ('_' == c) || (':' == c) || ('.' == c) || ('-' == c)) {
            ++p->pos;
        } else {
            break;
        }
    }
    n = p->pos - start;
    if (n >= cap) {
        return xml_syntax(p, "name too long");
    }
    memcpy(out, p->s + start, n);
    out[n] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

/* Entity reference at '&'; appends the decoded scalar as UTF-8. */
static int xml_read_entity(xml_parser_t *p, xml_buf_t *out) {
    size_t i = p->pos + 1;
    uint32_t cp = 0;
    if ((i < p->len) && ('#' == p->s[i])) {
        ++i;
        if ((i < p->len) && (('x' == p->s[i]) || ('X' == p->s[i]))) {
            ++i;
            if (i >= p->len) {
                return xml_syntax(p, "bad character reference");
            }
            for (; i < p->len; ++i) {
                char c = p->s[i];
                uint32_t d = 0;
                if ((c >= '0') && (c <= '9')) {
                    d = (uint32_t)(c - '0');
                } else if ((c >= 'a') && (c <= 'f')) {
                    d = (uint32_t)(c - 'a') + 10u;
                } else if ((c >= 'A') && (c <= 'F')) {
                    d = (uint32_t)(c - 'A') + 10u;
                } else {
                    break;
                }
                cp = (cp << 4) | d;
            }
        } else {
            for (; i < p->len; ++i) {
                char c = p->s[i];
                if ((c < '0') || (c > '9')) {
                    break;
                }
                cp = (cp * 10u) + (uint32_t)(c - '0');
            }
        }
    } else {
        char name[8];
        size_t n = 0;
        while ((i < p->len) && (n < (sizeof(name) - 1)) && (';' != p->s[i])) {
            name[n] = p->s[i];
            ++n;
            ++i;
        }
        name[n] = '\0';
        if ((i >= p->len) || (';' != p->s[i])) {
            return xml_syntax(p, "bad entity reference");
        }
        if (0 == strcmp(name, "amp")) {
            cp = '&';
        } else if (0 == strcmp(name, "lt")) {
            cp = '<';
        } else if (0 == strcmp(name, "gt")) {
            cp = '>';
        } else if (0 == strcmp(name, "quot")) {
            cp = '"';
        } else if (0 == strcmp(name, "apos")) {
            cp = '\'';
        } else {
            return xml_syntax(p, "foreign entity reference");
        }
    }
    if ((i >= p->len) || (';' != p->s[i])) {
        return xml_syntax(p, "unterminated entity reference");
    }
    if ((0 == cp) || (cp > 0x10FFFFu) || ((cp >= 0xD800u) && (cp <= 0xDFFFu))) {
        return xml_syntax(p, "bad character reference");
    }
    p->pos = i + 1;
    return xml_buf_utf8(out, cp);
}

/* Character data until '<' (text + CDATA + entities). */
static int xml_read_text(xml_parser_t *p, xml_buf_t *out) {
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if ('<' == c) {
            if (xml_starts(p, "<![CDATA[")) {
                size_t start = 0;
                int status = 0;
                p->pos += 9;
                start = p->pos;
                for (;;) {
                    if ((p->pos + 3) > p->len) {
                        return xml_syntax(p, "unterminated CDATA");
                    }
                    if (0 == memcmp(p->s + p->pos, "]]>", 3)) {
                        status = xml_buf_add(out, p->s + start, p->pos - start);
                        p->pos += 3;
                        break;
                    }
                    if ('\n' == p->s[p->pos]) {
                        ++p->line;
                    }
                    ++p->pos;
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                continue;
            }
            return SHREWD_BASIC_ERR_OK;
        }
        if ('&' == c) {
            int status = xml_read_entity(p, out);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if ('\n' == c) {
            ++p->line;
        }
        {
            int status = xml_buf_add(out, &c, 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        ++p->pos;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Attribute value (after the opening quote) until the matching quote. */
static int xml_read_attr_value(xml_parser_t *p, xml_buf_t *out) {
    char quote = p->s[p->pos];
    ++p->pos;
    while ((p->pos < p->len) && (quote != p->s[p->pos])) {
        char c = p->s[p->pos];
        if ('&' == c) {
            int status = xml_read_entity(p, out);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if ('<' == c) {
            return xml_syntax(p, "'<' in attribute value");
        }
        if ('\n' == c) {
            ++p->line;
        }
        {
            int status = xml_buf_add(out, &c, 1);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        ++p->pos;
    }
    if (p->pos >= p->len) {
        return xml_syntax(p, "unterminated attribute value");
    }
    ++p->pos; /* closing quote */
    return SHREWD_BASIC_ERR_OK;
}

static int xml_skip_comment(xml_parser_t *p) {
    p->pos += 4;
    for (;;) {
        if ((p->pos + 3) > p->len) {
            return xml_syntax(p, "unterminated comment");
        }
        if (0 == memcmp(p->s + p->pos, "-->", 3)) {
            p->pos += 3;
            return SHREWD_BASIC_ERR_OK;
        }
        if ('\n' == p->s[p->pos]) {
            ++p->line;
        }
        ++p->pos;
    }
}

/* Skip one whole element subtree (used for unknown children). */
static int xml_skip_element(xml_parser_t *p) {
    char name[XML_NAME_MAX];
    char seen[8][XML_NAME_MAX];
    size_t seen_n = 0;
    int status = 0;
    ++p->pos; /* '<' */
    status = xml_read_name(p, name, sizeof(name));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (;;) {
        char attr[XML_NAME_MAX];
        xml_buf_t val;
        xml_skip_ws(p);
        if (p->pos >= p->len) {
            return xml_syntax(p, "unterminated tag");
        }
        if (xml_starts(p, "/>")) {
            p->pos += 2;
            return SHREWD_BASIC_ERR_OK;
        }
        if ('>' == p->s[p->pos]) {
            ++p->pos;
            break;
        }
        status = xml_read_name(p, attr, sizeof(attr));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        for (size_t i = 0; i < seen_n; ++i) {
            if (0 == strcmp(seen[i], attr)) {
                return xml_syntax(p, "duplicate attribute");
            }
        }
        if (seen_n < (sizeof(seen) / sizeof(seen[0]))) {
            snprintf(seen[seen_n], sizeof(seen[0]), "%s", attr);
            ++seen_n;
        }
        xml_skip_ws(p);
        if ((p->pos >= p->len) || ('=' != p->s[p->pos])) {
            return xml_syntax(p, "'=' expected");
        }
        ++p->pos;
        xml_skip_ws(p);
        if ((p->pos >= p->len) || (('"' != p->s[p->pos]) && ('\'' != p->s[p->pos]))) {
            return xml_syntax(p, "quoted value expected");
        }
        memset(&val, 0, sizeof(val));
        status = xml_read_attr_value(p, &val);
        shrewd_basic_xml_text_free(val.data);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    for (;;) {
        xml_skip_ws(p);
        if (p->pos >= p->len) {
            return xml_syntax(p, "unterminated element");
        }
        if (xml_starts(p, "</")) {
            char close[XML_NAME_MAX];
            p->pos += 2;
            status = xml_read_name(p, close, sizeof(close));
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (0 != strcmp(close, name)) {
                return xml_syntax(p, "mismatched close tag");
            }
            xml_skip_ws(p);
            if ((p->pos >= p->len) || ('>' != p->s[p->pos])) {
                return xml_syntax(p, "'>' expected");
            }
            ++p->pos;
            return SHREWD_BASIC_ERR_OK;
        }
        if (xml_starts(p, "<!--")) {
            status = xml_skip_comment(p);
        } else if (xml_starts(p, "<![CDATA[")) {
            xml_buf_t sink;
            memset(&sink, 0, sizeof(sink));
            status = xml_read_text(p, &sink);
            shrewd_basic_xml_text_free(sink.data);
        } else if (xml_starts(p, "<")) {
            status = xml_skip_element(p);
        } else {
            xml_buf_t sink;
            memset(&sink, 0, sizeof(sink));
            status = xml_read_text(p, &sink);
            shrewd_basic_xml_text_free(sink.data);
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
}

/* --- decode --------------------------------------------------------------- */

static int xml_decode_fields(xml_parser_t *p,
                             const char *tag,
                             const shrewd_basic_reflect_type_t *type,
                             void *object,
                             const shrewd_basic_xml_policy_t *map,
                             size_t map_count,
                             size_t depth);

/* One field element: leaves carry text, struct/pointer targets recurse
   through xml_decode_fields (which parses the whole element). */
static int xml_decode_field_element(xml_parser_t *p,
                                    const char *tag,
                                    const shrewd_basic_reflect_field_t *field,
                                    void *at,
                                    const shrewd_basic_xml_policy_t *map,
                                    size_t map_count,
                                    size_t depth) {
    shrewd_basic_reflect_kind_t kind = field->sb_refl_type->sb_refl_kind;
    int is_leaf = ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))
                   || (SHREWD_BASIC_REFLECT_KIND_STRING == kind) || (0 != xml_wide_kind(kind))
                   || ((SHREWD_BASIC_REFLECT_KIND_STRUCT != kind)
                       && (SHREWD_BASIC_REFLECT_KIND_ARRAY != kind)
                       && (SHREWD_BASIC_REFLECT_KIND_UNION != kind)))
                      ? 1
                      : 0;
    void *target = at;
    void *block = NULL;
    int status = 0;
    if (XML_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_JSON_DEPTH;
    }
    if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_calloc(&block, 1, field->sb_refl_type->sb_refl_size)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        target = block;
    }
    if (0 == is_leaf) {
        status = xml_decode_fields(p, tag, field->sb_refl_type, target, map, map_count, depth + 1);
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            if (SHREWD_BASIC_ERR_OK == status) {
                memcpy(at, &block, sizeof(block));
            } else {
                shrewd_basic_xml_free(block, field->sb_refl_type);
                {
                    void *old = block;
                    (void)shrewd_basic_platform_free(&old);
                }
            }
        }
        return status;
    }
    {
        char name[XML_NAME_MAX];
        xml_buf_t text;
        ++p->pos; /* '<' */
        status = xml_read_name(p, name, sizeof(name));
        if (SHREWD_BASIC_ERR_OK == status) {
            status = (0 == strcmp(name, tag)) ? SHREWD_BASIC_ERR_OK
                                              : xml_syntax(p, "unexpected element name");
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            xml_skip_ws(p);
            if ((p->pos < p->len) && ('/' == p->s[p->pos])) {
                p->pos += 2; /* <name/> */
            } else if ((p->pos < p->len) && ('>' == p->s[p->pos])) {
                ++p->pos;
                memset(&text, 0, sizeof(text));
                status = xml_read_text(p, &text);
                if (SHREWD_BASIC_ERR_OK == status) {
                    size_t start = 0;
                    size_t end = text.len;
                    while ((start < end)
                           && ((' ' == text.data[start]) || ('\t' == text.data[start])
                               || ('\r' == text.data[start]) || ('\n' == text.data[start]))) {
                        ++start;
                    }
                    while ((end > start)
                           && ((' ' == text.data[end - 1]) || ('\t' == text.data[end - 1])
                               || ('\r' == text.data[end - 1]) || ('\n' == text.data[end - 1]))) {
                        --end;
                    }
                    status = xml_field_from(field, target, text.data + start, end - start);
                }
                shrewd_basic_xml_text_free(text.data);
                if (SHREWD_BASIC_ERR_OK == status) {
                    char close[XML_NAME_MAX];
                    xml_skip_ws(p);
                    if (!xml_starts(p, "</")) {
                        status = xml_syntax(p, "close tag expected");
                    } else {
                        p->pos += 2;
                        status = xml_read_name(p, close, sizeof(close));
                        if ((SHREWD_BASIC_ERR_OK == status) && (0 != strcmp(close, name))) {
                            status = xml_syntax(p, "mismatched close tag");
                        }
                        if (SHREWD_BASIC_ERR_OK == status) {
                            xml_skip_ws(p);
                            if ((p->pos >= p->len) || ('>' != p->s[p->pos])) {
                                status = xml_syntax(p, "'>' expected");
                            } else {
                                ++p->pos;
                            }
                        }
                    }
                }
            } else {
                status = xml_syntax(p, "attributes on field elements are not supported");
            }
        }
        if ((SHREWD_BASIC_ERR_OK == status)
            && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR))) {
            memcpy(at, &block, sizeof(block));
        }
    }
    if ((SHREWD_BASIC_ERR_OK != status)
        && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR))) {
        void *old = block;
        (void)shrewd_basic_platform_free(&old);
    }
    return status;
}

static int xml_decode_fields(xml_parser_t *p,
                             const char *tag,
                             const shrewd_basic_reflect_type_t *type,
                             void *object,
                             const shrewd_basic_xml_policy_t *map,
                             size_t map_count,
                             size_t depth) {
    char name[XML_NAME_MAX];
    char seen[16][XML_NAME_MAX];
    size_t seen_n = 0;
    int status = 0;
    int empty = 0;
    size_t *counts = NULL;
    if (XML_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_JSON_DEPTH;
    }
    ++p->pos; /* '<' */
    status = xml_read_name(p, name, sizeof(name));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 != strcmp(name, tag)) {
        return xml_syntax(p, "unexpected element name");
    }
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_calloc((void **)&counts,
                                        (0 < type->sb_refl_field_count) ? type->sb_refl_field_count
                                                                        : 1,
                                        sizeof(*counts))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    for (;;) {
        char attr[XML_NAME_MAX];
        xml_buf_t val;
        xml_skip_ws(p);
        if (p->pos >= p->len) {
            status = xml_syntax(p, "unterminated tag");
            goto done;
        }
        if (xml_starts(p, "/>")) {
            p->pos += 2;
            empty = 1;
            break;
        }
        if ('>' == p->s[p->pos]) {
            ++p->pos;
            break;
        }
        status = xml_read_name(p, attr, sizeof(attr));
        if (SHREWD_BASIC_ERR_OK != status) {
            goto done;
        }
        for (size_t i = 0; i < seen_n; ++i) {
            if (0 == strcmp(seen[i], attr)) {
                status = xml_syntax(p, "duplicate attribute");
                goto done;
            }
        }
        if (seen_n < (sizeof(seen) / sizeof(seen[0]))) {
            snprintf(seen[seen_n], sizeof(seen[0]), "%s", attr);
            ++seen_n;
        }
        xml_skip_ws(p);
        if ((p->pos >= p->len) || ('=' != p->s[p->pos])) {
            status = xml_syntax(p, "'=' expected");
            goto done;
        }
        ++p->pos;
        xml_skip_ws(p);
        if ((p->pos >= p->len) || (('"' != p->s[p->pos]) && ('\'' != p->s[p->pos]))) {
            status = xml_syntax(p, "quoted value expected");
            goto done;
        }
        memset(&val, 0, sizeof(val));
        status = xml_read_attr_value(p, &val);
        if (SHREWD_BASIC_ERR_OK == status) {
            const shrewd_basic_reflect_field_t *field = NULL;
            for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
                const shrewd_basic_reflect_field_t *f = &type->sb_refl_fields[i];
                const shrewd_basic_xml_policy_t *row = xml_policy(map, map_count, f->sb_refl_name);
                if ((NULL != row) && (0 != row->as_attr)
                    && (0u == (row->flags & SHREWD_BASIC_XML_HIDDEN))
                    && (0 == strcmp(attr, xml_tag(row, f->sb_refl_name)))) {
                    field = f;
                    break;
                }
            }
            if (NULL != field) {
                status = xml_field_from(
                    field, (unsigned char *)object + field->sb_refl_offset, val.data, val.len);
                if (SHREWD_BASIC_ERR_OK == status) {
                    counts[field - type->sb_refl_fields] = 1;
                }
            }
        }
        shrewd_basic_xml_text_free(val.data);
        if (SHREWD_BASIC_ERR_OK != status) {
            goto done;
        }
    }
    if (0 == empty) {
        for (;;) {
            xml_skip_ws(p);
            if (p->pos >= p->len) {
                status = xml_syntax(p, "unterminated element");
                goto done;
            }
            if (xml_starts(p, "</")) {
                char close[XML_NAME_MAX];
                p->pos += 2;
                status = xml_read_name(p, close, sizeof(close));
                if (SHREWD_BASIC_ERR_OK != status) {
                    goto done;
                }
                if (0 != strcmp(close, name)) {
                    status = xml_syntax(p, "mismatched close tag");
                    goto done;
                }
                xml_skip_ws(p);
                if ((p->pos >= p->len) || ('>' != p->s[p->pos])) {
                    status = xml_syntax(p, "'>' expected");
                    goto done;
                }
                ++p->pos;
                break;
            }
            if (xml_starts(p, "<!--")) {
                status = xml_skip_comment(p);
                if (SHREWD_BASIC_ERR_OK != status) {
                    goto done;
                }
                continue;
            }
            if (xml_starts(p, "<![CDATA[")) {
                xml_buf_t sink;
                memset(&sink, 0, sizeof(sink));
                status = xml_read_text(p, &sink);
                shrewd_basic_xml_text_free(sink.data);
                if (SHREWD_BASIC_ERR_OK != status) {
                    goto done;
                }
                continue;
            }
            if (xml_starts(p, "<")) {
                char child[XML_NAME_MAX];
                const shrewd_basic_reflect_field_t *field = NULL;
                size_t save = p->pos;
                int shape = 0;
                ++p->pos;
                status = xml_read_name(p, child, sizeof(child));
                p->pos = save;
                if (SHREWD_BASIC_ERR_OK != status) {
                    goto done;
                }
                for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
                    const shrewd_basic_reflect_field_t *f = &type->sb_refl_fields[i];
                    const shrewd_basic_xml_policy_t *row
                        = xml_policy(map, map_count, f->sb_refl_name);
                    if ((NULL != row)
                        && ((0 != row->as_attr)
                            || (0u != (row->flags & SHREWD_BASIC_XML_HIDDEN)))) {
                        continue;
                    }
                    if (0 == strcmp(child, xml_tag(row, f->sb_refl_name))) {
                        field = f;
                        break;
                    }
                }
                if (NULL == field) {
                    status = xml_skip_element(p);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        goto done;
                    }
                    continue;
                }
                shape = xml_shape(field);
                if (XML_F_ARRAY == shape) {
                    size_t idx = counts[field - type->sb_refl_fields];
                    if (idx >= field->sb_refl_count) {
                        status = xml_syntax(p, "too many array elements");
                        goto done;
                    }
                    counts[field - type->sb_refl_fields] = idx + 1;
                    status
                        = xml_decode_field_element(p,
                                                   child,
                                                   field,
                                                   (unsigned char *)object + field->sb_refl_offset
                                                       + (idx * field->sb_refl_stride),
                                                   map,
                                                   map_count,
                                                   depth + 1);
                } else if (0 != counts[field - type->sb_refl_fields]) {
                    status = xml_syntax(p, "duplicate element");
                    goto done;
                } else {
                    counts[field - type->sb_refl_fields] = 1;
                    status
                        = xml_decode_field_element(p,
                                                   child,
                                                   field,
                                                   (unsigned char *)object + field->sb_refl_offset,
                                                   map,
                                                   map_count,
                                                   depth + 1);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    goto done;
                }
                continue;
            }
            {
                xml_buf_t sink;
                memset(&sink, 0, sizeof(sink));
                status = xml_read_text(p, &sink);
                shrewd_basic_xml_text_free(sink.data);
                if (SHREWD_BASIC_ERR_OK != status) {
                    goto done;
                }
            }
        }
    }
    for (size_t i = 0; i < map_count; ++i) {
        const shrewd_basic_xml_policy_t *row = &map[i];
        const shrewd_basic_reflect_field_t *f = NULL;
        if ((NULL == row->field) || (0u == (row->flags & SHREWD_BASIC_XML_REQUIRED))
            || (0u != (row->flags & SHREWD_BASIC_XML_HIDDEN))) {
            continue;
        }
        if ((SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_type_find_field(type, row->field, &f))
            && (0 == counts[f - type->sb_refl_fields])) {
            xml_fail(p->error, p->line, "required field missing");
            status = SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
            goto done;
        }
    }
done: {
    void *old = counts;
    (void)shrewd_basic_platform_free(&old);
}
    return status;
}

int shrewd_basic_xml_decode(const char *text,
                            size_t length,
                            void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_xml_policy_t *map,
                            size_t map_count,
                            shrewd_basic_xml_error_t *error) {
    xml_parser_t p;
    int status = 0;
    if ((NULL == text) || (NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(object, 0, type->sb_refl_size);
    memset(&p, 0, sizeof(p));
    p.s = text;
    p.len = length;
    p.line = 1;
    p.error = error;
    xml_skip_ws(&p);
    if (xml_starts(&p, "<?")) {
        if (!xml_starts(&p, "<?xml")) {
            return xml_syntax(&p, "processing instruction rejected");
        }
        p.pos += 5;
        for (;;) {
            if ((p.pos + 2) > p.len) {
                return xml_syntax(&p, "unterminated prolog");
            }
            if (0 == memcmp(p.s + p.pos, "?>", 2)) {
                p.pos += 2;
                break;
            }
            if ('\n' == p.s[p.pos]) {
                ++p.line;
            }
            ++p.pos;
        }
    }
    for (;;) {
        size_t save = p.pos;
        xml_skip_ws(&p);
        if (xml_starts(&p, "<!--")) {
            status = xml_skip_comment(&p);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        p.pos = save;
        break;
    }
    xml_skip_ws(&p);
    if ((xml_starts(&p, "<!DOCTYPE")) || (xml_starts(&p, "<!ENTITY"))) {
        return xml_syntax(&p, "DTD rejected (XXE guard)");
    }
    if (!xml_starts(&p, "<")) {
        return xml_syntax(&p, "root element expected");
    }
    status = xml_decode_fields(&p, type->sb_refl_name, type, object, map, map_count, 0);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (;;) {
        size_t save = p.pos;
        xml_skip_ws(&p);
        if (xml_starts(&p, "<!--")) {
            status = xml_skip_comment(&p);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        p.pos = save;
        break;
    }
    xml_skip_ws(&p);
    if (p.pos != p.len) {
        return xml_syntax(&p, "trailing characters");
    }
    return SHREWD_BASIC_ERR_OK;
}

/* --- free (contents only - the root stays the caller's) ------------------- */

static void xml_free_fields(const shrewd_basic_reflect_type_t *type, void *object);

static void xml_free_value(const shrewd_basic_reflect_type_t *type, void *value, size_t depth) {
    if (XML_MAX_DEPTH <= depth) {
        return;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind)
        || (0 != xml_wide_kind(type->sb_refl_kind))) {
        void *pointer = NULL;
        memcpy(&pointer, value, sizeof(pointer));
        if (NULL != pointer) {
            (void)shrewd_basic_platform_free(&pointer);
            memset(value, 0, sizeof(void *));
        }
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind) {
        xml_free_fields(type, value);
        return;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            xml_free_value(type->sb_refl_elem,
                           (unsigned char *)value + (i * type->sb_refl_elem_stride),
                           depth + 1);
        }
    }
}

static void xml_free_fields(const shrewd_basic_reflect_type_t *type, void *object) {
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        unsigned char *at = (unsigned char *)object + field->sb_refl_offset;
        int shape = xml_shape(field);
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            continue; /* inline buffer */
        }
        if (XML_F_PTR == shape) {
            void *pointer = NULL;
            memcpy(&pointer, at, sizeof(pointer));
            if (NULL != pointer) {
                xml_free_value(field->sb_refl_type, pointer, 0);
                (void)shrewd_basic_platform_free(&pointer);
                memset(at, 0, sizeof(void *));
            }
            continue;
        }
        if ((SHREWD_BASIC_REFLECT_KIND_STRING == field->sb_refl_type->sb_refl_kind)
            || (0 != xml_wide_kind(field->sb_refl_type->sb_refl_kind))) {
            void *pointer = NULL;
            memcpy(&pointer, at, sizeof(pointer));
            if (NULL != pointer) {
                (void)shrewd_basic_platform_free(&pointer);
                memset(at, 0, sizeof(void *));
            }
            continue;
        }
        if (XML_F_ARRAY == shape) {
            for (size_t j = 0; j < field->sb_refl_count; ++j) {
                xml_free_value(field->sb_refl_type, at + (j * field->sb_refl_stride), 0);
            }
            continue;
        }
        if (XML_F_STRUCT == shape) {
            xml_free_fields(field->sb_refl_type, at);
        }
    }
}

int shrewd_basic_xml_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    xml_free_fields(type, object);
    return SHREWD_BASIC_ERR_OK;
}

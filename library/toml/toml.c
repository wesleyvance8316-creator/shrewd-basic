/**
 * @file toml.c
 * @brief TOML 1.0 subset mapping (see formats/toml.h for the profile).
 *
 * The grammar layer lives here; every leaf conversion goes through the
 * shared text layer (shrewd_basic_reflect_scan_text / _render_text), so
 * numeric strictness, enum naming and string ownership stay in one
 * place. Rejected constructs always carry a line number.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/toml.h"
#include "shrewd-basic/platform/memory.h"

#define TOML_MAX_TABLES 64
#define TOML_MAX_DEPTH 32

typedef enum { TV_STR = 0, TV_INT, TV_FLOAT, TV_BOOL, TV_DATETIME } toml_vkind_t;

typedef struct {
    const char *cur;
    const char *end;
    size_t line;
    shrewd_basic_toml_error_t *error;
} toml_parser_t;

typedef struct {
    const void *obj;
    const void *type; /* record identity: an array's first element
                         shares its owner's address */
    size_t field_count;
    unsigned char *seen; /* per field: 0 unset, 1 key, 2 subtable, 3 array table */
    size_t *used;        /* per field: [[array table]] elements opened */
} toml_table_t;

typedef struct {
    toml_table_t tables[TOML_MAX_TABLES];
    size_t count;
} toml_tables_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} toml_buf_t;

/* --- errors --------------------------------------------------------------- */

static int toml_fail(shrewd_basic_toml_error_t *error, size_t line, int status, const char *msg) {
    if (NULL != error) {
        error->line = line;
        (void)snprintf(error->message, sizeof(error->message), "%s", msg);
    }
    return status;
}

static int p_fail(toml_parser_t *p, int status, const char *msg) {
    return toml_fail(p->error, p->line, status, msg);
}

/* --- string builder -------------------------------------------------------- */

static int buf_put(toml_buf_t *b, const char *text, size_t n) {
    if ((b->len + n + 1) > b->cap) {
        size_t cap = (0 == b->cap) ? 256 : b->cap;
        void *block = NULL;
        while (cap < (b->len + n + 1)) {
            cap *= 2;
        }
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, cap)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        if (NULL != b->data) {
            memcpy(block, b->data, b->len);
            {
                void *old = b->data;
                (void)shrewd_basic_platform_free(&old);
            }
        }
        b->data = block;
        b->cap = cap;
    }
    memcpy(b->data + b->len, text, n);
    b->len += n;
    b->data[b->len] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static int buf_puts(toml_buf_t *b, const char *text) {
    return buf_put(b, text, strlen(text));
}

static int buf_putc(toml_buf_t *b, char c) {
    return buf_put(b, &c, 1);
}

static int buf_printf(toml_buf_t *b, const char *fmt, ...) {
    char tmp[128];
    va_list ap;
    int written = 0;
    va_start(ap, fmt);
    written = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if ((0 > written) || ((size_t)written >= sizeof(tmp))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    return buf_put(b, tmp, (size_t)written);
}

/* --- field helpers ---------------------------------------------------------- */

static const shrewd_basic_reflect_field_t *find_field(const shrewd_basic_reflect_type_t *type,
                                                      const char *name,
                                                      size_t len) {
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        if ((strlen(field->sb_refl_name) == len) && (0 == memcmp(field->sb_refl_name, name, len))) {
            return field;
        }
    }
    return NULL;
}

static int field_is_array(const shrewd_basic_reflect_field_t *field) {
    return ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_ARRAY))
            && (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)))
               ? 1
               : 0;
}

static int field_is_buffer(const shrewd_basic_reflect_field_t *field) {
    return (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) ? 1 : 0;
}

/* A value vector behind a pointer (FIELD_DARRAY): storage at the
   member slot, bound in the owner's count member. */
static int field_is_dyn(const shrewd_basic_reflect_field_t *field) {
    return ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_DARRAY))
            && (0u
                == (field->sb_refl_flags
                    & (SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL))))
               ? 1
               : 0;
}

static size_t dyn_count(const shrewd_basic_reflect_field_t *field, const void *owner) {
    size_t count = 0;
    memcpy(&count,
           (const unsigned char *)owner + field->sb_refl_limit_offset,
           field->sb_refl_limit_size);
    return count;
}

static void dyn_set_count(const shrewd_basic_reflect_field_t *field, void *owner, size_t count) {
    size_t word = count;
    memcpy((unsigned char *)owner + field->sb_refl_limit_offset, &word, field->sb_refl_limit_size);
}

static void *dyn_base(const shrewd_basic_reflect_field_t *field, const void *owner) {
    void *ptr = NULL;
    memcpy(&ptr, (const unsigned char *)owner + field->sb_refl_offset, sizeof(ptr));
    return ptr;
}

static void dyn_set_base(const shrewd_basic_reflect_field_t *field, void *owner, void *ptr) {
    memcpy((unsigned char *)owner + field->sb_refl_offset, &ptr, sizeof(ptr));
}

/* Grow a dynamic vector to hold @p need elements (zeroed tail). */
static int dyn_grow(const shrewd_basic_reflect_field_t *field, void *owner, size_t need) {
    size_t have = dyn_count(field, owner);
    void *base = dyn_base(field, owner);
    void *block = NULL;
    if (need <= have) {
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_realloc(&block, base, need * field->sb_refl_stride)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    memset((unsigned char *)block + (have * field->sb_refl_stride),
           0,
           (need - have) * field->sb_refl_stride);
    dyn_set_base(field, owner, block);
    dyn_set_count(field, owner, need);
    return SHREWD_BASIC_ERR_OK;
}

/* Synthesize the element view of a fixed array field. */
static shrewd_basic_reflect_field_t elem_field(const shrewd_basic_reflect_field_t *field) {
    shrewd_basic_reflect_field_t elem = *field;
    elem.sb_refl_flags &= (uint32_t)SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED;
    elem.sb_refl_count = (size_t)1;
    elem.sb_refl_size = field->sb_refl_stride;
    elem.sb_refl_stride = field->sb_refl_stride;
    return elem;
}

/* Can a scalar/string token of class @p kind land in this field? */
static int scalar_fits(toml_vkind_t kind, const shrewd_basic_reflect_field_t *field) {
    shrewd_basic_reflect_kind_t k = field->sb_refl_type->sb_refl_kind;
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_BITS
               | SHREWD_BASIC_REFLECT_FIELD_F_UNION))) {
        return 0;
    }
    if (field_is_array(field)) {
        return 0;
    }
    if (field_is_buffer(field)) {
        /* Plain char buffers only - wide buffers refuse here. */
        return (((TV_STR == kind) || (TV_DATETIME == kind)) && (1 == field->sb_refl_stride)) ? 1
                                                                                             : 0;
    }
    switch (kind) {
    case TV_DATETIME:
        /* Raw text preserved: string fields only. */
        return ((SHREWD_BASIC_REFLECT_KIND_STRING == k)
                || (field_is_buffer(field) && (1 == field->sb_refl_stride)))
                   ? 1
                   : 0;
    case TV_STR:
        return ((SHREWD_BASIC_REFLECT_KIND_CHAR == k) || (SHREWD_BASIC_REFLECT_KIND_STRING == k)
                || (SHREWD_BASIC_REFLECT_KIND_ENUM == k))
                   ? 1
                   : 0;
    case TV_INT:
        return ((SHREWD_BASIC_REFLECT_KIND_INT8 == k) || (SHREWD_BASIC_REFLECT_KIND_INT16 == k)
                || (SHREWD_BASIC_REFLECT_KIND_INT32 == k) || (SHREWD_BASIC_REFLECT_KIND_INT64 == k)
                || (SHREWD_BASIC_REFLECT_KIND_UINT8 == k) || (SHREWD_BASIC_REFLECT_KIND_UINT16 == k)
                || (SHREWD_BASIC_REFLECT_KIND_UINT32 == k)
                || (SHREWD_BASIC_REFLECT_KIND_UINT64 == k) || (SHREWD_BASIC_REFLECT_KIND_ENUM == k)
                || (SHREWD_BASIC_REFLECT_KIND_FLOAT == k) || (SHREWD_BASIC_REFLECT_KIND_DOUBLE == k)
                || (SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE == k))
                   ? 1
                   : 0;
    case TV_FLOAT:
        return ((SHREWD_BASIC_REFLECT_KIND_FLOAT == k) || (SHREWD_BASIC_REFLECT_KIND_DOUBLE == k)
                || (SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE == k))
                   ? 1
                   : 0;
    case TV_BOOL:
        return (SHREWD_BASIC_REFLECT_KIND_BOOL == k) ? 1 : 0;
    default:
        return 0;
    }
}

/* Element kinds allowed inside arrays (leaf scalars + strings). */
static int elem_supported(const shrewd_basic_reflect_field_t *field) {
    shrewd_basic_reflect_kind_t k = field->sb_refl_type->sb_refl_kind;
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_BITS
               | SHREWD_BASIC_REFLECT_FIELD_F_UNION))) {
        return 0;
    }
    return ((SHREWD_BASIC_REFLECT_KIND_CHAR == k) || (SHREWD_BASIC_REFLECT_KIND_STRING == k)
            || (SHREWD_BASIC_REFLECT_KIND_ENUM == k) || (SHREWD_BASIC_REFLECT_KIND_BOOL == k)
            || (SHREWD_BASIC_REFLECT_KIND_INT8 == k) || (SHREWD_BASIC_REFLECT_KIND_INT16 == k)
            || (SHREWD_BASIC_REFLECT_KIND_INT32 == k) || (SHREWD_BASIC_REFLECT_KIND_INT64 == k)
            || (SHREWD_BASIC_REFLECT_KIND_UINT8 == k) || (SHREWD_BASIC_REFLECT_KIND_UINT16 == k)
            || (SHREWD_BASIC_REFLECT_KIND_UINT32 == k) || (SHREWD_BASIC_REFLECT_KIND_UINT64 == k)
            || (SHREWD_BASIC_REFLECT_KIND_FLOAT == k) || (SHREWD_BASIC_REFLECT_KIND_DOUBLE == k)
            || (SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE == k))
               ? 1
               : 0;
}

/* --- encode ----------------------------------------------------------------- */

static int enc_string(toml_buf_t *b, const char *text, size_t len) {
    int status = buf_putc(b, '"');
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < len); ++i) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
        case '"':
            status = buf_puts(b, "\\\"");
            break;
        case '\\':
            status = buf_puts(b, "\\\\");
            break;
        case '\b':
            status = buf_puts(b, "\\b");
            break;
        case '\f':
            status = buf_puts(b, "\\f");
            break;
        case '\n':
            status = buf_puts(b, "\\n");
            break;
        case '\r':
            status = buf_puts(b, "\\r");
            break;
        case '\t':
            status = buf_puts(b, "\\t");
            break;
        default:
            if (0x20u > c) {
                status = buf_printf(b, "\\u%04x", (unsigned int)c);
            } else {
                status = buf_putc(b, (char)c);
            }
            break;
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = buf_putc(b, '"');
    }
    return status;
}

/* Render one leaf value as TOML text (after the "key = " part). */
static int enc_leaf(toml_buf_t *b,
                    const shrewd_basic_reflect_field_t *field,
                    const unsigned char *at) {
    shrewd_basic_reflect_kind_t k = field->sb_refl_type->sb_refl_kind;
    char text[128];
    size_t len = 0;
    int status = 0;
    if ((SHREWD_BASIC_REFLECT_KIND_STRING == k) && (0 == field_is_buffer(field))) {
        /* Pointer string (owned or borrowed): TOML has no null string. */
        const char *content = NULL;
        memcpy(&content, at, sizeof(content));
        if (NULL == content) {
            return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
        }
        return enc_string(b, content, strlen(content));
    }
    status = shrewd_basic_reflect_render_text(field, at, text, sizeof(text), &len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if ((SHREWD_BASIC_REFLECT_KIND_CHAR == k) || (SHREWD_BASIC_REFLECT_KIND_ENUM == k)) {
        /* Characters and enum names live in TOML strings. */
        return enc_string(b, text, len);
    }
    if ((SHREWD_BASIC_REFLECT_KIND_FLOAT == k) || (SHREWD_BASIC_REFLECT_KIND_DOUBLE == k)
        || (SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE == k)) {
        if ((0 == strstr(text, ".")) && (0 == strstr(text, "e")) && (0 == strstr(text, "E"))) {
            /* Keep floats floats ("1" would parse back as an int). */
            status = buf_puts(b, text);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = buf_puts(b, ".0");
            }
            return status;
        }
    }
    return buf_put(b, text, len);
}

static int enc_value(toml_buf_t *b,
                     const shrewd_basic_reflect_field_t *field,
                     const unsigned char *at);

static int enc_struct(toml_buf_t *b,
                      const void *object,
                      const shrewd_basic_reflect_type_t *type,
                      const char *path,
                      size_t depth) {
    int status = SHREWD_BASIC_ERR_OK;
    if (TOML_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
    }
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_field_count); ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
            continue;
        }
        status = buf_printf(b, "%s = ", field->sb_refl_name);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = enc_value(b, field, (const unsigned char *)object + field->sb_refl_offset);
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = buf_putc(b, '\n');
        }
    }
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < type->sb_refl_field_count); ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        char sub[256];
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT != field->sb_refl_type->sb_refl_kind) {
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
        }
        if ('\0' == path[0]) {
            (void)snprintf(sub, sizeof(sub), "%s", field->sb_refl_name);
        } else {
            (void)snprintf(sub, sizeof(sub), "%s.%s", path, field->sb_refl_name);
        }
        if ((0 != field_is_array(field)) || (0 != field_is_dyn(field))) {
            /* Struct array: one [[path]] block per element. */
            void *base = NULL;
            size_t count = 0;
            if (0 != field_is_dyn(field)) {
                base = dyn_base(field, object);
                count = dyn_count(field, object);
            } else {
                base = (unsigned char *)object + field->sb_refl_offset;
                count = field->sb_refl_count;
            }
            for (size_t j = 0; (SHREWD_BASIC_ERR_OK == status) && (j < count); ++j) {
                status = buf_printf(b, "\n[[%s]]\n", sub);
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = enc_struct(b,
                                        (const unsigned char *)base + (j * field->sb_refl_stride),
                                        field->sb_refl_type,
                                        sub,
                                        depth + 1);
                }
            }
            continue;
        }
        status = buf_printf(b, "\n[%s]\n", sub);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = enc_struct(b,
                                (const unsigned char *)object + field->sb_refl_offset,
                                field->sb_refl_type,
                                sub,
                                depth + 1);
        }
    }
    return status;
}

static int enc_value(toml_buf_t *b,
                     const shrewd_basic_reflect_field_t *field,
                     const unsigned char *at) {
    if ((0 != field_is_array(field)) || (0 != field_is_dyn(field))) {
        const unsigned char *base = NULL;
        size_t count = 0;
        shrewd_basic_reflect_field_t elem = elem_field(field);
        int status = 0;
        if (0 != field_is_dyn(field)) {
            base = (const unsigned char *)dyn_base(
                field, (const unsigned char *)at - field->sb_refl_offset);
            count = dyn_count(field, (const unsigned char *)at - field->sb_refl_offset);
        } else {
            base = at;
            count = field->sb_refl_count;
        }
        status = buf_putc(b, '[');
        if ((SHREWD_BASIC_ERR_OK == status) && (0 == elem_supported(field))) {
            status = SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
        }
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < count); ++i) {
            if (0 != i) {
                status = buf_puts(b, ", ");
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                status = enc_leaf(b, &elem, base + (i * field->sb_refl_stride));
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = buf_putc(b, ']');
        }
        return status;
    }
    return enc_leaf(b, field, at);
}

int shrewd_basic_toml_encode(const void *object,
                             const shrewd_basic_reflect_type_t *type,
                             char **out_text,
                             shrewd_basic_toml_error_t *error) {
    toml_buf_t b;
    int status = 0;
    if ((NULL == object) || (NULL == type) || (NULL == out_text)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return toml_fail(error, 0, SHREWD_BASIC_ERR_ARG_INVALID, "root type must be a struct");
    }
    memset(&b, 0, sizeof(b));
    status = enc_struct(&b, object, type, "", 0);
    if (SHREWD_BASIC_ERR_OK != status) {
        if (NULL != b.data) {
            void *old = b.data;
            (void)shrewd_basic_platform_free(&old);
        }
        return toml_fail(error, 0, status, "value has no TOML form in this subset");
    }
    *out_text = b.data;
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_toml_text_free(char *text) {
    if (NULL != text) {
        void *old = text;
        (void)shrewd_basic_platform_free(&old);
    }
}

/* --- decode: parser helpers -------------------------------------------------- */

static void p_ws(toml_parser_t *p) {
    while ((p->cur < p->end) && ((' ' == *p->cur) || ('\t' == *p->cur))) {
        ++p->cur;
    }
}

static void p_skip_comment(toml_parser_t *p) {
    if ((p->cur < p->end) && ('#' == *p->cur)) {
        while ((p->cur < p->end) && ('\n' != *p->cur)) {
            ++p->cur;
        }
    }
}

static int p_eol(toml_parser_t *p) {
    p_ws(p);
    p_skip_comment(p);
    if (p->cur >= p->end) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ('\r' == *p->cur) {
        ++p->cur;
    }
    if (p->cur < p->end) {
        if ('\n' != *p->cur) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "trailing characters on line");
        }
        ++p->cur;
        p->line += 1;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Whitespace, newlines and comments (inside arrays). */
static int p_skip_inline_ws(toml_parser_t *p) {
    for (;;) {
        p_ws(p);
        if ((p->cur < p->end) && ('#' == *p->cur)) {
            p_skip_comment(p);
        }
        if ((p->cur < p->end) && ('\r' == *p->cur)) {
            ++p->cur;
        }
        if ((p->cur < p->end) && ('\n' == *p->cur)) {
            ++p->cur;
            p->line += 1;
            continue;
        }
        return SHREWD_BASIC_ERR_OK;
    }
}

static int is_bare_key_char(char c) {
    return (('a' <= c) && ('z' >= c)) || (('A' <= c) && ('Z' >= c)) || (('0' <= c) && ('9' >= c))
           || ('_' == c) || ('-' == c);
}

static int p_basic_string(toml_parser_t *p, toml_buf_t *out);
static int p_literal_string(toml_parser_t *p, toml_buf_t *out);

/* --- decode: keys ------------------------------------------------------------- */

/*
 * One key segment: bare, "basic" (escapes) or 'literal'. Quoted forms
 * decode into @p buf; bare forms return a slice of the source via
 * buf too (uniform, cheap for config sizes).
 */
static int p_key_segment(toml_parser_t *p, toml_buf_t *buf) {
    int status = 0;
    memset(buf, 0, sizeof(*buf));
    p_ws(p);
    if (p->cur >= p->end) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "key expected");
    }
    if ('"' == *p->cur) {
        status = p_basic_string(p, buf);
    } else if ('\'' == *p->cur) {
        status = p_literal_string(p, buf);
    } else {
        const char *start = p->cur;
        while ((p->cur < p->end) && (0 != is_bare_key_char(*p->cur))) {
            ++p->cur;
        }
        if (p->cur == start) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "expected a key");
        }
        status = buf_put(buf, start, (size_t)(p->cur - start));
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 == buf->len) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "empty key");
    }
    return SHREWD_BASIC_ERR_OK;
}

/* --- decode: strings ---------------------------------------------------------- */

static int utf8_emit(toml_buf_t *b, uint32_t cp) {
    if (cp < 0x80u) {
        return buf_putc(b, (char)cp);
    }
    if (cp < 0x800u) {
        char out[2];
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return buf_put(b, out, 2);
    }
    if (cp < 0x10000u) {
        char out[3];
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return buf_put(b, out, 3);
    }
    {
        char out[4];
        out[0] = (char)(0xF0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        return buf_put(b, out, 4);
    }
}

static int p_hex4(toml_parser_t *p, uint32_t *out) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        char c = '\0';
        if (p->cur >= p->end) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "truncated \\u escape");
        }
        c = *p->cur++;
        if (('0' <= c) && ('9' >= c)) {
            value = (value << 4) | (uint32_t)(c - '0');
        } else if (('a' <= c) && ('f' >= c)) {
            value = (value << 4) | (uint32_t)(c - 'a' + 10);
        } else if (('A' <= c) && ('F' >= c)) {
            value = (value << 4) | (uint32_t)(c - 'A' + 10);
        } else {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad hex digit in \\u escape");
        }
    }
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

/*
 * A run of N >= 3 quotes at the terminator: the last three close the
 * string and the leading N-3 belong to the content (TOML 1.0).
 */
static int p_quote_run(toml_parser_t *p, char quote, size_t *consumed) {
    size_t run = 0;
    while ((p->cur < p->end) && (quote == *p->cur)) {
        ++run;
        ++p->cur;
    }
    *consumed = run;
    return (run >= 3) ? 1 : 0;
}

static int p_multi_basic_string(toml_parser_t *p, toml_buf_t *out) {
    int status = SHREWD_BASIC_ERR_OK;
    p->cur += 2; /* remaining two opener quotes (one already eaten) */
    /* First newline right after the opener is stripped. */
    if ((p->cur < p->end) && ('\r' == *p->cur)) {
        ++p->cur;
    }
    if ((p->cur < p->end) && ('\n' == *p->cur)) {
        ++p->cur;
        p->line += 1;
    }
    for (;;) {
        char c = '\0';
        if (p->cur >= p->end) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated multi-line string");
        }
        c = *p->cur;
        if ('"' == c) {
            size_t consumed = 0;
            size_t keep = 0;
            if (0 != p_quote_run(p, '"', &consumed)) {
                keep = consumed - 3; /* quotes before the terminator */
                for (size_t i = 0; i < keep; ++i) {
                    status = buf_putc(out, '"');
                }
                return status;
            }
            for (size_t i = 0; i < consumed; ++i) {
                status = buf_putc(out, '"');
            }
            continue;
        }
        ++p->cur;
        if ('\\' != c) {
            if ('\n' == c) {
                p->line += 1;
            }
            status = buf_putc(out, c);
            continue;
        }
        if (p->cur >= p->end) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated escape");
        }
        c = *p->cur++;
        switch (c) {
        case '\r':
            /* line-ending backslash: trim whitespace up to the next
               non-whitespace or newline */
            if ((p->cur < p->end) && ('\n' == *p->cur)) {
                ++p->cur;
                p->line += 1;
            }
            /* fall through */
        case '\n':
            if ('\n' != c) {
                /* \r handled above with optional \n */
            } else {
                p->line += 1;
            }
            while ((p->cur < p->end)
                   && ((' ' == *p->cur) || ('\t' == *p->cur) || ('\r' == *p->cur)
                       || ('\n' == *p->cur))) {
                if ('\n' == *p->cur) {
                    p->line += 1;
                }
                ++p->cur;
            }
            break;
        case '"':
        case '\\':
        case '/':
            status = buf_putc(out, c);
            break;
        case 'b':
            status = buf_putc(out, '\b');
            break;
        case 'f':
            status = buf_putc(out, '\f');
            break;
        case 'n':
            status = buf_putc(out, '\n');
            break;
        case 'r':
            status = buf_putc(out, '\r');
            break;
        case 't':
            status = buf_putc(out, '\t');
            break;
        case 'u':
        case 'U': {
            uint32_t cp = 0;
            if ('u' == c) {
                status = p_hex4(p, &cp);
            } else {
                for (int i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < 4); ++i) {
                    uint32_t part = 0;
                    status = p_hex4(p, &part);
                    cp = (cp << 4) | part;
                }
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                break;
            }
            if ((0xD800u <= cp) && (cp <= 0xDBFFu)) {
                uint32_t low = 0;
                if (((p->end - p->cur) < 6) || ('\\' != p->cur[0]) || ('u' != p->cur[1])) {
                    status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                    break;
                }
                p->cur += 2;
                status = p_hex4(p, &low);
                if (SHREWD_BASIC_ERR_OK != status) {
                    break;
                }
                if (!((0xDC00u <= low) && (low <= 0xDFFFu))) {
                    status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                    break;
                }
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
            } else if ((0xDC00u <= cp) && (cp <= 0xDFFFu)) {
                status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                break;
            }
            status = utf8_emit(out, cp);
            break;
        }
        default:
            status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad escape in string");
            break;
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
}

static int p_basic_string(toml_parser_t *p, toml_buf_t *out) {
    int status = SHREWD_BASIC_ERR_OK;
    ++p->cur; /* opening quote */
    if (((p->end - p->cur) >= 2) && ('"' == p->cur[0]) && ('"' == p->cur[1])) {
        return p_multi_basic_string(p, out);
    }
    while ((SHREWD_BASIC_ERR_OK == status) && (p->cur < p->end)) {
        char c = *p->cur++;
        if ('"' == c) {
            return SHREWD_BASIC_ERR_OK;
        }
        if ('\n' == c) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "newline in string");
        }
        if ('\\' != c) {
            status = buf_putc(out, c);
            continue;
        }
        if (p->cur >= p->end) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated escape");
        }
        c = *p->cur++;
        switch (c) {
        case '"':
        case '\\':
        case '/':
            status = buf_putc(out, c);
            break;
        case 'b':
            status = buf_putc(out, '\b');
            break;
        case 'f':
            status = buf_putc(out, '\f');
            break;
        case 'n':
            status = buf_putc(out, '\n');
            break;
        case 'r':
            status = buf_putc(out, '\r');
            break;
        case 't':
            status = buf_putc(out, '\t');
            break;
        case 'u':
        case 'U': {
            uint32_t cp = 0;
            if ('u' == c) {
                status = p_hex4(p, &cp);
            } else {
                for (int i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < 4); ++i) {
                    uint32_t part = 0;
                    status = p_hex4(p, &part);
                    cp = (cp << 4) | part;
                }
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                break;
            }
            if ((0xD800u <= cp) && (cp <= 0xDBFFu)) {
                uint32_t low = 0;
                if (((p->end - p->cur) < 6) || ('\\' != p->cur[0]) || ('u' != p->cur[1])) {
                    status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                    break;
                }
                p->cur += 2;
                status = p_hex4(p, &low);
                if (SHREWD_BASIC_ERR_OK != status) {
                    break;
                }
                if (!((0xDC00u <= low) && (low <= 0xDFFFu))) {
                    status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                    break;
                }
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
            } else if ((0xDC00u <= cp) && (cp <= 0xDFFFu)) {
                status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                break;
            }
            status = utf8_emit(out, cp);
            break;
        }
        default:
            status = p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad escape in string");
            break;
        }
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated string");
}

static int p_multi_literal_string(toml_parser_t *p, toml_buf_t *out) {
    p->cur += 2; /* remaining two opener quotes (one already eaten) */
    /* First newline right after the opener is stripped. */
    if ((p->cur < p->end) && ('\r' == *p->cur)) {
        ++p->cur;
    }
    if ((p->cur < p->end) && ('\n' == *p->cur)) {
        ++p->cur;
        p->line += 1;
    }
    for (;;) {
        char c = '\0';
        int status = 0;
        if (p->cur >= p->end) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated multi-line string");
        }
        c = *p->cur;
        if ('\'' == c) {
            size_t consumed = 0;
            if (0 != p_quote_run(p, '\'', &consumed)) {
                for (size_t i = 0; i + 3 < consumed; ++i) {
                    status = buf_putc(out, '\'');
                    if (SHREWD_BASIC_ERR_OK != status) {
                        return status;
                    }
                }
                return SHREWD_BASIC_ERR_OK;
            }
            for (size_t i = 0; i < consumed; ++i) {
                status = buf_putc(out, '\'');
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            continue;
        }
        ++p->cur;
        if ('\n' == c) {
            p->line += 1;
        }
        status = buf_putc(out, c);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
}

static int p_literal_string(toml_parser_t *p, toml_buf_t *out) {
    ++p->cur; /* opening quote */
    if (((p->end - p->cur) >= 2) && ('\'' == p->cur[0]) && ('\'' == p->cur[1])) {
        return p_multi_literal_string(p, out);
    }
    while (p->cur < p->end) {
        char c = *p->cur++;
        int status = 0;
        if ('\'' == c) {
            return SHREWD_BASIC_ERR_OK;
        }
        if ('\n' == c) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "newline in string");
        }
        status = buf_putc(out, c);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated string");
}

/* --- decode: scalars ------------------------------------------------------------ */

/* Structural datetime shapes: dddd-dd-dd [Tt ]dd:dd:dd[.digits][offset]
   or a bare dd:dd:dd[.digits][offset]; the raw text is preserved. */
static int p_is_digit(char c) {
    return (('0' <= c) && ('9' >= c)) ? 1 : 0;
}

static int p_datetime(toml_parser_t *p, toml_vkind_t *kind, char **out_text, size_t *out_len) {
    const char *start = p->cur;
    const char *q = p->cur;
    int matched = 0;
    char *raw = NULL;
    size_t n = 0;
    /* date part */
    for (int i = 0; (i < 10) && (q < p->end); ++i, ++q) {
        if ((4 == i) || (7 == i)) {
            if ('-' != *q) {
                q = NULL;
                break;
            }
        } else if (0 == p_is_digit(*q)) {
            q = NULL;
            break;
        }
    }
    if (NULL == q) {
        /* maybe bare time */
        q = start;
        for (int i = 0; (i < 8) && (q < p->end); ++i, ++q) {
            if ((2 == i) || (5 == i)) {
                if (':' != *q) {
                    q = NULL;
                    break;
                }
            } else if (0 == p_is_digit(*q)) {
                q = NULL;
                break;
            }
        }
        if (NULL == q) {
            return 1; /* not a datetime */
        }
        matched = 1; /* bare time */
    }
    if (0 == matched) {
        /* optional separator + time */
        if ((q < p->end) && (('T' == *q) || ('t' == *q) || (' ' == *q))) {
            const char *after = q + 1;
            int ok = 1;
            for (int i = 0; (i < 8) && (0 != ok); ++i) {
                char c = (after + i < p->end) ? after[i] : '\0';
                if ((2 == i) || (5 == i)) {
                    ok = (':' == c) ? 1 : 0;
                } else {
                    ok = p_is_digit(c);
                }
            }
            if (0 != ok) {
                q = after + 8;
                matched = 1;
            }
        }
        if (0 == matched) {
            /* date alone */
            q = start + 10;
        }
    }
    if (0 != matched) {
        /* fraction */
        if ((q < p->end) && ('.' == *q)) {
            const char *frac = q + 1;
            size_t digits = 0;
            while ((frac < p->end) && (0 != p_is_digit(*frac))) {
                ++frac;
                ++digits;
            }
            if (0 == digits) {
                return 1;
            }
            q = frac;
        }
        /* offset */
        if ((q < p->end) && (('Z' == *q) || ('z' == *q))) {
            ++q;
        } else if ((q < p->end) && (('+' == *q) || ('-' == *q))) {
            const char *o = q + 1;
            int ok = p_is_digit((o < p->end) ? o[0] : '\0')
                     && p_is_digit((o + 1 < p->end) ? o[1] : '\0')
                     && ((o + 2 < p->end) && (':' == o[2]))
                     && p_is_digit((o + 3 < p->end) ? o[3] : '\0')
                     && p_is_digit((o + 4 < p->end) ? o[4] : '\0');
            if (0 == ok) {
                return 1;
            }
            q = o + 5;
        }
    }
    /* boundary: the shape must end at a value delimiter */
    if ((q < p->end) && (' ' != *q) && ('\t' != *q) && ('\n' != *q) && ('\r' != *q) && (',' != *q)
        && (']' != *q) && ('}' != *q) && ('#' != *q)) {
        return 1;
    }
    n = (size_t)(q - start);
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&raw, n + 1)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    memcpy(raw, start, n);
    raw[n] = '\0';
    p->cur = q;
    *kind = TV_DATETIME;
    *out_text = raw;
    *out_len = n;
    return 0;
}

static int p_numeric(toml_parser_t *p,
                     const char *tok,
                     size_t tok_len,
                     toml_vkind_t *kind,
                     char **out_text,
                     size_t *out_len) {
    /* Normalize TOML numeric spelling to what the text layer accepts. */
    char *norm = NULL;
    size_t n = 0;
    int negative = 0;
    size_t i = 0;
    int is_float = 0;
    size_t cap = tok_len + 32;
    for (size_t j = 1; j < tok_len; ++j) {
        if ((':' == tok[j]) || (('-' == tok[j]) && ('0' <= tok[j - 1]) && ('9' >= tok[j - 1]))) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "malformed number or datetime");
        }
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&norm, cap)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if ('-' == tok[0]) {
        negative = 1;
        norm[n++] = '-';
        i = 1;
    } else if ('+' == tok[0]) {
        i = 1;
    }
    if (((tok_len - i) > 2) && ('0' == tok[i]) && (('x' == tok[i + 1]) || ('X' == tok[i + 1]))) {
        /* 0x hex -> decimal (sign applies). */
        uint64_t acc = 0;
        i += 2;
        for (; i < tok_len; ++i) {
            char c = tok[i];
            uint64_t digit = 0;
            if ('_' == c) {
                continue;
            }
            if (('0' <= c) && ('9' >= c)) {
                digit = (uint64_t)(c - '0');
            } else if (('a' <= c) && ('f' >= c)) {
                digit = (uint64_t)(c - 'a' + 10);
            } else if (('A' <= c) && ('F' >= c)) {
                digit = (uint64_t)(c - 'A' + 10);
            } else {
                void *old = norm;
                (void)shrewd_basic_platform_free(&old);
                return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "malformed number");
            }
            acc = (acc << 4) | digit;
        }
        n = (size_t)snprintf(norm, cap, negative ? "-%llu" : "%llu", (unsigned long long)acc);
        *kind = TV_INT;
        *out_text = norm;
        *out_len = n;
        return SHREWD_BASIC_ERR_OK;
    }
    for (; i < tok_len; ++i) {
        char c = tok[i];
        if ('_' == c) {
            /* Underscores must sit between digits. */
            if ((n == (size_t)negative) || ((i + 1) >= tok_len) || ('0' > tok[i - 1])
                || ('9' < tok[i - 1]) || ('0' > tok[i + 1]) || ('9' < tok[i + 1])) {
                void *old = norm;
                (void)shrewd_basic_platform_free(&old);
                return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "malformed number");
            }
            continue;
        }
        if (('.' == c) || ('e' == c) || ('E' == c)) {
            is_float = 1;
        }
        norm[n++] = c;
    }
    if (n == (size_t)negative) {
        void *old = norm;
        (void)shrewd_basic_platform_free(&old);
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "malformed number");
    }
    norm[n] = '\0';
    *kind = (0 != is_float) ? TV_FLOAT : TV_INT;
    *out_text = norm;
    *out_len = n;
    return SHREWD_BASIC_ERR_OK;
}

static int p_scalar(toml_parser_t *p, toml_vkind_t *kind, char **out_text, size_t *out_len) {
    const char *tok = NULL;
    size_t tok_len = 0;
    *out_text = NULL;
    *out_len = 0;
    p_ws(p);
    if (p->cur >= p->end) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "value expected");
    }
    if ('"' == *p->cur) {
        toml_buf_t out;
        int status = 0;
        memset(&out, 0, sizeof(out));
        status = p_basic_string(p, &out);
        if (SHREWD_BASIC_ERR_OK != status) {
            if (NULL != out.data) {
                void *old = out.data;
                (void)shrewd_basic_platform_free(&old);
            }
            return status;
        }
        *kind = TV_STR;
        *out_text = out.data;
        *out_len = out.len;
        return SHREWD_BASIC_ERR_OK;
    }
    if ('\'' == *p->cur) {
        toml_buf_t out;
        int status = 0;
        memset(&out, 0, sizeof(out));
        status = p_literal_string(p, &out);
        if (SHREWD_BASIC_ERR_OK != status) {
            if (NULL != out.data) {
                void *old = out.data;
                (void)shrewd_basic_platform_free(&old);
            }
            return status;
        }
        *kind = TV_STR;
        *out_text = out.data;
        *out_len = out.len;
        return SHREWD_BASIC_ERR_OK;
    }
    if ('{' == *p->cur) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "inline tables not supported");
    }
    if ('[' == *p->cur) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "nested arrays not supported");
    }
    {
        int hit = p_datetime(p, kind, out_text, out_len);
        if (0 == hit) {
            return SHREWD_BASIC_ERR_OK;
        }
        if (0 > hit) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
    }
    tok = p->cur;
    while ((p->cur < p->end) && (' ' != *p->cur) && ('\t' != *p->cur) && ('\n' != *p->cur)
           && ('\r' != *p->cur) && (',' != *p->cur) && (']' != *p->cur) && ('}' != *p->cur)
           && ('{' != *p->cur) && ('#' != *p->cur)) {
        ++p->cur;
    }
    tok_len = (size_t)(p->cur - tok);
    if (0 == tok_len) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "value expected");
    }
    if ((4 == tok_len) && (0 == memcmp(tok, "true", 4))) {
        char *word = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&word, 5)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        memcpy(word, "true", 5);
        *kind = TV_BOOL;
        *out_text = word;
        *out_len = 4;
        return SHREWD_BASIC_ERR_OK;
    }
    if ((5 == tok_len) && (0 == memcmp(tok, "false", 5))) {
        char *word = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&word, 6)) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        memcpy(word, "false", 6);
        *kind = TV_BOOL;
        *out_text = word;
        *out_len = 5;
        return SHREWD_BASIC_ERR_OK;
    }
    return p_numeric(p, tok, tok_len, kind, out_text, out_len);
}

/* --- decode: tables and arrays ---------------------------------------------------- */

static toml_table_t *tables_find(toml_tables_t *tables, const void *obj, const void *type) {
    for (size_t i = 0; i < tables->count; ++i) {
        if ((tables->tables[i].obj == obj) && (tables->tables[i].type == type)) {
            return &tables->tables[i];
        }
    }
    return NULL;
}

static int tables_open(toml_tables_t *tables,
                       const void *obj,
                       const void *type,
                       size_t field_count,
                       toml_table_t **out,
                       shrewd_basic_toml_error_t *error) {
    toml_table_t *rec = tables_find(tables, obj, type);
    if (NULL != rec) {
        *out = rec;
        return SHREWD_BASIC_ERR_OK;
    }
    if (TOML_MAX_TABLES <= tables->count) {
        return toml_fail(error, 0, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "too many tables");
    }
    rec = &tables->tables[tables->count];
    rec->obj = obj;
    rec->type = type;
    rec->field_count = field_count;
    rec->seen = NULL;
    rec->used = NULL;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_calloc(
            (void **)&rec->seen, (0 != field_count) ? field_count : 1, 1)) {
        return toml_fail(error, 0, SHREWD_BASIC_ERR_MEM_NOMEM, "out of memory");
    }
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_calloc(
            (void **)&rec->used, (0 != field_count) ? field_count : 1, sizeof(size_t))) {
        return toml_fail(error, 0, SHREWD_BASIC_ERR_MEM_NOMEM, "out of memory");
    }
    tables->count += 1;
    *out = rec;
    return SHREWD_BASIC_ERR_OK;
}

static void tables_close(toml_tables_t *tables) {
    for (size_t i = 0; i < tables->count; ++i) {
        if (NULL != tables->tables[i].seen) {
            void *old = tables->tables[i].seen;
            (void)shrewd_basic_platform_free(&old);
        }
        if (NULL != tables->tables[i].used) {
            void *old = tables->tables[i].used;
            (void)shrewd_basic_platform_free(&old);
        }
    }
    tables->count = 0;
}

static int assign_scalar(toml_parser_t *p,
                         const shrewd_basic_reflect_field_t *field,
                         void *slot,
                         toml_vkind_t kind,
                         char *text,
                         size_t len) {
    int status = 0;
    if (0 == scalar_fits(kind, field)) {
        if (TV_DATETIME == kind) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "datetime needs a string field");
        }
        return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "value does not fit the field");
    }
    status = shrewd_basic_reflect_scan_text(field, slot, text, len);
    if (SHREWD_BASIC_ERR_OK != status) {
        return p_fail(p, status, "value does not fit the field");
    }
    return SHREWD_BASIC_ERR_OK;
}

static int p_value_into(toml_parser_t *p,
                        void *obj,
                        const shrewd_basic_reflect_field_t *field,
                        toml_tables_t *tables,
                        int in_inline);
static int p_key_assign(toml_parser_t *p,
                        void *obj,
                        const shrewd_basic_reflect_type_t *type,
                        toml_tables_t *tables,
                        int in_inline);
static int p_inline_table(toml_parser_t *p,
                          void *base,
                          const shrewd_basic_reflect_type_t *type,
                          toml_tables_t *tables);

static int p_array_into(toml_parser_t *p,
                        void *owner,
                        const shrewd_basic_reflect_field_t *field,
                        toml_tables_t *tables,
                        int in_inline) {
    int elem_struct
        = (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) ? 1 : 0;
    size_t first_kind = 0;
    int have_first = 0;
    size_t index = 0;
    shrewd_basic_reflect_field_t elem = elem_field(field);
    int status = 0;
    if ((0 != elem_struct)
        && (0u
            != (field->sb_refl_flags
                & (SHREWD_BASIC_REFLECT_FIELD_F_PTR | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL)))) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "array elements not supported");
    }
    if ((0 == elem_struct) && (0 == elem_supported(field))) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "array elements not supported");
    }
    ++p->cur; /* '[' */
    for (;;) {
        char *text = NULL;
        size_t len = 0;
        toml_vkind_t kind = TV_STR;
        void *slot = NULL;
        if (0 != in_inline) {
            p_ws(p);
            status = SHREWD_BASIC_ERR_OK;
        } else {
            status = p_skip_inline_ws(p);
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if ((p->cur < p->end) && (']' == *p->cur)) {
            ++p->cur;
            return SHREWD_BASIC_ERR_OK;
        }
        /* resolve the element slot (fixed overflow is loud) */
        if (0 != field_is_dyn(field)) {
            if (SHREWD_BASIC_ERR_OK != dyn_grow(field, owner, index + 1)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            slot = (unsigned char *)dyn_base(field, owner) + (index * field->sb_refl_stride);
        } else {
            if (index >= field->sb_refl_count) {
                return p_fail(
                    p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "array longer than the field");
            }
            slot = (unsigned char *)owner + field->sb_refl_offset + (index * field->sb_refl_stride);
        }
        if (0 != elem_struct) {
            if ((p->cur >= p->end) || ('{' != *p->cur)) {
                return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "mixed-type array");
            }
            status = p_inline_table(p, slot, field->sb_refl_type, tables);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        } else {
            status = p_scalar(p, &kind, &text, &len);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if ((0 == have_first) || ((size_t)kind == first_kind)) {
                if (0 == have_first) {
                    first_kind = (size_t)kind;
                    have_first = 1;
                }
            } else {
                void *old = text;
                (void)shrewd_basic_platform_free(&old);
                return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "mixed-type array");
            }
            status = assign_scalar(p, &elem, slot, kind, text, len);
            {
                void *old = text;
                (void)shrewd_basic_platform_free(&old);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        index += 1;
        if (0 != in_inline) {
            p_ws(p);
        } else {
            status = p_skip_inline_ws(p);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        if ((p->cur < p->end) && (',' == *p->cur)) {
            ++p->cur;
            continue;
        }
        if ((p->cur < p->end) && (']' == *p->cur)) {
            ++p->cur;
            return SHREWD_BASIC_ERR_OK;
        }
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "',' or ']' expected");
    }
}

/* Inline table: { key = value, ... } filling a struct (single line). */
static int p_inline_table(toml_parser_t *p,
                          void *base,
                          const shrewd_basic_reflect_type_t *type,
                          toml_tables_t *tables) {
    toml_table_t *rec = NULL;
    int status = SHREWD_BASIC_ERR_OK;
    ++p->cur; /* '{' */
    status = tables_open(tables, base, type, type->sb_refl_field_count, &rec, p->error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    p_ws(p);
    if ((p->cur < p->end) && ('}' == *p->cur)) {
        ++p->cur;
        return SHREWD_BASIC_ERR_OK;
    }
    for (;;) {
        status = p_key_assign(p, base, type, tables, 1);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        p_ws(p);
        if ((p->cur < p->end) && (',' == *p->cur)) {
            ++p->cur;
            p_ws(p);
            continue;
        }
        if ((p->cur < p->end) && ('}' == *p->cur)) {
            ++p->cur;
            return SHREWD_BASIC_ERR_OK;
        }
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "',' or '}' expected");
    }
}

/* Value dispatch: struct -> inline table, array -> [...], else scalar. */
static int p_value_into(toml_parser_t *p,
                        void *obj,
                        const shrewd_basic_reflect_field_t *field,
                        toml_tables_t *tables,
                        int in_inline) {
    void *slot = (unsigned char *)obj + field->sb_refl_offset;
    (void)in_inline;
    p_ws(p);
    if (p->cur >= p->end) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "value expected");
    }
    if ((0 != field_is_array(field)) || (0 != field_is_dyn(field))) {
        if ('[' != *p->cur) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "array value expected");
        }
        return p_array_into(p, obj, field, tables, in_inline);
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "pointer tables not supported");
        }
        if ('{' != *p->cur) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "inline table expected");
        }
        return p_inline_table(p, slot, field->sb_refl_type, tables);
    }
    if ('[' == *p->cur) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "array value for a non-array field");
    }
    if ('{' == *p->cur) {
        return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "inline table for a non-table field");
    }
    {
        char *value = NULL;
        size_t value_len = 0;
        toml_vkind_t kind = TV_STR;
        int status = p_scalar(p, &kind, &value, &value_len);
        if (SHREWD_BASIC_ERR_OK == status) {
            status = assign_scalar(p, field, slot, kind, value, value_len);
        }
        if (NULL != value) {
            void *old = value;
            (void)shrewd_basic_platform_free(&old);
        }
        return status;
    }
}

/*
 * key(.key)* = value into obj/type: dotted intermediates descend into
 * struct fields, the final segment assigns. Duplicate detection rides
 * the table records (seen: 1 key, 2 subtable).
 */
static int p_key_assign(toml_parser_t *p,
                        void *obj,
                        const shrewd_basic_reflect_type_t *type,
                        toml_tables_t *tables,
                        int in_inline) {
    toml_buf_t key;
    int status = p_key_segment(p, &key);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (;;) {
        const shrewd_basic_reflect_field_t *field = NULL;
        toml_table_t *rec = NULL;
        size_t index = 0;
        p_ws(p);
        if ((p->cur < p->end) && ('.' == *p->cur)) {
            /* intermediate segment: descend into a struct field */
            field = find_field(type, key.data, key.len);
            if ((NULL == field)
                || (SHREWD_BASIC_REFLECT_KIND_STRUCT != field->sb_refl_type->sb_refl_kind)
                || (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR))) {
                void *old = key.data;
                (void)shrewd_basic_platform_free(&old);
                return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "not a table");
            }
            status = tables_open(tables, obj, type, type->sb_refl_field_count, &rec, p->error);
            if (SHREWD_BASIC_ERR_OK != status) {
                void *old = key.data;
                (void)shrewd_basic_platform_free(&old);
                return status;
            }
            index = (size_t)(field - type->sb_refl_fields);
            if (0 == rec->seen[index]) {
                rec->seen[index] = 2;
            } else if (2 != rec->seen[index]) {
                void *old = key.data;
                (void)shrewd_basic_platform_free(&old);
                return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "duplicate key");
            }
            obj = (unsigned char *)obj + field->sb_refl_offset;
            type = field->sb_refl_type;
            {
                void *old = key.data;
                (void)shrewd_basic_platform_free(&old);
            }
            ++p->cur; /* '.' */
            status = p_key_segment(p, &key);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        /* final segment: '=' value */
        field = find_field(type, key.data, key.len);
        if (NULL == field) {
            void *old = key.data;
            (void)shrewd_basic_platform_free(&old);
            return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "unknown key");
        }
        index = (size_t)(field - type->sb_refl_fields);
        status = tables_open(tables, obj, type, type->sb_refl_field_count, &rec, p->error);
        if (SHREWD_BASIC_ERR_OK != status) {
            void *old = key.data;
            (void)shrewd_basic_platform_free(&old);
            return status;
        }
        if (0 != rec->seen[index]) {
            void *old = key.data;
            (void)shrewd_basic_platform_free(&old);
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "duplicate key");
        }
        rec->seen[index] = 1;
        {
            void *old = key.data;
            (void)shrewd_basic_platform_free(&old);
        }
        p_ws(p);
        if ((p->cur >= p->end) || ('=' != *p->cur)) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "'=' expected");
        }
        ++p->cur;
        return p_value_into(p, obj, field, tables, in_inline);
    }
}

/*
 * Table / array-table headers: [a.b] and [[a.b]]. Intermediate
 * segments descend (implicit super-tables allowed; a key-assigned
 * parent is a duplicate); array tables append one element per header
 * and later [a.b] paths descend into the LAST element (TOML 1.0).
 */
static int p_table_header(toml_parser_t *p,
                          void *root,
                          const shrewd_basic_reflect_type_t *root_type,
                          toml_tables_t *tables,
                          void **cur_obj,
                          const shrewd_basic_reflect_type_t **cur_type) {
    void *obj = root;
    const shrewd_basic_reflect_type_t *type = root_type;
    int aot = 0;
    int status = 0;
    ++p->cur; /* '[' */
    if ((p->cur < p->end) && ('[' == *p->cur)) {
        aot = 1;
        ++p->cur;
    }
    for (;;) {
        toml_buf_t key;
        const shrewd_basic_reflect_field_t *field = NULL;
        toml_table_t *rec = NULL;
        size_t index = 0;
        int is_final = 0;
        status = p_key_segment(p, &key);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        field = find_field(type, key.data, key.len);
        if (NULL == field) {
            void *old = key.data;
            (void)shrewd_basic_platform_free(&old);
            return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "unknown table name");
        }
        index = (size_t)(field - type->sb_refl_fields);
        p_ws(p);
        is_final = ((p->cur < p->end) && (']' == *p->cur)) ? 1 : 0;
        if (0 != is_final) {
            void *old = key.data;
            (void)shrewd_basic_platform_free(&old);
            if (0 != aot) {
                /* [[name]]: append one element of a struct array */
                void *base = NULL;
                size_t n = 0;
                if (((0 == field_is_array(field)) && (0 == field_is_dyn(field)))
                    || (SHREWD_BASIC_REFLECT_KIND_STRUCT != field->sb_refl_type->sb_refl_kind)) {
                    return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "not an array table");
                }
                status = tables_open(tables, obj, type, type->sb_refl_field_count, &rec, p->error);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                if ((1 == rec->seen[index]) || (2 == rec->seen[index])) {
                    return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "duplicate table");
                }
                rec->seen[index] = 3;
                if (0 != field_is_dyn(field)) {
                    n = dyn_count(field, obj);
                    if (SHREWD_BASIC_ERR_OK != dyn_grow(field, obj, n + 1)) {
                        return SHREWD_BASIC_ERR_MEM_NOMEM;
                    }
                    base = dyn_base(field, obj);
                } else {
                    n = rec->used[index];
                    if (n >= field->sb_refl_count) {
                        return p_fail(
                            p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "array longer than the field");
                    }
                    rec->used[index] = n + 1;
                    base = (unsigned char *)obj + field->sb_refl_offset;
                }
                ++p->cur; /* second ']' */
                if ((p->cur >= p->end) || (']' != *p->cur)) {
                    return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "']]' expected");
                }
                ++p->cur;
                *cur_obj = (unsigned char *)base + (n * field->sb_refl_stride);
                *cur_type = field->sb_refl_type;
                return p_eol(p);
            }
            if (SHREWD_BASIC_REFLECT_KIND_STRUCT != field->sb_refl_type->sb_refl_kind) {
                return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "not a table");
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                return p_fail(p, SHREWD_BASIC_ERR_JSON_UNSUPPORTED, "pointer tables not supported");
            }
            status = tables_open(tables, obj, type, type->sb_refl_field_count, &rec, p->error);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            if (0 != rec->seen[index]) {
                return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "duplicate table");
            }
            rec->seen[index] = 2;
            ++p->cur; /* ']' */
            *cur_obj = (unsigned char *)obj + field->sb_refl_offset;
            *cur_type = field->sb_refl_type;
            return p_eol(p);
        }
        {
            void *old = key.data;
            (void)shrewd_basic_platform_free(&old);
        }
        /* intermediate segment */
        status = tables_open(tables, obj, type, type->sb_refl_field_count, &rec, p->error);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (((0 != field_is_array(field)) || (0 != field_is_dyn(field)))
            && (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind)) {
            /* descend into the last [[array table]] element */
            void *base = NULL;
            size_t n = 0;
            if (3 != rec->seen[index]) {
                return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "no array table element");
            }
            if (0 != field_is_dyn(field)) {
                n = dyn_count(field, obj);
                base = dyn_base(field, obj);
            } else {
                n = rec->used[index];
                base = (unsigned char *)obj + field->sb_refl_offset;
            }
            if (0 == n) {
                return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "no array table element");
            }
            obj = (unsigned char *)base + ((n - 1) * field->sb_refl_stride);
            type = field->sb_refl_type;
        } else if ((SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind)
                   && (0u == (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR))
                   && (0 == field_is_array(field)) && (0 == field_is_dyn(field))) {
            if (0 == rec->seen[index]) {
                rec->seen[index] = 2;
            } else if (2 != rec->seen[index]) {
                return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "duplicate table");
            }
            obj = (unsigned char *)obj + field->sb_refl_offset;
            type = field->sb_refl_type;
        } else {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "not a table");
        }
        if ((p->cur >= p->end) || ('.' != *p->cur)) {
            return p_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "'.' expected");
        }
        ++p->cur;
    }
}

/* --- decode: main loop ------------------------------------------------------------- */

int shrewd_basic_toml_decode(const char *text,
                             size_t length,
                             void *object,
                             const shrewd_basic_reflect_type_t *type,
                             shrewd_basic_toml_error_t *error) {
    toml_parser_t p;
    toml_tables_t tables;
    void *cur_obj = object;
    const shrewd_basic_reflect_type_t *cur_type = type;
    int status = 0;
    if ((NULL == text) || (NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return toml_fail(error, 0, SHREWD_BASIC_ERR_ARG_INVALID, "root type must be a struct");
    }
    memset(object, 0, type->sb_refl_size);
    p.cur = text;
    p.end = text + length;
    p.line = 1;
    p.error = error;
    memset(&tables, 0, sizeof(tables));
    {
        toml_table_t *root_rec = NULL;
        status = tables_open(&tables, object, type, type->sb_refl_field_count, &root_rec, error);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    while (p.cur < p.end) {
        status = p_skip_inline_ws(&p);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        if (p.cur >= p.end) {
            break;
        }
        if ('[' == *p.cur) {
            status = p_table_header(&p, object, type, &tables, &cur_obj, &cur_type);
            if (SHREWD_BASIC_ERR_OK != status) {
                break;
            }
            continue;
        }
        status = p_key_assign(&p, cur_obj, cur_type, &tables, 0);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
        status = p_eol(&p);
        if (SHREWD_BASIC_ERR_OK != status) {
            break;
        }
    }
    tables_close(&tables);
    return status;
}

/* --- free --------------------------------------------------------------------------- */

static void free_fields(const shrewd_basic_reflect_type_t *type, void *object, size_t depth) {
    if (TOML_MAX_DEPTH <= depth) {
        return;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        unsigned char *at = (unsigned char *)object + field->sb_refl_offset;
        if ((0 != field_is_array(field)) || (0 != field_is_dyn(field))) {
            unsigned char *base = at;
            size_t count = field->sb_refl_count;
            if (0 != field_is_dyn(field)) {
                base = (unsigned char *)dyn_base(field, object);
                count = dyn_count(field, object);
            }
            if ((SHREWD_BASIC_REFLECT_KIND_STRING == field->sb_refl_type->sb_refl_kind)
                && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED))) {
                for (size_t j = 0; j < count; ++j) {
                    char *slot = NULL;
                    memcpy(&slot, base + (j * field->sb_refl_stride), sizeof(slot));
                    if (NULL != slot) {
                        void *old = slot;
                        (void)shrewd_basic_platform_free(&old);
                        /* zero the slot: cross-module free hand-off is
                           then safe (no double free on re-free) */
                        memset(base + (j * field->sb_refl_stride), 0, sizeof(slot));
                    }
                }
            } else if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
                for (size_t j = 0; j < count; ++j) {
                    free_fields(field->sb_refl_type, base + (j * field->sb_refl_stride), depth + 1);
                }
            }
            if (0 != field_is_dyn(field)) {
                void *old = base;
                (void)shrewd_basic_platform_free(&old);
                dyn_set_base(field, object, NULL);
                dyn_set_count(field, object, 0);
            }
            continue;
        }
        if (field_is_buffer(field)) {
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            continue;
        }
        if ((SHREWD_BASIC_REFLECT_KIND_STRING == field->sb_refl_type->sb_refl_kind)
            && (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED))) {
            char *slot = NULL;
            memcpy(&slot, at, sizeof(slot));
            if (NULL != slot) {
                void *old = slot;
                (void)shrewd_basic_platform_free(&old);
                /* zero the slot: cross-module free hand-off is then
                   safe (no double free on re-free) */
                memset(at, 0, sizeof(slot));
            }
            continue;
        }
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT == field->sb_refl_type->sb_refl_kind) {
            free_fields(field->sb_refl_type, at, depth + 1);
        }
    }
}

int shrewd_basic_toml_free(void *object, const shrewd_basic_reflect_type_t *type) {
    if ((NULL == object) || (NULL == type)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    free_fields(type, object, 0);
    return SHREWD_BASIC_ERR_OK;
}

/**
 * @file csv.c
 * @brief RFC 4180 CSV mapping over reflect-described struct tables.
 *
 * Structure layer only: columns are flattened leaf fields, and every
 * leaf conversion goes through the shared text layer
 * (shrewd_basic_reflect_scan_text / _render_text). Strict where RFC
 * 4180 is strict: records match the header width, stray quotes and
 * junk after a closing quote are errors; empty lines are ignored and
 * empty fields are written quoted so single-column tables round-trip.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/formats/csv.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/formats/json.h"
#include "shrewd-basic/reflect/reflect.h"
#include "shrewd-basic-internal/reflect-internal.h"

/*
 * Shared log module of the csv module. Logging policy: WARN for
 * argument misuse, TRACE for encode/decode progress; success is
 * silent.
 */
SHREWD_BASIC_LOG_MODULE_DEFINE(shrewd_basic_csv_log, "csv", SHREWD_BASIC_LOG_LEVEL_ALL);

#define CSV_MAX_COLUMNS ((size_t)128)
#define CSV_MAX_DEPTH ((size_t)8)
#define CSV_NAME_MAX ((size_t)128)

/* --- growable output buffer ------------------------------------------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} csv_buf_t;

static int csv_buf_reserve(csv_buf_t *buf, size_t extra) {
    void *block = NULL;
    size_t want = 0;
    if ((buf->len + extra + 1) <= buf->cap) {
        return SHREWD_BASIC_ERR_OK;
    }
    want = (0 != buf->cap) ? buf->cap : 256;
    while (want < (buf->len + extra + 1)) {
        want *= 2;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, buf->data, want)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    buf->data = block;
    buf->cap = want;
    return SHREWD_BASIC_ERR_OK;
}

static int csv_buf_put(csv_buf_t *buf, const char *data, size_t n) {
    int status = csv_buf_reserve(buf, n);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memcpy(buf->data + buf->len, data, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

static int csv_buf_putc(csv_buf_t *buf, char c) {
    return csv_buf_put(buf, &c, 1);
}

/* --- column model ------------------------------------------------------ */

typedef struct {
    const shrewd_basic_reflect_field_t *path[CSV_MAX_DEPTH];
    size_t depth;
    int owned_string;
    char name[CSV_NAME_MAX];
    size_t prefix_len;                       /* prefix bytes inside name */
    shrewd_basic_json_field_policy_t policy; /* merged policy aspects */
    int hidden;                              /* HIDDEN: suppressed both ways */
} csv_column_t;

/* Merged policy aspects for one member (rows merge per aspect, later
   rows win - the json policy semantics). */
static void csv_policy_get(const shrewd_basic_json_info_t *info,
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

/* Canonical leaf name for the wire: KEY row's first name, else the
   member name. */
static const char *csv_leaf_name(const shrewd_basic_json_field_policy_t *policy,
                                 const char *member) {
    if ((NULL != policy) && (NULL != policy->keys) && (0 < policy->key_count)) {
        return policy->keys[0];
    }
    return member;
}

/* A header cell matches the column's canonical name, its member name
   or any KEY alias (each with the same nesting prefix). */
static int csv_name_matches(const csv_column_t *column, const char *text, size_t len) {
    if ((len == strlen(column->name)) && (0 == memcmp(text, column->name, len))) {
        return 1;
    }
    if (0 != column->policy.key_count) {
        char cand[CSV_NAME_MAX];
        const shrewd_basic_json_field_policy_t *row = &column->policy;
        for (size_t k = 0; k <= row->key_count; ++k) {
            const char *leaf = (0 == k) ? row->field : row->keys[k - 1];
            size_t leaf_len = strlen(leaf);
            if ((column->prefix_len + leaf_len) < sizeof(cand)) {
                memcpy(cand, column->name, column->prefix_len);
                memcpy(cand + column->prefix_len, leaf, leaf_len + 1);
                if ((len == (column->prefix_len + leaf_len)) && (0 == memcmp(text, cand, len))) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

typedef struct {
    csv_column_t *items;
    size_t count;
    size_t cap;
} csv_columns_t;

static void csv_columns_release(csv_columns_t *columns) {
    if (NULL != columns->items) {
        (void)shrewd_basic_platform_free((void **)&columns->items);
    }
    columns->items = NULL;
    columns->count = 0;
    columns->cap = 0;
}

static int csv_columns_add(csv_columns_t *columns, const csv_column_t *column) {
    void *block = NULL;
    if (columns->count >= CSV_MAX_COLUMNS) {
        return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
    }
    if (columns->count == columns->cap) {
        size_t cap = (0 != columns->cap) ? (columns->cap * 2) : 8;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_realloc(
                &block, columns->items, cap * sizeof(*columns->items))) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        columns->items = block;
        columns->cap = cap;
    }
    columns->items[columns->count] = *column;
    columns->count += 1;
    return SHREWD_BASIC_ERR_OK;
}

static int csv_fail(
    shrewd_basic_csv_error_t *error, size_t line, int status, const char *fmt, const char *arg) {
    if (NULL != error) {
        error->line = line;
        (void)snprintf(error->message, sizeof(error->message), fmt, (NULL != arg) ? arg : "");
    }
    return status;
}

/* Resolve the leaf address of @p column inside @p base. */
static unsigned char *csv_column_at(const csv_column_t *column, void *base) {
    unsigned char *at = (unsigned char *)base;
    for (size_t i = 0; i < column->depth; ++i) {
        at += column->path[i]->sb_refl_offset;
    }
    return at;
}

/*
 * Flatten one struct type into leaf columns. Nested structs recurse
 * with a dotted prefix; anything without text semantics is a loud
 * rejection (the row shape must round-trip). @p draft carries the
 * path chain under construction (depth = chain length so far).
 */
static int csv_collect(const shrewd_basic_reflect_type_t *type,
                       const shrewd_basic_json_info_t *info,
                       csv_column_t *draft,
                       const char *prefix,
                       csv_columns_t *columns,
                       shrewd_basic_csv_error_t *error) {
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const shrewd_basic_reflect_type_t *value_type = field->sb_refl_type;
        uint32_t flags = field->sb_refl_flags;
        int status = 0;
        shrewd_basic_json_field_policy_t policy;
        csv_policy_get(info, field->sb_refl_name, &policy);
        draft->path[draft->depth] = field;
        draft->policy = policy;
        draft->prefix_len = strlen(prefix);
        draft->hidden = (0u != (policy.flags & SHREWD_BASIC_JSON_F_HIDDEN)) ? 1 : 0;
        if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            return csv_fail(error,
                            0,
                            SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                            "union field '%s' has no CSV columns",
                            field->sb_refl_name);
        }
        if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
            return csv_fail(error,
                            0,
                            SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                            "bit group '%s' has no CSV columns",
                            field->sb_refl_name);
        }
        if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            /* Text buffer: the flag carries the shape (the kind may
               read as CHAR through the _Generic decay). */
            int written = snprintf(draft->name,
                                   sizeof(draft->name),
                                   "%s%s",
                                   prefix,
                                   csv_leaf_name(&policy, field->sb_refl_name));
            if ((size_t)written >= sizeof(draft->name)) {
                return csv_fail(error,
                                0,
                                SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                "column name too long near '%s'",
                                field->sb_refl_name);
            }
            draft->depth += 1;
            draft->owned_string = 0;
            status = csv_columns_add(columns, draft);
            draft->depth -= 1;
            if (SHREWD_BASIC_ERR_OK != status) {
                return csv_fail(error, 0, status, "too many columns (limit 128)", NULL);
            }
            continue;
        }
        if (0u
            != (flags
                & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY
                   | SHREWD_BASIC_REFLECT_FIELD_F_SENTINEL | SHREWD_BASIC_REFLECT_FIELD_F_FLEX))) {
            return csv_fail(error,
                            0,
                            SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                            "array field '%s' has no CSV columns",
                            field->sb_refl_name);
        }
        if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
            return csv_fail(error,
                            0,
                            SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                            "pointer field '%s' has no CSV columns",
                            field->sb_refl_name);
        }
        if (SHREWD_BASIC_REFLECT_KIND_STRUCT == value_type->sb_refl_kind) {
            char prefix_next[CSV_NAME_MAX];
            int written
                = snprintf(prefix_next, sizeof(prefix_next), "%s%s.", prefix, field->sb_refl_name);
            if ((size_t)written >= sizeof(prefix_next)) {
                return csv_fail(error,
                                0,
                                SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                "column name too long near '%s'",
                                field->sb_refl_name);
            }
            if (CSV_MAX_DEPTH <= (draft->depth + 1)) {
                return csv_fail(error,
                                0,
                                SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                "struct nesting deeper than 8",
                                NULL);
            }
            draft->depth += 1;
            status = csv_collect(value_type, info, draft, prefix_next, columns, error);
            draft->depth -= 1;
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            continue;
        }
        if (SHREWD_BASIC_REFLECT_KIND_STRING == value_type->sb_refl_kind) {
            if (0u != (flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED)) {
                int written = snprintf(draft->name,
                                       sizeof(draft->name),
                                       "%s%s",
                                       prefix,
                                       csv_leaf_name(&policy, field->sb_refl_name));
                if ((size_t)written >= sizeof(draft->name)) {
                    return csv_fail(error,
                                    0,
                                    SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                    "column name too long near '%s'",
                                    field->sb_refl_name);
                }
                draft->depth += 1;
                draft->owned_string = 1;
                status = csv_columns_add(columns, draft);
                draft->depth -= 1;
                if (SHREWD_BASIC_ERR_OK != status) {
                    return csv_fail(error, 0, status, "too many columns (limit 128)", NULL);
                }
                continue;
            }
            return csv_fail(error,
                            0,
                            SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                            "borrowed string '%s' needs stringbuf or owned",
                            field->sb_refl_name);
        }
        if ((SHREWD_BASIC_REFLECT_KIND_STRING16 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_STRING32 == value_type->sb_refl_kind)) {
            return csv_fail(error,
                            0,
                            SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                            "wide string '%s' has no CSV columns",
                            field->sb_refl_name);
        }
        if ((SHREWD_BASIC_REFLECT_KIND_BOOL == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_CHAR == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_INT8 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_UINT8 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_INT16 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_UINT16 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_INT32 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_UINT32 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_INT64 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_UINT64 == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_ENUM == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_FLOAT == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_DOUBLE == value_type->sb_refl_kind)
            || (SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE == value_type->sb_refl_kind)) {
            int written = snprintf(draft->name,
                                   sizeof(draft->name),
                                   "%s%s",
                                   prefix,
                                   csv_leaf_name(&policy, field->sb_refl_name));
            if ((size_t)written >= sizeof(draft->name)) {
                return csv_fail(error,
                                0,
                                SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                                "column name too long near '%s'",
                                field->sb_refl_name);
            }
            draft->depth += 1;
            draft->owned_string = 0;
            status = csv_columns_add(columns, draft);
            draft->depth -= 1;
            if (SHREWD_BASIC_ERR_OK != status) {
                return csv_fail(error, 0, status, "too many columns (limit 128)", NULL);
            }
            continue;
        }
        return csv_fail(error,
                        0,
                        SHREWD_BASIC_ERR_JSON_UNSUPPORTED,
                        "field '%s' has no CSV columns",
                        field->sb_refl_name);
    }
    return SHREWD_BASIC_ERR_OK;
}

static int csv_columns_build(const shrewd_basic_reflect_type_t *row_type,
                             const shrewd_basic_json_info_t *info,
                             csv_columns_t *columns,
                             shrewd_basic_csv_error_t *error) {
    csv_column_t draft;
    int status = 0;
    memset(&draft, 0, sizeof(draft));
    memset(columns, 0, sizeof(*columns));
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != row_type->sb_refl_kind) {
        return csv_fail(error, 0, SHREWD_BASIC_ERR_ARG_INVALID, "row type is not a struct", NULL);
    }
    status = csv_collect(row_type, info, &draft, "", columns, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        csv_columns_release(columns);
    }
    return status;
}

/* --- field writing (RFC 4180 quoting) --------------------------------- */

static int csv_quoted(const char *text, size_t n) {
    if (0 == n) {
        return 1; /* empty fields are written quoted (round-trips 1-col) */
    }
    for (size_t i = 0; i < n; ++i) {
        if ((',' == text[i]) || ('"' == text[i]) || ('\r' == text[i]) || ('\n' == text[i])) {
            return 1;
        }
    }
    return 0;
}

static int csv_write_field(csv_buf_t *buf, const char *text, size_t n) {
    int status = 0;
    if (0 == csv_quoted(text, n)) {
        return csv_buf_put(buf, text, n);
    }
    status = csv_buf_putc(buf, '"');
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < n); ++i) {
        if ('"' == text[i]) {
            status = csv_buf_put(buf, "\"\"", 2);
        } else {
            status = csv_buf_putc(buf, text[i]);
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = csv_buf_putc(buf, '"');
    }
    return status;
}

/* --- record parsing ---------------------------------------------------- */

typedef struct {
    const char *data;
    size_t len;
} csv_span_t;

/*
 * Parse one record at *pos into cells (at most max_cells; more cells
 * is an error). Empty lines are skipped. Returns KEY_NOTFOUND at a
 * clean end of input. *line tracks the 1-based source line.
 */
static int csv_next_record(const char *text,
                           size_t length,
                           size_t *pos,
                           size_t *line,
                           csv_span_t *cells,
                           size_t max_cells,
                           size_t *out_count) {
    size_t count = 0;
    *out_count = 0;
    /* Empty lines carry no information: skip them. */
    for (;;) {
        if (*pos >= length) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        if ('\n' == text[*pos]) {
            *pos += 1;
            *line += 1;
            continue;
        }
        if (('\r' == text[*pos]) && ((*pos + 1) < length) && ('\n' == text[*pos + 1])) {
            *pos += 2;
            *line += 1;
            continue;
        }
        break;
    }
    for (;;) {
        csv_span_t cell;
        if (count >= max_cells) {
            *out_count = count;
            return SHREWD_BASIC_ERR_JSON_SYNTAX; /* record too wide */
        }
        if ('"' == text[*pos]) {
            /* Quoted field: content until the closing quote; "" is a
               literal quote. Embedded newlines stay verbatim. */
            size_t i = *pos + 1;
            size_t start = i;
            size_t k = 0;
            size_t w = 0;
            size_t raw_len = 0;
            void *block = NULL;
            for (;;) {
                if (i >= length) {
                    *out_count = count;
                    return SHREWD_BASIC_ERR_JSON_SYNTAX; /* unterminated quote */
                }
                if ('"' == text[i]) {
                    if (((i + 1) < length) && ('"' == text[i + 1])) {
                        i += 2;
                        continue;
                    }
                    break;
                }
                if ('\n' == text[i]) {
                    *line += 1;
                }
                i += 1;
            }
            raw_len = i - start;
            if (0 != memchr(text + start, '"', raw_len)) {
                /* Doubled quotes present: unescape into a heap copy. */
                if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, raw_len + 1)) {
                    return SHREWD_BASIC_ERR_MEM_NOMEM;
                }
                while (k < raw_len) {
                    ((char *)block)[w] = text[start + k];
                    k += ('"' == text[start + k]) ? 2 : 1;
                    w += 1;
                }
                ((char *)block)[w] = '\0';
                cell.data = block;
                cell.len = w;
            } else {
                cell.data = text + start;
                cell.len = raw_len;
            }
            *pos = i + 1; /* past the closing quote */
            cells[count] = cell;
            count += 1;
            if (*pos >= length) {
                break; /* record ends at EOF */
            }
            if (',' == text[*pos]) {
                *pos += 1;
                if (*pos >= length) {
                    /* trailing comma: one more empty field */
                    cells[count].data = text + *pos;
                    cells[count].len = 0;
                    count += 1;
                    break;
                }
                continue;
            }
            if (('\r' == text[*pos]) && ((*pos + 1) < length) && ('\n' == text[*pos + 1])) {
                *pos += 2;
            } else if (('\n' == text[*pos]) || ('\r' == text[*pos])) {
                *pos += 1;
            } else {
                *out_count = count;
                return SHREWD_BASIC_ERR_JSON_SYNTAX; /* junk after closing quote */
            }
            *line += 1;
            break;
        }
        {
            /* Unquoted field: up to comma or line end; a stray quote
               is an error (RFC 4180 quotes only open a field). */
            size_t start = *pos;
            while (*pos < length) {
                char c = text[*pos];
                if ((',' == c) || ('\n' == c) || ('\r' == c)) {
                    break;
                }
                if ('"' == c) {
                    *out_count = count;
                    return SHREWD_BASIC_ERR_JSON_SYNTAX;
                }
                *pos += 1;
            }
            cell.data = text + start;
            cell.len = *pos - start;
            cells[count] = cell;
            count += 1;
            if (*pos >= length) {
                break;
            }
            if (',' == text[*pos]) {
                *pos += 1;
                if (*pos >= length) {
                    /* trailing comma: one more empty field */
                    cells[count].data = text + *pos;
                    cells[count].len = 0;
                    count += 1;
                    break;
                }
                continue;
            }
            if (('\r' == text[*pos]) && ((*pos + 1) < length) && ('\n' == text[*pos + 1])) {
                *pos += 2;
            } else {
                *pos += 1;
            }
            *line += 1;
            break;
        }
    }
    *out_count = count;
    return SHREWD_BASIC_ERR_OK;
}

/* Quoted cells were unescaped into heap copies; the parser can't tell
   callers which those are. Track them by pointer provenance at the
   call site instead: decode frees any cell pointer outside [text,
   text+length). Helper: */
static int csv_cell_owned(const char *text, size_t length, const csv_span_t *cell) {
    return ((cell->data < text) || (cell->data >= (text + length))) ? 1 : 0;
}

static void csv_cells_release(const char *text, size_t length, csv_span_t *cells, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (0 != csv_cell_owned(text, length, &cells[i])) {
            void *block = (void *)(uintptr_t)cells[i].data;
            (void)shrewd_basic_platform_free(&block);
        }
    }
}

/* --- encode ------------------------------------------------------------ */

int shrewd_basic_csv_encode(const void *rows,
                            size_t count,
                            const shrewd_basic_reflect_type_t *row_type,
                            const shrewd_basic_json_info_t *info,
                            char **out_text,
                            shrewd_basic_csv_error_t *error) {
    csv_columns_t columns;
    csv_buf_t buf;
    size_t written_cells = 0;
    int status = 0;
    if ((NULL == row_type) || (NULL == out_text)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_csv_log,
                         "encode",
                         "encode: row type or out_text is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((NULL == rows) && (0 != count)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_csv_log,
                         "encode",
                         "encode: rows is NULL with count > 0\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_text = NULL;
    if (NULL != error) {
        error->line = 0;
        error->message[0] = '\0';
    }
    status = csv_columns_build(row_type, info, &columns, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    memset(&buf, 0, sizeof(buf));
    written_cells = 0;
    status = csv_buf_reserve(&buf, 1);
    if (SHREWD_BASIC_ERR_OK == status) {
        buf.data[0] = '\0';
    }
    /* Header line (HIDDEN columns never surface). */
    for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < columns.count); ++i) {
        if (0 != columns.items[i].hidden) {
            continue;
        }
        if (0 != written_cells) {
            status = csv_buf_putc(&buf, ',');
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = csv_write_field(&buf, columns.items[i].name, strlen(columns.items[i].name));
        }
        written_cells += 1;
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = csv_buf_putc(&buf, '\n');
    }
    /* Data rows. */
    for (size_t r = 0; (SHREWD_BASIC_ERR_OK == status) && (r < count); ++r) {
        const unsigned char *base = (const unsigned char *)rows + (r * row_type->sb_refl_size);
        written_cells = 0;
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < columns.count); ++i) {
            const csv_column_t *column = &columns.items[i];
            const unsigned char *at = csv_column_at(column, (void *)(uintptr_t)base);
            if (0 != column->hidden) {
                continue;
            }
            char stack[256];
            char *cell = stack;
            char *heap = NULL;
            size_t need = 0;
            int rendered = shrewd_basic_reflect_render_text(
                column->path[column->depth - 1], at, stack, sizeof(stack), &need);
            if (SHREWD_BASIC_ERR_REFLECT_MISMATCH == rendered) {
                if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc((void **)&heap, need + 1)) {
                    status = SHREWD_BASIC_ERR_MEM_NOMEM;
                    break;
                }
                rendered = shrewd_basic_reflect_render_text(
                    column->path[column->depth - 1], at, heap, need + 1, &need);
                cell = heap;
            }
            if (SHREWD_BASIC_ERR_OK != rendered) {
                status = csv_fail(
                    error, r + 2, rendered, "cannot render column '%.90s'", column->name);
            } else if (0 != written_cells) {
                status = csv_buf_putc(&buf, ',');
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = csv_write_field(&buf, cell, need);
                }
            } else {
                status = csv_write_field(&buf, cell, need);
            }
            written_cells += 1;
            if (NULL != heap) {
                (void)shrewd_basic_platform_free((void **)&heap);
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = csv_buf_putc(&buf, '\n');
        }
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        if (NULL != buf.data) {
            (void)shrewd_basic_platform_free((void **)&buf.data);
        }
        csv_columns_release(&columns);
        return status;
    }
    *out_text = buf.data;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_csv_log,
                     "encode",
                     "encode ok (%zu rows, %zu columns)\n",
                     count,
                     columns.count);
    csv_columns_release(&columns);
    return SHREWD_BASIC_ERR_OK;
}

/* --- decode ------------------------------------------------------------ */

int shrewd_basic_csv_decode(const char *text,
                            size_t length,
                            void **out_rows,
                            size_t *out_count,
                            const shrewd_basic_reflect_type_t *row_type,
                            const shrewd_basic_json_info_t *info,
                            shrewd_basic_csv_error_t *error) {
    csv_columns_t columns;
    csv_span_t cells[CSV_MAX_COLUMNS + 1];
    size_t map[CSV_MAX_COLUMNS];
    size_t header_width = 0;
    size_t pos = 0;
    size_t line = 1;
    size_t count = 0;
    size_t cap = 0;
    void *rows = NULL;
    int status = 0;
    if ((NULL == text) || (NULL == out_rows) || (NULL == out_count) || (NULL == row_type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_csv_log,
                         "decode",
                         "decode: text, out_rows, out_count or row type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_rows = NULL;
    *out_count = 0;
    if (NULL != error) {
        error->line = 0;
        error->message[0] = '\0';
    }
    status = csv_columns_build(row_type, info, &columns, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    /* Header: map each header cell to its column (by name or alias). */
    {
        size_t n = 0;
        status = csv_next_record(text, length, &pos, &line, cells, CSV_MAX_COLUMNS + 1, &n);
        if (SHREWD_BASIC_ERR_KEY_NOTFOUND == status) {
            csv_columns_release(&columns);
            return csv_fail(error, line, SHREWD_BASIC_ERR_JSON_SYNTAX, "missing header", NULL);
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            csv_columns_release(&columns);
            return csv_fail(error, line, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad header", NULL);
        }
        if (n > CSV_MAX_COLUMNS) {
            csv_cells_release(text, length, cells, n);
            csv_columns_release(&columns);
            return csv_fail(error, 1, SHREWD_BASIC_ERR_JSON_SYNTAX, "too many header cells", NULL);
        }
        header_width = n;
        for (size_t i = 0; i < n; ++i) {
            size_t found = columns.count; /* sentinel: none */
            for (size_t j = 0; j < columns.count; ++j) {
                if (0 != csv_name_matches(&columns.items[j], cells[i].data, cells[i].len)) {
                    found = j;
                    break;
                }
            }
            if ((found < columns.count) && (0 != columns.items[found].hidden)) {
                map[i] = columns.count; /* HIDDEN: known but never decoded */
                continue;
            }
            if (found >= columns.count) {
                char bad[64];
                size_t copy = (cells[i].len < (sizeof(bad) - 1)) ? cells[i].len : (sizeof(bad) - 1);
                memcpy(bad, cells[i].data, copy);
                bad[copy] = '\0';
                csv_cells_release(text, length, cells, n);
                csv_columns_release(&columns);
                return csv_fail(
                    error, 1, SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH, "unknown column '%s'", bad);
            }
            for (size_t k = 0; k < i; ++k) {
                if (map[k] == found) {
                    int failure = csv_fail(error,
                                           1,
                                           SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                                           "duplicate column '%s'",
                                           columns.items[found].name);
                    csv_cells_release(text, length, cells, n);
                    csv_columns_release(&columns);
                    return failure;
                }
            }
            map[i] = found;
        }
        csv_cells_release(text, length, cells, n);
    }
    /* REQUIRED policy rows demand their column in the header. */
    for (size_t j = 0; j < columns.count; ++j) {
        const csv_column_t *column = &columns.items[j];
        int present = 0;
        if ((0 != column->hidden)
            || (0u == (column->policy.flags & SHREWD_BASIC_JSON_F_REQUIRED))) {
            continue;
        }
        for (size_t i = 0; i < header_width; ++i) {
            if (map[i] == j) {
                present = 1;
                break;
            }
        }
        if (0 == present) {
            csv_columns_release(&columns);
            return csv_fail(error,
                            1,
                            SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH,
                            "missing required column '%.90s'",
                            column->name);
        }
    }
    /* Data records. */
    for (;;) {
        size_t n = 0;
        size_t record_line = line;
        unsigned char *row = NULL;
        status = csv_next_record(text, length, &pos, &line, cells, CSV_MAX_COLUMNS + 1, &n);
        if (SHREWD_BASIC_ERR_KEY_NOTFOUND == status) {
            break;
        }
        if (SHREWD_BASIC_ERR_OK != status) {
            csv_cells_release(text, length, cells, n);
            goto fail;
        }
        if (n > CSV_MAX_COLUMNS) {
            csv_cells_release(text, length, cells, n);
            status = csv_fail(
                error, record_line, SHREWD_BASIC_ERR_JSON_SYNTAX, "record too wide", NULL);
            goto fail;
        }
        if (n != header_width) {
            csv_cells_release(text, length, cells, n);
            status = csv_fail(error,
                              record_line,
                              SHREWD_BASIC_ERR_JSON_SYNTAX,
                              "record field count does not match the header",
                              NULL);
            goto fail;
        }
        if (count == cap) {
            size_t want = (0 != cap) ? (cap * 2) : 8;
            void *block = NULL;
            size_t bytes = want * row_type->sb_refl_size;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, rows, bytes)) {
                csv_cells_release(text, length, cells, n);
                status = SHREWD_BASIC_ERR_MEM_NOMEM;
                goto fail;
            }
            rows = block;
            memset((unsigned char *)rows + (cap * row_type->sb_refl_size),
                   0,
                   (want - cap) * row_type->sb_refl_size);
            cap = want;
        }
        row = (unsigned char *)rows + (count * row_type->sb_refl_size);
        for (size_t i = 0; i < n; ++i) {
            const csv_column_t *column = NULL;
            unsigned char *at = NULL;
            if (map[i] >= columns.count) {
                continue; /* HIDDEN column: known, never decoded */
            }
            column = &columns.items[map[i]];
            at = csv_column_at(column, row);
            status = shrewd_basic_reflect_scan_text(
                column->path[column->depth - 1], at, cells[i].data, cells[i].len);
            if (SHREWD_BASIC_ERR_OK != status) {
                csv_cells_release(text, length, cells, n);
                (void)csv_fail(
                    error, record_line, status, "bad value for column '%.90s'", column->name);
                goto fail;
            }
        }
        csv_cells_release(text, length, cells, n);
        count += 1;
    }
    *out_rows = rows;
    *out_count = count;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_csv_log,
                     "decode",
                     "decode ok (%zu rows, %zu columns)\n",
                     count,
                     columns.count);
    csv_columns_release(&columns);
    return SHREWD_BASIC_ERR_OK;

fail:
    (void)shrewd_basic_csv_rows_free(rows, count, row_type);
    csv_columns_release(&columns);
    return status;
}

/* --- free -------------------------------------------------------------- */

int shrewd_basic_csv_rows_free(void *rows,
                               size_t count,
                               const shrewd_basic_reflect_type_t *row_type) {
    csv_columns_t columns;
    int status = 0;
    if ((NULL == rows) && (0 == count)) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL == rows) || (NULL == row_type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_csv_log,
                         "free",
                         "free: rows or row type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = csv_columns_build(row_type, NULL, &columns, NULL);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (size_t r = 0; r < count; ++r) {
        unsigned char *base = (unsigned char *)rows + (r * row_type->sb_refl_size);
        for (size_t i = 0; i < columns.count; ++i) {
            const csv_column_t *column = &columns.items[i];
            if (0 != column->owned_string) {
                unsigned char *at = csv_column_at(column, base);
                void *pointer = reflect_load_ptr(at);
                if (NULL != pointer) {
                    (void)shrewd_basic_platform_free(&pointer);
                    memset(at, 0, sizeof(void *));
                }
            }
        }
    }
    csv_columns_release(&columns);
    (void)shrewd_basic_platform_free(&rows);
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_csv_text_free(char *text) {
    if (NULL != text) {
        (void)shrewd_basic_platform_free((void **)&text);
    }
}

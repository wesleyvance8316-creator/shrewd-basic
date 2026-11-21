#include "shrewd-basic/reflect/reflect.h"

#include <stdio.h>
#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared reflect log module, defined in library/reflect/reflect.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

/* Defensive recursion cap (same bound as the graph walkers). */
#define EQUAL_MAX_DEPTH 16

/*
 * Pairs of objects currently being compared. Re-entering a pair means a
 * cycle; the two graphs are then equal by construction (bisimulation),
 * which terminates comparison of cyclic data.
 */
typedef struct equal_pair {
    const void *a;
    const void *b;
} equal_pair_t;

static int equal_pair_seen(const equal_pair_t *path, size_t depth, const void *a, const void *b)
{
    for (size_t i = 0; i < depth; ++i) {
        if ((path[i].a == a) && (path[i].b == b)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Optional path report for shrewd_basic_reflect_diff(). The walk pushes
 * one segment per descent and restores it on success, so when a mismatch
 * stops the recursion the buffer is frozen at the first difference's
 * path. NULL means "no report" (the plain equal walk).
 */
typedef struct diff_path {
    char *buf;
    size_t cap;
    size_t len;
} diff_path_t;

static size_t diff_path_mark(const diff_path_t *diff)
{
    return (NULL != diff) ? diff->len : 0;
}

static void diff_path_restore(diff_path_t *diff, size_t mark)
{
    if (NULL != diff) {
        diff->len = mark;
        diff->buf[mark] = '\0';
    }
}

static void diff_path_append(diff_path_t *diff, const char *text)
{
    size_t room = (diff->cap - 1) - diff->len;
    size_t used = strlen(text);
    if (used > room) {
        used = room;
    }
    memcpy(diff->buf + diff->len, text, used);
    diff->len += used;
    diff->buf[diff->len] = '\0';
}

static void diff_path_member(diff_path_t *diff, const char *name)
{
    if (NULL == diff) {
        return;
    }
    if (0 != diff->len) {
        diff_path_append(diff, ".");
    }
    diff_path_append(diff, name);
}

static void diff_path_index(diff_path_t *diff, size_t index)
{
    char text[24] = {0};
    if (NULL == diff) {
        return;
    }
    (void)snprintf(text, sizeof(text), "[%zu]", index);
    diff_path_append(diff, text);
}

static int reflect_equal_fields(const shrewd_basic_reflect_field_t *fields,
                                size_t count,
                                const unsigned char *a,
                                const unsigned char *b,
                                equal_pair_t *path,
                                size_t depth,
                                diff_path_t *diff,
                                bool *out_equal);

static int reflect_equal_value(const shrewd_basic_reflect_type_t *type,
                               const unsigned char *a,
                               const unsigned char *b,
                               size_t nbytes,
                               const shrewd_basic_reflect_ops_t *ops,
                               equal_pair_t *path,
                               size_t depth,
                               diff_path_t *diff,
                               bool *out_equal)
{
    if (EQUAL_MAX_DEPTH <= depth) {
        *out_equal = true;
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->equal)) {
        return ops->equal(a, b, out_equal);
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        *out_equal = ((0 != reflect_load_u64(a, nbytes)) == (0 != reflect_load_u64(b, nbytes)));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        *out_equal = (reflect_load_i64(a, nbytes) == reflect_load_i64(b, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        *out_equal = (reflect_load_u64(a, nbytes) == reflect_load_u64(b, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        *out_equal = (reflect_load_i64(a, nbytes) == reflect_load_i64(b, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        *out_equal = (reflect_load_f64(a, nbytes) == reflect_load_f64(b, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *left = reflect_load_str(a);
        const char *right = reflect_load_str(b);
        if ((NULL == left) || (NULL == right)) {
            *out_equal = (left == right);
        } else {
            *out_equal = (0 == strcmp(left, right));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        /* Without ops the only meaningful comparison is identity. */
        *out_equal = (a == b);
        return SHREWD_BASIC_ERR_OK;
    default:
        break;
    }
    /* Fixed array values (rows of a multi-dimension array). */
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            size_t mark_j = diff_path_mark(diff);
            bool same = true;
            int status = 0;
            diff_path_index(diff, i);
            status = reflect_equal_value(type->sb_refl_elem,
                                         a + (i * type->sb_refl_elem_stride),
                                         b + (i * type->sb_refl_elem_stride),
                                         type->sb_refl_elem_stride,
                                         type->sb_refl_elem->sb_refl_ops,
                                         path,
                                         depth + 1,
                                         diff,
                                         &same);
            if ((SHREWD_BASIC_ERR_OK != status) || !same) {
                *out_equal = false;
                return status;
            }
            diff_path_restore(diff, mark_j);
        }
        *out_equal = true;
        return SHREWD_BASIC_ERR_OK;
    }
    /* Structs (and unknown kinds): compare field by field. */
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        *out_equal = (0 == memcmp(a, b, nbytes));
        return SHREWD_BASIC_ERR_OK;
    }
    return reflect_equal_fields(
        type->sb_refl_fields, type->sb_refl_field_count, a, b, path, depth, diff, out_equal);
}

/* Compare one field list - a struct's fields or an active union variant's. */
static int reflect_equal_fields(const shrewd_basic_reflect_field_t *fields,
                                size_t count,
                                const unsigned char *a,
                                const unsigned char *b,
                                equal_pair_t *path,
                                size_t depth,
                                diff_path_t *diff,
                                bool *out_equal)
{
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        reflect_array_view_t view_a = reflect_array_view(a, field);
        reflect_array_view_t view_b = reflect_array_view(b, field);
        size_t mark = diff_path_mark(diff);
        int indexed
            = (0u
               != (field->sb_refl_flags
                   & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)));
        diff_path_member(diff, field->sb_refl_name);
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            /*
             * Tagged union: the tags must match and only the active
             * variant compares. An unknown tag has no semantics to walk,
             * so its raw bytes stand in.
             */
            const unsigned char *ua = a + field->sb_refl_offset;
            const unsigned char *ub = b + field->sb_refl_offset;
            int64_t tag_a
                = reflect_load_i64(a + field->sb_refl_limit_offset, field->sb_refl_limit_size);
            int64_t tag_b
                = reflect_load_i64(b + field->sb_refl_limit_offset, field->sb_refl_limit_size);
            const shrewd_basic_reflect_variant_t *variant = NULL;
            if (tag_a != tag_b) {
                *out_equal = false;
                return SHREWD_BASIC_ERR_OK;
            }
            variant = reflect_union_variant(field, ua);
            if (NULL == variant) {
                if (0 != memcmp(ua, ub, field->sb_refl_stride)) {
                    *out_equal = false;
                    return SHREWD_BASIC_ERR_OK;
                }
            } else {
                bool same = true;
                int status = reflect_equal_fields(variant->sb_refl_fields,
                                                  variant->sb_refl_field_count,
                                                  ua,
                                                  ub,
                                                  path,
                                                  depth + 1,
                                                  diff,
                                                  &same);
                if ((SHREWD_BASIC_ERR_OK != status) || !same) {
                    *out_equal = false;
                    return status;
                }
            }
            diff_path_restore(diff, mark);
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            /* Text buffers compare by content up to the first NUL. */
            const char *text_a = (const char *)(a + field->sb_refl_offset);
            const char *text_b = (const char *)(b + field->sb_refl_offset);
            size_t len_a = reflect_strn_len(text_a, field->sb_refl_count);
            size_t len_b = reflect_strn_len(text_b, field->sb_refl_count);
            if ((len_a != len_b) || (0 != memcmp(text_a, text_b, len_a))) {
                *out_equal = false;
                return SHREWD_BASIC_ERR_OK;
            }
            diff_path_restore(diff, mark);
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
            /* Bit groups compare their bits only; other unit bits are
               invisible. */
            uint64_t unit_a = reflect_load_u64(a + field->sb_refl_offset, field->sb_refl_size);
            uint64_t unit_b = reflect_load_u64(b + field->sb_refl_offset, field->sb_refl_size);
            if (reflect_bits_extract(unit_a, field->sb_refl_bit_offset, field->sb_refl_bit_width)
                != reflect_bits_extract(
                    unit_b, field->sb_refl_bit_offset, field->sb_refl_bit_width)) {
                *out_equal = false;
                return SHREWD_BASIC_ERR_OK;
            }
            diff_path_restore(diff, mark);
            continue;
        }
        if (view_a.count != view_b.count) {
            *out_equal = false;
            return SHREWD_BASIC_ERR_OK;
        }
        for (size_t j = 0; j < view_a.count; ++j) {
            const unsigned char *elem_a = view_a.base + (j * view_a.stride);
            const unsigned char *elem_b = view_b.base + (j * view_b.stride);
            size_t mark_j = diff_path_mark(diff);
            if (0 != indexed) {
                diff_path_index(diff, j);
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                const void *target_a = reflect_load_ptr(elem_a);
                const void *target_b = reflect_load_ptr(elem_b);
                if ((NULL == target_a) && (NULL == target_b)) {
                    diff_path_restore(diff, mark_j);
                    continue;
                }
                if ((NULL == target_a) || (NULL == target_b)) {
                    *out_equal = false;
                    return SHREWD_BASIC_ERR_OK;
                }
                if (0 != equal_pair_seen(path, depth, target_a, target_b)) {
                    diff_path_restore(diff, mark_j);
                    continue;
                }
                path[depth].a = target_a;
                path[depth].b = target_b;
                bool same = true;
                int status = reflect_equal_value(field->sb_refl_type,
                                                 (const unsigned char *)target_a,
                                                 (const unsigned char *)target_b,
                                                 field->sb_refl_type->sb_refl_size,
                                                 field_ops,
                                                 path,
                                                 depth + 1,
                                                 diff,
                                                 &same);
                if ((SHREWD_BASIC_ERR_OK != status) || !same) {
                    *out_equal = false;
                    return status;
                }
            } else {
                bool same = true;
                int status = reflect_equal_value(field->sb_refl_type,
                                                 elem_a,
                                                 elem_b,
                                                 view_a.stride,
                                                 field_ops,
                                                 path,
                                                 depth + 1,
                                                 diff,
                                                 &same);
                if ((SHREWD_BASIC_ERR_OK != status) || !same) {
                    *out_equal = false;
                    return status;
                }
            }
            diff_path_restore(diff, mark_j);
        }
        diff_path_restore(diff, mark);
    }
    *out_equal = true;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_equal(const void *a,
                               const void *b,
                               const shrewd_basic_reflect_type_t *type,
                               bool *out_equal)
{
    if ((NULL == a) || (NULL == b) || (NULL == type) || (NULL == out_equal)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_reflect_log,
                         "field",
                         "equal: a, b, type or out_equal is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    equal_pair_t path[EQUAL_MAX_DEPTH] = {0};
    bool same = true;
    /* Zeroed pairs: walks may skip levels (union variants). */
    memset(path, 0, sizeof(path));
    int status = reflect_equal_value(type,
                                     (const unsigned char *)a,
                                     (const unsigned char *)b,
                                     type->sb_refl_size,
                                     type->sb_refl_ops,
                                     path,
                                     0,
                                     NULL,
                                     &same);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    *out_equal = same;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_reflect_log,
                     "field",
                     "equal: %s (%s)\n",
                     (same ? "yes" : "no"),
                     type->sb_refl_name);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_diff(const void *a,
                              const void *b,
                              const shrewd_basic_reflect_type_t *type,
                              char *path_out,
                              size_t path_out_size)
{
    if ((NULL == a) || (NULL == b) || (NULL == type)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_reflect_log,
                         "diff",
                         "diff: a, b or type is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    diff_path_t report = {0};
    diff_path_t *trace = NULL;
    report.buf = path_out;
    report.cap = path_out_size;
    report.len = 0;
    if ((NULL != path_out) && (0 < path_out_size)) {
        path_out[0] = '\0';
        trace = &report;
    }
    equal_pair_t path[EQUAL_MAX_DEPTH] = {0};
    bool same = true;
    /* Zeroed pairs: walks may skip levels (union variants). */
    memset(path, 0, sizeof(path));
    int status = reflect_equal_value(type,
                                     (const unsigned char *)a,
                                     (const unsigned char *)b,
                                     type->sb_refl_size,
                                     type->sb_refl_ops,
                                     path,
                                     0,
                                     trace,
                                     &same);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (same) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != trace) && (0 == report.len)) {
        /* The roots themselves compared unequal (root ops->equal). */
        diff_path_append(trace, "(root)");
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_reflect_log,
                     "diff",
                     "diff: first difference at %s (%s)\n",
                     (NULL != trace) ? path_out : "(unreported)",
                     type->sb_refl_name);
    return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
}

/* --- diff_all: report every difference ------------------------------- */

/*
 * Like reflect_diff() but keeps walking: every mismatch is reported
 * through the callback with the same path syntax, leaf/shape semantics
 * mirror the equal walk (bit groups compare their bits, string buffers
 * their text, unions their active variant and tag, cyclic pairs are
 * visited once). The callback returns SHREWD_BASIC_ERR_OK to continue
 * and any other code to abort the walk.
 */
typedef struct diff_all_ctx {
    shrewd_basic_reflect_diff_fn fn;
    void *ctx;
} diff_all_ctx_t;

static int diff_all_report(const diff_all_ctx_t *all,
                           const diff_path_t *diff,
                           shrewd_basic_reflect_diff_kind_t kind)
{
    const char *path = (0 != diff->len) ? diff->buf : "(root)";
    return all->fn(all->ctx, path, kind);
}

static int diff_all_value(const shrewd_basic_reflect_type_t *type,
                          const unsigned char *a,
                          const unsigned char *b,
                          size_t nbytes,
                          const shrewd_basic_reflect_ops_t *ops,
                          equal_pair_t *pair_path,
                          size_t depth,
                          diff_path_t *diff,
                          const diff_all_ctx_t *all);

static int diff_all_fields(const shrewd_basic_reflect_field_t *fields,
                           size_t count,
                           const unsigned char *a,
                           const unsigned char *b,
                           equal_pair_t *pair_path,
                           size_t depth,
                           diff_path_t *diff,
                           const diff_all_ctx_t *all)
{
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        reflect_array_view_t view_a = reflect_array_view(a, field);
        reflect_array_view_t view_b = reflect_array_view(b, field);
        size_t mark = diff_path_mark(diff);
        size_t shared = (view_a.count < view_b.count) ? view_a.count : view_b.count;
        int indexed
            = (0u
               != (field->sb_refl_flags
                   & (SHREWD_BASIC_REFLECT_FIELD_F_ARRAY | SHREWD_BASIC_REFLECT_FIELD_F_DARRAY)));
        int status = 0;
        diff_path_member(diff, field->sb_refl_name);
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            const unsigned char *ua = a + field->sb_refl_offset;
            const unsigned char *ub = b + field->sb_refl_offset;
            int64_t tag_a
                = reflect_load_i64(a + field->sb_refl_limit_offset, field->sb_refl_limit_size);
            int64_t tag_b
                = reflect_load_i64(b + field->sb_refl_limit_offset, field->sb_refl_limit_size);
            const shrewd_basic_reflect_variant_t *variant = NULL;
            if (tag_a != tag_b) {
                status = diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_SHAPE);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                diff_path_restore(diff, mark);
                continue;
            }
            variant = reflect_union_variant(field, ua);
            if (NULL == variant) {
                if (0 != memcmp(ua, ub, field->sb_refl_stride)) {
                    status = diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        return status;
                    }
                }
            } else {
                status = diff_all_fields(variant->sb_refl_fields,
                                         variant->sb_refl_field_count,
                                         ua,
                                         ub,
                                         pair_path,
                                         depth + 1,
                                         diff,
                                         all);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            diff_path_restore(diff, mark);
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            const char *text_a = (const char *)(a + field->sb_refl_offset);
            const char *text_b = (const char *)(b + field->sb_refl_offset);
            size_t len_a = reflect_strn_len(text_a, field->sb_refl_count);
            size_t len_b = reflect_strn_len(text_b, field->sb_refl_count);
            if ((len_a != len_b) || (0 != memcmp(text_a, text_b, len_a))) {
                status = diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            diff_path_restore(diff, mark);
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
            uint64_t unit_a = reflect_load_u64(a + field->sb_refl_offset, field->sb_refl_size);
            uint64_t unit_b = reflect_load_u64(b + field->sb_refl_offset, field->sb_refl_size);
            if (reflect_bits_extract(unit_a, field->sb_refl_bit_offset, field->sb_refl_bit_width)
                != reflect_bits_extract(
                    unit_b, field->sb_refl_bit_offset, field->sb_refl_bit_width)) {
                status = diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            diff_path_restore(diff, mark);
            continue;
        }
        if (view_a.count != view_b.count) {
            status = diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_SHAPE);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        for (size_t j = 0; j < shared; ++j) {
            const unsigned char *elem_a = view_a.base + (j * view_a.stride);
            const unsigned char *elem_b = view_b.base + (j * view_b.stride);
            size_t mark_j = diff_path_mark(diff);
            if (0 != indexed) {
                diff_path_index(diff, j);
            }
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                const void *target_a = reflect_load_ptr(elem_a);
                const void *target_b = reflect_load_ptr(elem_b);
                if ((NULL == target_a) && (NULL == target_b)) {
                    diff_path_restore(diff, mark_j);
                    continue;
                }
                if ((NULL == target_a) || (NULL == target_b)) {
                    status = diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_SHAPE);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        return status;
                    }
                    diff_path_restore(diff, mark_j);
                    continue;
                }
                if (0 != equal_pair_seen(pair_path, depth, target_a, target_b)) {
                    diff_path_restore(diff, mark_j);
                    continue;
                }
                pair_path[depth].a = target_a;
                pair_path[depth].b = target_b;
                status = diff_all_value(field->sb_refl_type,
                                        (const unsigned char *)target_a,
                                        (const unsigned char *)target_b,
                                        field->sb_refl_type->sb_refl_size,
                                        field_ops,
                                        pair_path,
                                        depth + 1,
                                        diff,
                                        all);
            } else {
                status = diff_all_value(field->sb_refl_type,
                                        elem_a,
                                        elem_b,
                                        view_a.stride,
                                        field_ops,
                                        pair_path,
                                        depth + 1,
                                        diff,
                                        all);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            diff_path_restore(diff, mark_j);
        }
        diff_path_restore(diff, mark);
    }
    return SHREWD_BASIC_ERR_OK;
}

static int diff_all_value(const shrewd_basic_reflect_type_t *type,
                          const unsigned char *a,
                          const unsigned char *b,
                          size_t nbytes,
                          const shrewd_basic_reflect_ops_t *ops,
                          equal_pair_t *pair_path,
                          size_t depth,
                          diff_path_t *diff,
                          const diff_all_ctx_t *all)
{
    int status = 0;
    if (EQUAL_MAX_DEPTH <= depth) {
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->equal)) {
        bool same = true;
        status = ops->equal(a, b, &same);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        return same ? SHREWD_BASIC_ERR_OK
                    : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return ((0 != reflect_load_u64(a, nbytes)) == (0 != reflect_load_u64(b, nbytes)))
                   ? SHREWD_BASIC_ERR_OK
                   : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
        return (reflect_load_i64(a, nbytes) == reflect_load_i64(b, nbytes))
                   ? SHREWD_BASIC_ERR_OK
                   : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        return (reflect_load_u64(a, nbytes) == reflect_load_u64(b, nbytes))
                   ? SHREWD_BASIC_ERR_OK
                   : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        return (reflect_load_f64(a, nbytes) == reflect_load_f64(b, nbytes))
                   ? SHREWD_BASIC_ERR_OK
                   : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *left = reflect_load_str(a);
        const char *right = reflect_load_str(b);
        int same
            = (NULL == left) ? (left == right) : ((NULL != right) && (0 == strcmp(left, right)));
        return same ? SHREWD_BASIC_ERR_OK
                    : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        return (a == b) ? SHREWD_BASIC_ERR_OK
                        : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            size_t mark_j = diff_path_mark(diff);
            diff_path_index(diff, i);
            status = diff_all_value(type->sb_refl_elem,
                                    a + (i * type->sb_refl_elem_stride),
                                    b + (i * type->sb_refl_elem_stride),
                                    type->sb_refl_elem_stride,
                                    type->sb_refl_elem->sb_refl_ops,
                                    pair_path,
                                    depth + 1,
                                    diff,
                                    all);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            diff_path_restore(diff, mark_j);
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        return (0 == memcmp(a, b, nbytes))
                   ? SHREWD_BASIC_ERR_OK
                   : diff_all_report(all, diff, SHREWD_BASIC_REFLECT_DIFF_VALUE);
    }
    return diff_all_fields(
        type->sb_refl_fields, type->sb_refl_field_count, a, b, pair_path, depth, diff, all);
}

int shrewd_basic_reflect_diff_all(const void *a,
                                  const void *b,
                                  const shrewd_basic_reflect_type_t *type,
                                  shrewd_basic_reflect_diff_fn fn,
                                  void *ctx)
{
    equal_pair_t pair_path[EQUAL_MAX_DEPTH] = {0};
    char path_buf[256] = {0};
    diff_path_t diff = {0};
    diff_all_ctx_t all = {0};
    if ((NULL == a) || (NULL == b) || (NULL == type) || (NULL == fn)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_reflect_log,
                         "diff",
                         "diff_all: a, b, type or fn is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(pair_path, 0, sizeof(pair_path));
    diff.buf = path_buf;
    diff.cap = sizeof(path_buf);
    diff.len = 0;
    all.fn = fn;
    all.ctx = ctx;
    return diff_all_value(type,
                          (const unsigned char *)a,
                          (const unsigned char *)b,
                          type->sb_refl_size,
                          type->sb_refl_ops,
                          pair_path,
                          0,
                          &diff,
                          &all);
}

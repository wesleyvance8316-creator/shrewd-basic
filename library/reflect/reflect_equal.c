#include "shrewd-basic/reflect/reflect.h"

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

static int equal_pair_seen(const equal_pair_t *path, size_t depth,
                           const void *a, const void *b)
{
    for (size_t i = 0; i < depth; ++i) {
        if ((path[i].a == a) && (path[i].b == b)) {
            return 1;
        }
    }
    return 0;
}

static int reflect_equal_value(const shrewd_basic_reflect_type_t *type,
                               const unsigned char *a, const unsigned char *b,
                               size_t nbytes,
                               const shrewd_basic_reflect_ops_t *ops,
                               equal_pair_t *path, size_t depth, bool *out_equal)
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
        *out_equal = ((0 != reflect_load_u64(a, nbytes))
                      == (0 != reflect_load_u64(b, nbytes)));
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
    /* Structs (and unknown kinds): compare field by field. */
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        *out_equal = (0 == memcmp(a, b, nbytes));
        return SHREWD_BASIC_ERR_OK;
    }
    for (size_t i = 0; i < type->sb_refl_field_count; ++i) {
        const shrewd_basic_reflect_field_t *field = &type->sb_refl_fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        reflect_array_view_t view_a = reflect_array_view(a, field);
        reflect_array_view_t view_b = reflect_array_view(b, field);
        if (view_a.count != view_b.count) {
            *out_equal = false;
            return SHREWD_BASIC_ERR_OK;
        }
        for (size_t j = 0; j < view_a.count; ++j) {
            const unsigned char *elem_a = view_a.base + (j * view_a.stride);
            const unsigned char *elem_b = view_b.base + (j * view_b.stride);
            if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                const void *target_a = reflect_load_ptr(elem_a);
                const void *target_b = reflect_load_ptr(elem_b);
                if ((NULL == target_a) && (NULL == target_b)) {
                    continue;
                }
                if ((NULL == target_a) || (NULL == target_b)) {
                    *out_equal = false;
                    return SHREWD_BASIC_ERR_OK;
                }
                if (0 != equal_pair_seen(path, depth, target_a, target_b)) {
                    continue;
                }
                path[depth].a = target_a;
                path[depth].b = target_b;
                bool same = true;
                int status = reflect_equal_value(field->sb_refl_type,
                                                 (const unsigned char *)target_a,
                                                 (const unsigned char *)target_b,
                                                 field->sb_refl_type->sb_refl_size,
                                                 field_ops, path, depth + 1, &same);
                if ((SHREWD_BASIC_ERR_OK != status) || !same) {
                    *out_equal = false;
                    return status;
                }
            } else {
                bool same = true;
                int status = reflect_equal_value(field->sb_refl_type, elem_a, elem_b,
                                                 view_a.stride, field_ops, path,
                                                 depth + 1, &same);
                if ((SHREWD_BASIC_ERR_OK != status) || !same) {
                    *out_equal = false;
                    return status;
                }
            }
        }
    }
    *out_equal = true;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_equal(const void *a, const void *b,
                               const shrewd_basic_reflect_type_t *type,
                               bool *out_equal)
{
    if ((NULL == a) || (NULL == b) || (NULL == type) || (NULL == out_equal)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_reflect_log,
                         "field", "equal: a, b, type or out_equal is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    equal_pair_t path[EQUAL_MAX_DEPTH];
    bool same = true;
    int status = reflect_equal_value(type, (const unsigned char *)a,
                                     (const unsigned char *)b, type->sb_refl_size,
                                     type->sb_refl_ops, path, 0, &same);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    *out_equal = same;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_reflect_log,
                     "field", "equal: %s (%s)\n", (same ? "yes" : "no"),
                     type->sb_refl_name);
    return SHREWD_BASIC_ERR_OK;
}

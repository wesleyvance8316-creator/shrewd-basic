/**
 * @file dbus_ops.c
 * @brief sb_dbus hook factories: permission guards and json-policy
 *        value hooks (docs/dbus-design.md §11).
 *
 * shrewd_basic_dbus_make_guard() fills pre_get/pre_set vetoes from an
 * allow mask; mount it as an interceptor to gate a subtree.
 *
 * Deviation note for shrewd_basic_dbus_make_policy_ops(): the design
 * asked for "policy rows in pre_set", but a pre_set hook cannot see
 * the incoming value (the public request API carries no value
 * accessor and dbus.h is frozen) and interceptor CORE hooks are never
 * invoked - only the primary mount's get/set run as CORE. The RANGE
 * checks therefore live in CORE get/set hooks used as a virtual-node
 * wrapper (mount with ops= and no data). The caller passes a ctx laid
 * out as two pointers, target first:
 *
 *     struct { void *target; const shrewd_basic_json_info_t *info; };
 *
 * target is the shadow storage the hooks read and write; RANGE rows
 * are honored when the request's field selector names the row's
 * field (whole-resource accesses pass through unchecked). Values
 * outside [min, max] are refused with SHREWD_BASIC_ERR_REFLECT_TEXT.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "shrewd-basic/config/config.h"
#include "shrewd-basic/dbus/dbus.h"

/* --- permission guards --------------------------------------------------- */

static int dbus_ops_deny(void *ctx, shrewd_basic_dbus_req_t *req) {
    (void)ctx;
    return shrewd_basic_dbus_req_deny(req, SHREWD_BASIC_ERR_KEY_NOTFOUND);
}

int shrewd_basic_dbus_make_guard(uint32_t allow_mask, shrewd_basic_dbus_ops_t *out) {
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (0u == (allow_mask & SHREWD_BASIC_DBUS_ALLOW_READ)) {
        out->pre_get = dbus_ops_deny;
    }
    if (0u == (allow_mask & SHREWD_BASIC_DBUS_ALLOW_WRITE)) {
        out->pre_set = dbus_ops_deny;
    }
    return SHREWD_BASIC_ERR_OK;
}

#if SHREWD_BASIC_FORMAT_JSON

#include "shrewd-basic/formats/json.h"

/* Caller-side ctx contract (two pointers, target first - see above). */
typedef struct {
    void *target;
    const shrewd_basic_json_info_t *info;
} dbus_ops_policy_ctx_t;

/*
 * Value slot for the request: whole resource or the named field. The
 * field selector resolves against the NODE type (carried by the policy
 * info) - req_type() is the resolved target's own type, not the
 * container.
 */
static int dbus_ops_slot(const dbus_ops_policy_ctx_t *pctx,
                         const shrewd_basic_dbus_req_t *req,
                         size_t *out_offset,
                         size_t *out_size) {
    const shrewd_basic_reflect_type_t *node_type = (NULL != pctx->info) ? pctx->info->type : NULL;
    const char *field = shrewd_basic_dbus_req_field(req);
    const shrewd_basic_reflect_field_t *desc = NULL;
    if (NULL == node_type) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == field) {
        *out_offset = 0;
        *out_size = node_type->sb_refl_size;
        return SHREWD_BASIC_ERR_OK;
    }
    return shrewd_basic_reflect_type_resolve(node_type, field, &desc, out_offset, out_size);
}

/* Merged RANGE row for a field (later rows win, like the runtime merge). */
static const shrewd_basic_json_field_policy_t *dbus_ops_range(const shrewd_basic_json_info_t *info,
                                                              const char *field) {
    const shrewd_basic_json_field_policy_t *found = NULL;
    if ((NULL == info) || (NULL == field)) {
        return NULL;
    }
    for (size_t i = 0; i < info->field_count; ++i) {
        const shrewd_basic_json_field_policy_t *row = &info->fields[i];
        if ((0u != (row->flags & SHREWD_BASIC_JSON_F_RANGE)) && (0 == strcmp(row->field, field))) {
            found = row;
        }
    }
    return found;
}

/* Read the incoming value as a number, dispatched by kind. */
static long double dbus_ops_number(const shrewd_basic_reflect_type_t *type,
                                   const void *in,
                                   size_t size) {
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_FLOAT: {
        float v = 0.0f;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double v = 0.0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        long double v = 0.0L;
        memcpy(&v, in, sizeof(v));
        return v;
    }
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
    case SHREWD_BASIC_REFLECT_KIND_UINT8: {
        uint8_t v = 0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT16: {
        uint16_t v = 0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT32: {
        uint32_t v = 0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT64: {
        uint64_t v = 0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
    case SHREWD_BASIC_REFLECT_KIND_INT8: {
        int8_t v = 0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_INT16: {
        int16_t v = 0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
    case SHREWD_BASIC_REFLECT_KIND_INT32: {
        int32_t v = 0;
        memcpy(&v, in, sizeof(v));
        return (long double)v;
    }
    default: {
        int64_t v = 0;
        size_t n = (sizeof(v) < size) ? sizeof(v) : size;
        memcpy(&v, in, n);
        return (long double)v;
    }
    }
}

static int dbus_ops_policy_get(void *ctx,
                               const shrewd_basic_dbus_req_t *req,
                               void *out,
                               size_t *size) {
    const dbus_ops_policy_ctx_t *pctx = (const dbus_ops_policy_ctx_t *)ctx;
    size_t offset = 0;
    size_t want = 0;
    int status = 0;
    if ((NULL == pctx) || (NULL == pctx->target) || (NULL == out) || (NULL == size)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = dbus_ops_slot(pctx, req, &offset, &want);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (*size < want) {
        *size = want;
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(out, (const unsigned char *)pctx->target + offset, want);
    *size = want;
    return SHREWD_BASIC_ERR_OK;
}

static int dbus_ops_policy_set(void *ctx,
                               const shrewd_basic_dbus_req_t *req,
                               const void *in,
                               size_t size) {
    const dbus_ops_policy_ctx_t *pctx = (const dbus_ops_policy_ctx_t *)ctx;
    size_t offset = 0;
    size_t want = 0;
    int status = 0;
    const shrewd_basic_json_field_policy_t *row = NULL;
    if ((NULL == pctx) || (NULL == pctx->target) || (NULL == in)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = dbus_ops_slot(pctx, req, &offset, &want);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (size != want) {
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    row = dbus_ops_range(pctx->info, shrewd_basic_dbus_req_field(req));
    if (NULL != row) {
        long double value = dbus_ops_number(shrewd_basic_dbus_req_type(req), in, size);
        if ((value < (long double)row->min) || (value > (long double)row->max)) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
    }
    memcpy((unsigned char *)pctx->target + offset, in, size);
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_dbus_make_policy_ops(const shrewd_basic_json_info_t *info,
                                      shrewd_basic_dbus_ops_t *out) {
    if ((NULL == info) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->get = dbus_ops_policy_get;
    out->set = dbus_ops_policy_set;
    return SHREWD_BASIC_ERR_OK;
}

#else /* !SHREWD_BASIC_FORMAT_JSON */

int shrewd_basic_dbus_make_policy_ops(const shrewd_basic_json_info_t *info,
                                      shrewd_basic_dbus_ops_t *out) {
    (void)info;
    (void)out;
    return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
}

#endif /* SHREWD_BASIC_FORMAT_JSON */

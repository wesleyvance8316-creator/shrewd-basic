/**
 * @file reflect_text.c
 * @brief Leaf text <-> value: the shared text layer for the text
 *        formats (INI/CSV/env/args/TOML).
 *
 * Strict both ways: scan consumes the whole text and never narrows or
 * truncates silently (SHREWD_BASIC_ERR_REFLECT_TEXT / _MISMATCH),
 * render is round-trip safe (floats at decimal-digit precision, enums
 * by constant name). The layer covers leaf kinds with text semantics -
 * scalars, ENUM and char strings (buffer or owned); borrowed/wide
 * strings, bit groups and aggregates are REFLECT_NOT_SUPPORTED (the
 * structure layer of each format handles those shapes).
 */

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-basic/reflect/reflect.h"
#include "shrewd-basic-internal/reflect-internal.h"

/* --- strict parsers ---------------------------------------------------- */

/* Optional '-', decimal digits, full consumption, range-checked. */
static int text_scan_int(const char *s, size_t n, int64_t lo, int64_t hi, int64_t *out) {
    size_t i = 0;
    int negative = 0;
    uint64_t acc = 0;
    if (0 == n) {
        return SHREWD_BASIC_ERR_REFLECT_TEXT;
    }
    if ('-' == s[0]) {
        negative = 1;
        i = 1;
    }
    if (i >= n) {
        return SHREWD_BASIC_ERR_REFLECT_TEXT;
    }
    for (; i < n; ++i) {
        uint64_t digit = 0;
        if (('0' > s[i]) || ('9' < s[i])) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        digit = (uint64_t)(s[i] - '0');
        if (acc > ((UINT64_MAX - digit) / 10u)) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        acc = (acc * 10u) + digit;
    }
    if (0 != negative) {
        if (acc > (uint64_t)INT64_MAX + 1u) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        *out = (acc == (uint64_t)INT64_MAX + 1u) ? INT64_MIN : -(int64_t)acc;
    } else {
        if (acc > (uint64_t)INT64_MAX) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        *out = (int64_t)acc;
    }
    if ((*out < lo) || (*out > hi)) {
        return SHREWD_BASIC_ERR_REFLECT_TEXT;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Decimal digits, full consumption, range-checked (no sign). */
static int text_scan_uint(const char *s, size_t n, uint64_t hi, uint64_t *out) {
    size_t i = 0;
    uint64_t acc = 0;
    if (0 == n) {
        return SHREWD_BASIC_ERR_REFLECT_TEXT;
    }
    for (; i < n; ++i) {
        uint64_t digit = 0;
        if (('0' > s[i]) || ('9' < s[i])) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        digit = (uint64_t)(s[i] - '0');
        if (acc > ((UINT64_MAX - digit) / 10u)) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        acc = (acc * 10u) + digit;
    }
    if (acc > hi) {
        return SHREWD_BASIC_ERR_REFLECT_TEXT;
    }
    *out = acc;
    return SHREWD_BASIC_ERR_OK;
}

/* Full-consumption float text; finite values only. */
static int text_scan_ld(const char *s, size_t n, long double *out) {
    char tmp[128];
    char *end = NULL;
    long double value = 0.0L;
    if ((0 == n) || (n >= sizeof(tmp))) {
        return SHREWD_BASIC_ERR_REFLECT_TEXT;
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    errno = 0;
    value = strtold(tmp, &end);
    if ((end != (tmp + n)) || (ERANGE == errno) || (0 == isfinite(value))) {
        return SHREWD_BASIC_ERR_REFLECT_TEXT;
    }
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

/* Signed width limits by kind. */
static int text_int_bounds(shrewd_basic_reflect_kind_t kind, int64_t *lo, int64_t *hi) {
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_INT8:
        *lo = INT8_MIN;
        *hi = INT8_MAX;
        return 1;
    case SHREWD_BASIC_REFLECT_KIND_INT16:
        *lo = INT16_MIN;
        *hi = INT16_MAX;
        return 1;
    case SHREWD_BASIC_REFLECT_KIND_INT32:
        *lo = INT32_MIN;
        *hi = INT32_MAX;
        return 1;
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        *lo = INT64_MIN;
        *hi = INT64_MAX;
        return 1;
    default:
        return 0;
    }
}

static uint64_t text_uint_limit(shrewd_basic_reflect_kind_t kind) {
    switch (kind) {
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
        return UINT8_MAX;
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
        return UINT16_MAX;
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
        return UINT32_MAX;
    default:
        return UINT64_MAX;
    }
}

/* --- render -------------------------------------------------------------- */

/* Copy content (len bytes) into buf with measure/truncation semantics. */
static int text_copy_out(
    const char *content, size_t len, char *buf, size_t buf_size, size_t *out_len) {
    if (NULL != out_len) {
        *out_len = len;
    }
    if (NULL == buf) {
        return SHREWD_BASIC_ERR_OK;
    }
    if (buf_size <= len) {
        if (0 < buf_size) {
            memcpy(buf, content, buf_size - 1);
            buf[buf_size - 1] = '\0';
        }
        return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
    }
    memcpy(buf, content, len);
    buf[len] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_render_text(const shrewd_basic_reflect_field_t *field,
                                     const void *value,
                                     char *buf,
                                     size_t buf_size,
                                     size_t *out_len) {
    const shrewd_basic_reflect_type_t *type = NULL;
    const shrewd_basic_reflect_ops_t *ops = NULL;
    size_t nbytes = 0;
    char tmp[64];
    int written = 0;
    if ((NULL == field) || (NULL == value)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    type = field->sb_refl_type;
    nbytes = field->sb_refl_stride;
    ops = reflect_field_ops(field);
    if ((NULL != ops) && (NULL != ops->render)) {
        if (NULL == buf) {
            return SHREWD_BASIC_ERR_ARG_INVALID;
        }
        int status = ops->render(value, buf, buf_size);
        if ((SHREWD_BASIC_ERR_OK == status) && (NULL != out_len)) {
            *out_len = strlen(buf);
        }
        return status;
    }
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_BITS | SHREWD_BASIC_REFLECT_FIELD_F_PTR))) {
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
    }
    if ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))
        && (1 == field->sb_refl_stride)) {
        /* Char text buffer: content to the NUL byte (the kind may read
           as CHAR via the _Generic decay - the flag carries the shape). */
        const char *text = (const char *)value;
        size_t len = reflect_strn_len(text, field->sb_refl_count);
        return text_copy_out(text, len, buf, buf_size, out_len);
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        return ((0 != reflect_load_u64(value, nbytes))
                    ? text_copy_out("true", 4, buf, buf_size, out_len)
                    : text_copy_out("false", 5, buf, buf_size, out_len));
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        memcpy(&c, value, 1);
        return text_copy_out(&c, 1, buf, buf_size, out_len);
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        int64_t number = reflect_load_i64(value, nbytes);
        const char *name = NULL;
        if (SHREWD_BASIC_REFLECT_KIND_ENUM == type->sb_refl_kind) {
            for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
                if (type->sb_refl_enum[i].sb_refl_value == number) {
                    name = type->sb_refl_enum[i].sb_refl_name;
                    break;
                }
            }
        }
        if (NULL != name) {
            return text_copy_out(name, strlen(name), buf, buf_size, out_len);
        }
        written = snprintf(tmp, sizeof(tmp), "%lld", (long long)number);
        return text_copy_out(tmp, (size_t)written, buf, buf_size, out_len);
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        written = snprintf(
            tmp, sizeof(tmp), "%llu", (unsigned long long)reflect_load_u64(value, nbytes));
        return text_copy_out(tmp, (size_t)written, buf, buf_size, out_len);
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
        written = snprintf(tmp, sizeof(tmp), "%.9g", reflect_load_f64(value, nbytes));
        return text_copy_out(tmp, (size_t)written, buf, buf_size, out_len);
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
        written = snprintf(tmp, sizeof(tmp), "%.17g", reflect_load_f64(value, nbytes));
        return text_copy_out(tmp, (size_t)written, buf, buf_size, out_len);
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE:
        written = snprintf(
            tmp, sizeof(tmp), "%.*Lg", (int)LDBL_DECIMAL_DIG, reflect_load_ld(value, nbytes));
        return text_copy_out(tmp, (size_t)written, buf, buf_size, out_len);
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = reflect_load_str(value);
        size_t len = (NULL != text) ? strlen(text) : 0;
        return text_copy_out((NULL != text) ? text : "", len, buf, buf_size, out_len);
    }
    default:
        break;
    }
    return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
}

/* --- scan ---------------------------------------------------------------- */

int shrewd_basic_reflect_scan_text(const shrewd_basic_reflect_field_t *field,
                                   void *value,
                                   const char *text,
                                   size_t length) {
    const shrewd_basic_reflect_type_t *type = NULL;
    const shrewd_basic_reflect_ops_t *ops = NULL;
    size_t nbytes = 0;
    if ((NULL == field) || (NULL == value) || ((NULL == text) && (0 != length))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    type = field->sb_refl_type;
    nbytes = field->sb_refl_stride;
    ops = reflect_field_ops(field);
    if ((NULL != ops) && (NULL != ops->scan)) {
        return ops->scan(value, text, length);
    }
    if (0u
        != (field->sb_refl_flags
            & (SHREWD_BASIC_REFLECT_FIELD_F_BITS | SHREWD_BASIC_REFLECT_FIELD_F_PTR))) {
        return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
    }
    if ((0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF))
        && (1 == field->sb_refl_stride)) {
        /* Char text buffer (flag carries the shape - see render_text). */
        if (length > field->sb_refl_count) {
            return SHREWD_BASIC_ERR_REFLECT_MISMATCH;
        }
        memcpy(value, text, length);
        if (length < field->sb_refl_count) {
            ((char *)value)[length] = '\0';
        }
        return SHREWD_BASIC_ERR_OK;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL: {
        uint64_t flag = 0;
        if ((4 == length) && (0 == memcmp(text, "true", 4))) {
            flag = 1;
        } else if ((5 == length) && (0 == memcmp(text, "false", 5))) {
            flag = 0;
        } else {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        reflect_store_u64(value, nbytes, flag);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_CHAR: {
        char c = '\0';
        if (1 != length) {
            return SHREWD_BASIC_ERR_REFLECT_TEXT;
        }
        c = text[0];
        memcpy(value, &c, 1);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64: {
        int64_t lo = 0;
        int64_t hi = 0;
        int64_t number = 0;
        int status = 0;
        (void)text_int_bounds(type->sb_refl_kind, &lo, &hi);
        status = text_scan_int(text, length, lo, hi, &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(value, nbytes, (uint64_t)number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64: {
        uint64_t number = 0;
        int status = text_scan_uint(text, length, text_uint_limit(type->sb_refl_kind), &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(value, nbytes, number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_ENUM: {
        char name[128];
        int64_t number = 0;
        int status = 0;
        if (length < sizeof(name)) {
            memcpy(name, text, length);
            name[length] = '\0';
            for (size_t i = 0; i < type->sb_refl_enum_count; ++i) {
                if (0 == strcmp(type->sb_refl_enum[i].sb_refl_name, name)) {
                    reflect_store_u64(value, nbytes, (uint64_t)type->sb_refl_enum[i].sb_refl_value);
                    return SHREWD_BASIC_ERR_OK;
                }
            }
        }
        /* Name miss: fall back to a plain decimal value. */
        status = text_scan_int(text, length, INT64_MIN, INT64_MAX, &number);
        if (SHREWD_BASIC_ERR_OK == status) {
            reflect_store_u64(value, nbytes, (uint64_t)number);
        }
        return status;
    }
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE:
    case SHREWD_BASIC_REFLECT_KIND_LONG_DOUBLE: {
        long double number = 0.0L;
        int status = text_scan_ld(text, length, &number);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (SHREWD_BASIC_REFLECT_KIND_FLOAT == type->sb_refl_kind) {
            float single = (float)number;
            if ((long double)single != number) {
                return SHREWD_BASIC_ERR_REFLECT_TEXT; /* never narrow silently */
            }
            memcpy(value, &single, sizeof(single));
        } else if (SHREWD_BASIC_REFLECT_KIND_DOUBLE == type->sb_refl_kind) {
            double real = (double)number;
            if ((long double)real != number) {
                return SHREWD_BASIC_ERR_REFLECT_TEXT;
            }
            memcpy(value, &real, sizeof(real));
        } else {
            memcpy(value, &number, sizeof(number));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRING == type->sb_refl_kind) {
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRING_OWNED)) {
            void *block = NULL;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, length + 1)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            memcpy(block, text, length);
            memcpy(value, &block, sizeof(block));
            return SHREWD_BASIC_ERR_OK;
        }
    }
    return SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED;
}

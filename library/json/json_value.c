#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/*
 * Shared log module of the json module (imported by the other json
 * translation units with SHREWD_BASIC_LOG_MODULE_DECLARE).
 *
 * Logging policy: WARN for argument misuse, TRACE for parse/serialize
 * progress; successful calls stay silent.
 */
SHREWD_BASIC_LOG_MODULE_DEFINE(shrewd_basic_json_log, "json", SHREWD_BASIC_LOG_LEVEL_ALL);

/* --- growable output buffer ------------------------------------------- */

int json_buf_init(json_buf_t *buf)
{
    void *block = NULL;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->nomem = 0;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, 128)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    buf->data = block;
    buf->cap = 128;
    buf->data[0] = '\0';
    return SHREWD_BASIC_ERR_OK;
}

void json_buf_release(json_buf_t *buf)
{
    if (NULL != buf->data) {
        (void)shrewd_basic_platform_free((void **)&buf->data);
    }
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

int json_buf_append(json_buf_t *buf, const char *text, size_t n)
{
    if (0 != buf->nomem) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    if ((buf->len + n + 1) > buf->cap) {
        size_t cap = buf->cap * 2;
        void *block = NULL;
        while (cap < (buf->len + n + 1)) {
            cap *= 2;
        }
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, buf->data, cap)) {
            buf->nomem = 1;
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

int json_buf_printf(json_buf_t *buf, const char *fmt, ...)
{
    va_list ap;
    va_list copy;
    int needed = 0;
    va_start(ap, fmt);
    va_copy(copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (0 > needed) {
        va_end(copy);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if ((buf->len + (size_t)needed + 1) > buf->cap) {
        size_t cap = buf->cap * 2;
        void *block = NULL;
        while (cap < (buf->len + (size_t)needed + 1)) {
            cap *= 2;
        }
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, buf->data, cap)) {
            buf->nomem = 1;
            va_end(copy);
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        buf->data = block;
        buf->cap = cap;
    }
    (void)vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, copy);
    va_end(copy);
    buf->len += (size_t)needed;
    return SHREWD_BASIC_ERR_OK;
}

int json_buf_string(json_buf_t *buf, const char *text)
{
    return json_buf_append(buf, text, strlen(text));
}

/* --- constructors ----------------------------------------------------- */

static int json_value_alloc(shrewd_basic_json_kind_t kind, shrewd_basic_json_value_t **out)
{
    shrewd_basic_json_value_t *node = NULL;
    void *block = NULL;
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_alloc(&block, sizeof(shrewd_basic_json_value_t))) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    node = block;
    node->kind = kind;
    /* Initialize only the live head of the union - the inline slots are
       written before they are read, so they skip the zeroing traffic. */
    switch (kind) {
    case SHREWD_BASIC_JSON_ARRAY:
        node->u.array.items = NULL;
        node->u.array.count = 0;
        node->u.array.cap = 0;
        break;
    case SHREWD_BASIC_JSON_OBJECT:
        node->u.object.entries = NULL;
        node->u.object.count = 0;
        node->u.object.cap = 0;
        break;
    case SHREWD_BASIC_JSON_BOOL:
        node->u.boolean = false;
        break;
    case SHREWD_BASIC_JSON_NUMBER:
        node->u.number.raw = NULL;
        break;
    case SHREWD_BASIC_JSON_STRING:
        node->u.string = NULL;
        break;
    default:
        break;
    }
    *out = node;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_new(shrewd_basic_json_kind_t kind, shrewd_basic_json_value_t **out)
{
    if ((SHREWD_BASIC_JSON_NULL != kind) && (SHREWD_BASIC_JSON_ARRAY != kind)
        && (SHREWD_BASIC_JSON_OBJECT != kind)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_json_log,
                         "value",
                         "new: kind %d needs a typed constructor\n",
                         (int)kind);
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    return json_value_alloc(kind, out);
}

int shrewd_basic_json_new_bool(bool value, shrewd_basic_json_value_t **out)
{
    int status = json_value_alloc(SHREWD_BASIC_JSON_BOOL, out);
    if (SHREWD_BASIC_ERR_OK == status) {
        (*out)->u.boolean = value;
    }
    return status;
}

/* Store a number as its exact token text. */
int json_value_new_number(const char *raw, size_t n, shrewd_basic_json_value_t **out)
{
    int status = json_value_alloc(SHREWD_BASIC_JSON_NUMBER, out);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (n < sizeof((*out)->local.bytes)) {
        /* Inline: short tokens avoid a malloc each. */
        memcpy((*out)->local.bytes, raw, n);
        (*out)->local.bytes[n] = '\0';
        (*out)->u.number.raw = (*out)->local.bytes;
        return SHREWD_BASIC_ERR_OK;
    }
    {
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, n + 1)) {
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        memcpy(block, raw, n);
        ((char *)block)[n] = '\0';
        (*out)->u.number.raw = block;
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_new_i64(int64_t value, shrewd_basic_json_value_t **out)
{
    char raw[32] = {0};
    int length = snprintf(raw, sizeof(raw), "%lld", (long long)value);
    return json_value_new_number(raw, (size_t)((0 < length) ? length : 0), out);
}

int shrewd_basic_json_new_u64(uint64_t value, shrewd_basic_json_value_t **out)
{
    char raw[32] = {0};
    int length = snprintf(raw, sizeof(raw), "%llu", (unsigned long long)value);
    return json_value_new_number(raw, (size_t)((0 < length) ? length : 0), out);
}

int shrewd_basic_json_new_f64(double value, shrewd_basic_json_value_t **out)
{
    char raw[40] = {0};
    static const char *const forms[] = {"%g", "%.15g", "%.16g", "%.17g"};
    if ((value != value) || (value > DBL_MAX) || (value < -DBL_MAX)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_json_log,
                         "value",
                         "new_f64: non-finite has no JSON form\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    /* Shortest rendering that reads back as the same double. */
    for (size_t i = 0; i < (sizeof(forms) / sizeof(forms[0])); ++i) {
        (void)snprintf(raw, sizeof(raw), forms[i], value);
        if (strtod(raw, NULL) == value) {
            return json_value_new_number(raw, strlen(raw), out);
        }
    }
    (void)snprintf(raw, sizeof(raw), "%.17g", value);
    return json_value_new_number(raw, strlen(raw), out);
}

int shrewd_basic_json_new_string_n(const char *text, size_t n, shrewd_basic_json_value_t **out)
{
    int status = json_value_alloc(SHREWD_BASIC_JSON_STRING, out);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (n < sizeof((*out)->local.bytes)) {
        /* Inline: short strings avoid a malloc each. */
        if ((NULL != text) && (0 < n)) {
            memcpy((*out)->local.bytes, text, n);
        }
        (*out)->local.bytes[n] = '\0';
        (*out)->u.string = (*out)->local.bytes;
        return SHREWD_BASIC_ERR_OK;
    }
    {
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, n + 1)) {
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        if ((NULL != text) && (0 < n)) {
            memcpy(block, text, n);
        }
        ((char *)block)[n] = '\0';
        (*out)->u.string = block;
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_new_string(const char *text, shrewd_basic_json_value_t **out)
{
    return shrewd_basic_json_new_string_n(text, (NULL != text) ? strlen(text) : 0, out);
}

void shrewd_basic_json_value_free(shrewd_basic_json_value_t *value)
{
    if (NULL == value) {
        return;
    }
    switch (value->kind) {
    case SHREWD_BASIC_JSON_NUMBER:
        if (value->u.number.raw != value->local.bytes) {
            (void)shrewd_basic_platform_free((void **)&value->u.number.raw);
        }
        break;
    case SHREWD_BASIC_JSON_STRING:
        if (value->u.string != value->local.bytes) {
            (void)shrewd_basic_platform_free((void **)&value->u.string);
        }
        break;
    case SHREWD_BASIC_JSON_ARRAY:
        for (size_t i = 0; i < value->u.array.count; ++i) {
            shrewd_basic_json_value_free(value->u.array.items[i]);
        }
        if (0 != value->u.array.cap) {
            (void)shrewd_basic_platform_free((void **)&value->u.array.items);
        }
        break;
    case SHREWD_BASIC_JSON_OBJECT:
        for (size_t i = 0; i < value->u.object.count; ++i) {
            (void)shrewd_basic_platform_free((void **)&value->u.object.entries[i].key);
            shrewd_basic_json_value_free(value->u.object.entries[i].value);
        }
        if (0 != value->u.object.cap) {
            (void)shrewd_basic_platform_free((void **)&value->u.object.entries);
        }
        break;
    default:
        break;
    }
    (void)shrewd_basic_platform_free((void **)&value);
}

/* --- inspection ------------------------------------------------------- */

shrewd_basic_json_kind_t shrewd_basic_json_kind(const shrewd_basic_json_value_t *value)
{
    return (NULL != value) ? value->kind : SHREWD_BASIC_JSON_NULL;
}

size_t shrewd_basic_json_count(const shrewd_basic_json_value_t *value)
{
    if (NULL == value) {
        return 0;
    }
    if (SHREWD_BASIC_JSON_ARRAY == value->kind) {
        return value->u.array.count;
    }
    if (SHREWD_BASIC_JSON_OBJECT == value->kind) {
        return value->u.object.count;
    }
    return 0;
}

shrewd_basic_json_value_t *shrewd_basic_json_at(const shrewd_basic_json_value_t *value,
                                                size_t index)
{
    if (NULL == value) {
        return NULL;
    }
    if (SHREWD_BASIC_JSON_ARRAY == value->kind) {
        return (index < value->u.array.count) ? value->u.array.items[index] : NULL;
    }
    if (SHREWD_BASIC_JSON_OBJECT == value->kind) {
        return (index < value->u.object.count) ? value->u.object.entries[index].value : NULL;
    }
    return NULL;
}

const char *shrewd_basic_json_key_at(const shrewd_basic_json_value_t *value, size_t index)
{
    if ((NULL == value) || (SHREWD_BASIC_JSON_OBJECT != value->kind)
        || (index >= value->u.object.count)) {
        return NULL;
    }
    return value->u.object.entries[index].key;
}

shrewd_basic_json_value_t *shrewd_basic_json_get(const shrewd_basic_json_value_t *value,
                                                 const char *key)
{
    if ((NULL == value) || (SHREWD_BASIC_JSON_OBJECT != value->kind) || (NULL == key)) {
        return NULL;
    }
    for (size_t i = 0; i < value->u.object.count; ++i) {
        if (0 == strcmp(value->u.object.entries[i].key, key)) {
            return value->u.object.entries[i].value;
        }
    }
    return NULL;
}

int shrewd_basic_json_to_bool(const shrewd_basic_json_value_t *value, bool *out)
{
    if ((NULL == value) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_JSON_BOOL != value->kind) {
        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
    }
    *out = value->u.boolean;
    return SHREWD_BASIC_ERR_OK;
}

/* Number token shape: 0 = integer form, 1 = has fraction or exponent. */
static int json_number_is_double(const char *raw)
{
    return (NULL != strpbrk(raw, ".eE")) ? 1 : 0;
}

int shrewd_basic_json_to_i64(const shrewd_basic_json_value_t *value, int64_t *out)
{
    if ((NULL == value) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_JSON_NUMBER != value->kind) {
        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
    }
    if (0 == json_number_is_double(value->u.number.raw)) {
        char *end = NULL;
        long long parsed = strtoll(value->u.number.raw, &end, 10);
        if ((NULL == end) || ('\0' != *end)) {
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
        *out = (int64_t)parsed;
        return SHREWD_BASIC_ERR_OK;
    }
    {
        double number = strtod(value->u.number.raw, NULL);
        int64_t narrowed = 0;
        if ((number < -9223372036854775808.0) || (number >= 9223372036854775808.0)) {
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
        narrowed = (int64_t)number;
        if ((double)narrowed != number) {
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
        *out = narrowed;
        return SHREWD_BASIC_ERR_OK;
    }
}

int shrewd_basic_json_to_u64(const shrewd_basic_json_value_t *value, uint64_t *out)
{
    if ((NULL == value) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_JSON_NUMBER != value->kind) {
        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
    }
    if ('-' == value->u.number.raw[0]) {
        int64_t signed_value = 0;
        int status = shrewd_basic_json_to_i64(value, &signed_value);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (0 > signed_value) {
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
        *out = (uint64_t)signed_value;
        return SHREWD_BASIC_ERR_OK;
    }
    if (0 == json_number_is_double(value->u.number.raw)) {
        char *end = NULL;
        unsigned long long parsed = strtoull(value->u.number.raw, &end, 10);
        if ((NULL == end) || ('\0' != *end)) {
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
        *out = (uint64_t)parsed;
        return SHREWD_BASIC_ERR_OK;
    }
    {
        double number = strtod(value->u.number.raw, NULL);
        uint64_t narrowed = 0;
        if ((number < 0.0) || (number >= 18446744073709551616.0)) {
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
        narrowed = (uint64_t)number;
        if ((double)narrowed != number) {
            return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
        }
        *out = narrowed;
        return SHREWD_BASIC_ERR_OK;
    }
}

int shrewd_basic_json_to_f64(const shrewd_basic_json_value_t *value, double *out)
{
    if ((NULL == value) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_JSON_NUMBER != value->kind) {
        return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
    }
    *out = strtod(value->u.number.raw, NULL);
    return SHREWD_BASIC_ERR_OK;
}

const char *shrewd_basic_json_to_string(const shrewd_basic_json_value_t *value)
{
    if ((NULL == value) || (SHREWD_BASIC_JSON_STRING != value->kind)) {
        return NULL;
    }
    return value->u.string;
}

const char *shrewd_basic_json_number_text(const shrewd_basic_json_value_t *value)
{
    if ((NULL == value) || (SHREWD_BASIC_JSON_NUMBER != value->kind)) {
        return NULL;
    }
    return value->u.number.raw;
}

/* --- mutation --------------------------------------------------------- */

static int json_object_grow(shrewd_basic_json_value_t *object)
{
    if (object->u.object.count < object->u.object.cap) {
        return SHREWD_BASIC_ERR_OK;
    }
    {
        size_t cap = (0 == object->u.object.cap) ? 4 : (object->u.object.cap * 2);
        size_t have = object->u.object.count;
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_alloc(&block, cap * sizeof(shrewd_basic_json_entry_t))) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        if (0 < have) {
            memcpy(block, object->u.object.entries, have * sizeof(shrewd_basic_json_entry_t));
        }
        if (0 != object->u.object.cap) {
            (void)shrewd_basic_platform_free((void **)&object->u.object.entries);
        }
        object->u.object.entries = block;
        object->u.object.cap = cap;
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_set(shrewd_basic_json_value_t *object,
                          const char *key,
                          shrewd_basic_json_value_t *item)
{
    if ((NULL == object) || (SHREWD_BASIC_JSON_OBJECT != object->kind) || (NULL == key)
        || (NULL == item)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < object->u.object.count; ++i) {
        if (0 == strcmp(object->u.object.entries[i].key, key)) {
            shrewd_basic_json_value_free(object->u.object.entries[i].value);
            object->u.object.entries[i].value = item;
            return SHREWD_BASIC_ERR_OK;
        }
    }
    return json_object_add(object, key, item);
}

int json_object_add_steal(shrewd_basic_json_value_t *object,
                          char *key,
                          shrewd_basic_json_value_t *item)
{
    int status = 0;
    if ((NULL == object) || (SHREWD_BASIC_JSON_OBJECT != object->kind) || (NULL == key)
        || (NULL == item)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == object->u.object.cap) {
        if (NULL == object->u.object.entries) {
            object->u.object.entries = (shrewd_basic_json_entry_t *)(void *)object->local.bytes;
        }
        if (object->u.object.count < 2) {
            object->u.object.entries[object->u.object.count].key = key;
            object->u.object.entries[object->u.object.count].value = item;
            object->u.object.count += 1;
            return SHREWD_BASIC_ERR_OK;
        }
    }
    if (object->u.object.count >= object->u.object.cap) {
        status = json_object_grow(object);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
    }
    object->u.object.entries[object->u.object.count].key = key;
    object->u.object.entries[object->u.object.count].value = item;
    object->u.object.count += 1;
    return SHREWD_BASIC_ERR_OK;
}

int json_object_add(shrewd_basic_json_value_t *object,
                    const char *key,
                    shrewd_basic_json_value_t *item)
{
    void *block = NULL;
    size_t length = 0;
    int status = 0;
    if ((NULL == object) || (SHREWD_BASIC_JSON_OBJECT != object->kind) || (NULL == key)
        || (NULL == item)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == object->u.object.cap) {
        if (NULL == object->u.object.entries) {
            object->u.object.entries = (shrewd_basic_json_entry_t *)(void *)object->local.bytes;
        }
        if (object->u.object.count < 2) {
            void *key_block = NULL;
            size_t key_len = strlen(key);
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&key_block, key_len + 1)) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            memcpy(key_block, key, key_len + 1);
            object->u.object.entries[object->u.object.count].key = key_block;
            object->u.object.entries[object->u.object.count].value = item;
            object->u.object.count += 1;
            return SHREWD_BASIC_ERR_OK;
        }
    }
    status = json_object_grow(object);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    length = strlen(key);
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, length + 1)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    memcpy(block, key, length + 1);
    object->u.object.entries[object->u.object.count].key = block;
    object->u.object.entries[object->u.object.count].value = item;
    object->u.object.count += 1;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_append(shrewd_basic_json_value_t *array, shrewd_basic_json_value_t *item)
{
    if ((NULL == array) || (SHREWD_BASIC_JSON_ARRAY != array->kind) || (NULL == item)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == array->u.array.cap) {
        /* Inline storage until the third element forces a spill. */
        if (NULL == array->u.array.items) {
            array->u.array.items = (shrewd_basic_json_value_t **)(void *)array->local.bytes;
        }
        if (array->u.array.count < 2) {
            array->u.array.items[array->u.array.count] = item;
            array->u.array.count += 1;
            return SHREWD_BASIC_ERR_OK;
        }
    }
    if (array->u.array.count >= array->u.array.cap) {
        size_t cap = (0 == array->u.array.cap) ? 4 : (array->u.array.cap * 2);
        void *block = NULL;
        size_t have = array->u.array.count;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_alloc(&block, cap * sizeof(shrewd_basic_json_value_t *))) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        if (0 < have) {
            memcpy(block, array->u.array.items, have * sizeof(shrewd_basic_json_value_t *));
        }
        if (0 != array->u.array.cap) {
            (void)shrewd_basic_platform_free((void **)&array->u.array.items);
        }
        array->u.array.items = block;
        array->u.array.cap = cap;
    }
    array->u.array.items[array->u.array.count] = item;
    array->u.array.count += 1;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_remove_at(shrewd_basic_json_value_t *value, size_t index)
{
    if (NULL == value) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_JSON_ARRAY == value->kind) {
        if (index >= value->u.array.count) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        shrewd_basic_json_value_free(value->u.array.items[index]);
        memmove(&value->u.array.items[index],
                &value->u.array.items[index + 1],
                (value->u.array.count - index - 1) * sizeof(shrewd_basic_json_value_t *));
        value->u.array.count -= 1;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_JSON_OBJECT == value->kind) {
        if (index >= value->u.object.count) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        (void)shrewd_basic_platform_free((void **)&value->u.object.entries[index].key);
        shrewd_basic_json_value_free(value->u.object.entries[index].value);
        memmove(&value->u.object.entries[index],
                &value->u.object.entries[index + 1],
                (value->u.object.count - index - 1) * sizeof(shrewd_basic_json_entry_t));
        value->u.object.count -= 1;
        return SHREWD_BASIC_ERR_OK;
    }
    return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
}

int shrewd_basic_json_replace_at(shrewd_basic_json_value_t *value,
                                 size_t index,
                                 shrewd_basic_json_value_t *item)
{
    if ((NULL == value) || (NULL == item)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_JSON_ARRAY == value->kind) {
        if (index >= value->u.array.count) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        shrewd_basic_json_value_free(value->u.array.items[index]);
        value->u.array.items[index] = item;
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_JSON_OBJECT == value->kind) {
        if (index >= value->u.object.count) {
            return SHREWD_BASIC_ERR_KEY_NOTFOUND;
        }
        shrewd_basic_json_value_free(value->u.object.entries[index].value);
        value->u.object.entries[index].value = item;
        return SHREWD_BASIC_ERR_OK;
    }
    return SHREWD_BASIC_ERR_JSON_TYPE_MISMATCH;
}

int shrewd_basic_json_remove(shrewd_basic_json_value_t *value, const char *key)
{
    if ((NULL == value) || (SHREWD_BASIC_JSON_OBJECT != value->kind) || (NULL == key)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < value->u.object.count; ++i) {
        if (0 == strcmp(value->u.object.entries[i].key, key)) {
            return shrewd_basic_json_remove_at(value, i);
        }
    }
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
}

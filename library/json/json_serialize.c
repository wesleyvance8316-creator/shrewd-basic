#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * Serializer: compact by default, pretty on request. Numbers go out as
 * their exact token (parse/serialize round-trips text-identical),
 * strings are re-escaped (\" \\ \b \f \n \r \t, other control bytes as
 * \u00XX, everything else byte-for-byte).
 */

static int json_write_string(json_buf_t *buf, const char *text)
{
    int status = json_buf_append(buf, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)text;
         (SHREWD_BASIC_ERR_OK == status) && ('\0' != *p); ++p) {
        switch (*p) {
        case '"':
            status = json_buf_append(buf, "\\\"", 2);
            break;
        case '\\':
            status = json_buf_append(buf, "\\\\", 2);
            break;
        case '\b':
            status = json_buf_append(buf, "\\b", 2);
            break;
        case '\f':
            status = json_buf_append(buf, "\\f", 2);
            break;
        case '\n':
            status = json_buf_append(buf, "\\n", 2);
            break;
        case '\r':
            status = json_buf_append(buf, "\\r", 2);
            break;
        case '\t':
            status = json_buf_append(buf, "\\t", 2);
            break;
        default:
            if (0x20 > *p) {
                char escape[8];
                (void)snprintf(escape, sizeof(escape), "\\u%04x",
                               (unsigned int)*p);
                status = json_buf_append(buf, escape, 6);
            } else {
                status = json_buf_append(buf, (const char *)p, 1);
            }
            break;
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = json_buf_append(buf, "\"", 1);
    }
    return status;
}

static int json_write_value(json_buf_t *buf, const shrewd_basic_json_value_t *value,
                            const shrewd_basic_json_options_t *options,
                            shrewd_basic_json_error_t *error, int depth)
{
    int status = SHREWD_BASIC_ERR_OK;
    if (depth > json_opt_depth(options)) {
        if (NULL != error) {
            error->offset = 0;
            (void)snprintf(error->message, sizeof(error->message),
                           "nesting too deep");
        }
        return SHREWD_BASIC_ERR_JSON_DEPTH;
    }
    switch (value->kind) {
    case SHREWD_BASIC_JSON_NULL:
        return json_buf_string(buf, "null");
    case SHREWD_BASIC_JSON_BOOL:
        return json_buf_string(buf, value->u.boolean ? "true" : "false");
    case SHREWD_BASIC_JSON_NUMBER:
        return json_buf_string(buf, value->u.number.raw);
    case SHREWD_BASIC_JSON_STRING:
        return json_write_string(buf, value->u.string);
    case SHREWD_BASIC_JSON_ARRAY:
        break;
    case SHREWD_BASIC_JSON_OBJECT:
        break;
    default:
        return SHREWD_BASIC_ERR_JSON_UNSUPPORTED;
    }
    {
        int pretty = (NULL != options) ? options->pretty : 0;
        int indent = json_opt_indent(options);
        size_t count = shrewd_basic_json_count(value);
        int is_object = (SHREWD_BASIC_JSON_OBJECT == value->kind);
        status = json_buf_append(buf, is_object ? "{" : "[", 1);
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < count); ++i) {
            if (0 != i) {
                status = json_buf_append(buf, ",", 1);
            }
            if ((SHREWD_BASIC_ERR_OK == status) && (0 != pretty)) {
                status = json_buf_append(buf, "\n", 1);
                for (int s = 0; (SHREWD_BASIC_ERR_OK == status)
                        && (s < (depth * indent)); ++s) {
                    status = json_buf_append(buf, " ", 1);
                }
            }
            if ((SHREWD_BASIC_ERR_OK == status) && (0 != is_object)) {
                status = json_write_string(buf, shrewd_basic_json_key_at(value, i));
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = json_buf_string(buf, pretty ? ": " : ":");
                }
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                status = json_write_value(buf, shrewd_basic_json_at(value, i),
                                          options, error, depth + 1);
            }
        }
        if ((SHREWD_BASIC_ERR_OK == status) && (0 != pretty) && (0 < count)) {
            status = json_buf_append(buf, "\n", 1);
            for (int s = 0; (SHREWD_BASIC_ERR_OK == status)
                    && (s < ((depth - 1) * indent)); ++s) {
                status = json_buf_append(buf, " ", 1);
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = json_buf_append(buf, is_object ? "}" : "]", 1);
        }
        return status;
    }
}

int shrewd_basic_json_serialize(const shrewd_basic_json_value_t *value,
                                char **out_text,
                                const shrewd_basic_json_options_t *options,
                                shrewd_basic_json_error_t *error)
{
    json_buf_t buf;
    int status = 0;
    if ((NULL == value) || (NULL == out_text)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_json_log,
                         "serialize", "serialize: value or out_text is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_text = NULL;
    if (NULL != error) {
        error->offset = 0;
        error->path[0] = '\0';
        error->message[0] = '\0';
    }
    status = json_buf_init(&buf);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = json_write_value(&buf, value, options, error, 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        json_buf_release(&buf);
        return status;
    }
    *out_text = buf.data; /* ownership moves to the caller */
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_json_log,
                     "serialize", "serialize ok (%zu bytes)\n", buf.len);
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_json_text_free(char *text)
{
    if (NULL != text) {
        (void)shrewd_basic_platform_free((void **)&text);
    }
}

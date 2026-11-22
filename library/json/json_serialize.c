#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * Serializer: compact by default, pretty on request. The writing core
 * is a sink callback (shrewd_basic_json_write_fn) so output streams to
 * files/sockets without materializing the text; numbers go out as their
 * exact token (parse/serialize round-trips text-identical), strings are
 * re-escaped (\" \\ \b \f \n \r \t, other control bytes as \u00XX, and -
 * with options.escape_unicode - all non-ASCII as \uXXXX).
 */

static int json_out(shrewd_basic_json_write_fn write, void *ctx, const char *text, size_t length) {
    return write(ctx, text, length);
}

static int json_out_str(shrewd_basic_json_write_fn write, void *ctx, const char *text) {
    return write(ctx, text, strlen(text));
}

/* Emit one code point as \uXXXX (or a surrogate pair past the BMP). */
static int json_out_u_escape(shrewd_basic_json_write_fn write, void *ctx, unsigned code) {
    char text[16] = {0};
    if (0x10000u > code) {
        (void)snprintf(text, sizeof(text), "\\u%04x", code);
        return json_out_str(write, ctx, text);
    }
    code -= 0x10000u;
    (void)snprintf(
        text, sizeof(text), "\\u%04x\\u%04x", 0xd800u + (code >> 10), 0xdc00u + (code & 0x3ffu));
    return json_out_str(write, ctx, text);
}

static int json_write_string(shrewd_basic_json_write_fn write,
                             void *ctx,
                             const char *text,
                             int escape_unicode) {
    int status = json_out_str(write, ctx, "\"");
    for (const unsigned char *p = (const unsigned char *)text;
         (SHREWD_BASIC_ERR_OK == status) && ('\0' != *p);
         ++p) {
        switch (*p) {
        case '"':
            status = json_out_str(write, ctx, "\\\"");
            break;
        case '\\':
            status = json_out_str(write, ctx, "\\\\");
            break;
        case '\b':
            status = json_out_str(write, ctx, "\\b");
            break;
        case '\f':
            status = json_out_str(write, ctx, "\\f");
            break;
        case '\n':
            status = json_out_str(write, ctx, "\\n");
            break;
        case '\r':
            status = json_out_str(write, ctx, "\\r");
            break;
        case '\t':
            status = json_out_str(write, ctx, "\\t");
            break;
        default:
            if (0x20 > *p) {
                char escape[8] = {0};
                (void)snprintf(escape, sizeof(escape), "\\u%04x", (unsigned int)*p);
                status = json_out_str(write, ctx, escape);
            } else if ((0 != escape_unicode) && (0x7f < *p)) {
                unsigned code = 0;
                size_t used = json_utf8_decode(p, json_strn_len((const char *)p, 4), &code);
                if (0 < used) {
                    status = json_out_u_escape(write, ctx, code);
                    p += used - 1;
                } else {
                    /* Invalid sequence: escape the raw byte. */
                    char escape[8] = {0};
                    (void)snprintf(escape, sizeof(escape), "\\u%04x", (unsigned int)*p);
                    status = json_out_str(write, ctx, escape);
                }
            } else {
                status = json_out(write, ctx, (const char *)p, 1);
            }
            break;
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = json_out_str(write, ctx, "\"");
    }
    return status;
}

static int json_write_value(shrewd_basic_json_write_fn write,
                            void *ctx,
                            const shrewd_basic_json_value_t *value,
                            const shrewd_basic_json_options_t *options,
                            shrewd_basic_json_error_t *error,
                            int depth) {
    int status = SHREWD_BASIC_ERR_OK;
    if (depth > json_opt_depth(options)) {
        if (NULL != error) {
            error->offset = 0;
            (void)snprintf(error->message, sizeof(error->message), "nesting too deep");
        }
        return SHREWD_BASIC_ERR_JSON_DEPTH;
    }
    switch (value->kind) {
    case SHREWD_BASIC_JSON_NULL:
        return json_out_str(write, ctx, "null");
    case SHREWD_BASIC_JSON_BOOL:
        return json_out_str(write, ctx, value->u.boolean ? "true" : "false");
    case SHREWD_BASIC_JSON_NUMBER:
        return json_out_str(write, ctx, value->u.number.raw);
    case SHREWD_BASIC_JSON_STRING:
        return json_write_string(
            write, ctx, value->u.string, (NULL != options) ? options->escape_unicode : 0);
    default:
        break;
    }
    {
        int pretty = (NULL != options) ? options->pretty : 0;
        int indent = json_opt_indent(options);
        size_t count = shrewd_basic_json_count(value);
        int is_object = (SHREWD_BASIC_JSON_OBJECT == value->kind);
        size_t *order = NULL;
        if ((0 != is_object) && (NULL != options) && (0 != options->sort_keys) && (1 < count)) {
            /* sort_keys: emit members in ascending key order (byte
               wise; stable for duplicate keys via the index tie-break).
               The DOM keeps its document order. */
            if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_alloc((void **)&order, count * sizeof(*order))) {
                return SHREWD_BASIC_ERR_MEM_NOMEM;
            }
            for (size_t i = 0; i < count; ++i) {
                order[i] = i;
            }
            for (size_t i = 1; i < count; ++i) {
                size_t moving = order[i];
                size_t j = i;
                while ((0 < j)
                       && (0 < strcmp(shrewd_basic_json_key_at(value, order[j - 1]),
                                      shrewd_basic_json_key_at(value, moving)))) {
                    order[j] = order[j - 1];
                    --j;
                }
                order[j] = moving;
            }
        }
        status = json_out_str(write, ctx, is_object ? "{" : "[");
        for (size_t i = 0; (SHREWD_BASIC_ERR_OK == status) && (i < count); ++i) {
            size_t at = (NULL != order) ? order[i] : i;
            if (0 != i) {
                status = json_out_str(write, ctx, ",");
            }
            if ((SHREWD_BASIC_ERR_OK == status) && (0 != pretty)) {
                status = json_out_str(write, ctx, "\n");
                for (int s = 0; (SHREWD_BASIC_ERR_OK == status) && (s < (depth * indent)); ++s) {
                    status = json_out_str(write, ctx, " ");
                }
            }
            if ((SHREWD_BASIC_ERR_OK == status) && (0 != is_object)) {
                status = json_write_string(write,
                                           ctx,
                                           shrewd_basic_json_key_at(value, at),
                                           (NULL != options) ? options->escape_unicode : 0);
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = json_out_str(write, ctx, pretty ? ": " : ":");
                }
            }
            if (SHREWD_BASIC_ERR_OK == status) {
                status = json_write_value(
                    write, ctx, shrewd_basic_json_at(value, at), options, error, depth + 1);
            }
        }
        if (NULL != order) {
            (void)shrewd_basic_platform_free((void **)&order);
        }
        if ((SHREWD_BASIC_ERR_OK == status) && (0 != pretty) && (0 < count)) {
            status = json_out_str(write, ctx, "\n");
            for (int s = 0; (SHREWD_BASIC_ERR_OK == status) && (s < ((depth - 1) * indent)); ++s) {
                status = json_out_str(write, ctx, " ");
            }
        }
        if (SHREWD_BASIC_ERR_OK == status) {
            status = json_out_str(write, ctx, is_object ? "}" : "]");
        }
        return status;
    }
}

int shrewd_basic_json_serialize_to(const shrewd_basic_json_value_t *value,
                                   shrewd_basic_json_write_fn write,
                                   void *ctx,
                                   const shrewd_basic_json_options_t *options,
                                   shrewd_basic_json_error_t *error) {
    if ((NULL == value) || (NULL == write)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_json_log,
                         "serialize",
                         "serialize_to: value or write is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL != error) {
        error->offset = 0;
        error->path[0] = '\0';
        error->message[0] = '\0';
    }
    return json_write_value(write, ctx, value, options, error, 1);
}

int shrewd_basic_json_write_file(void *ctx, const char *data, size_t length) {
    FILE *file = (FILE *)ctx;
    return (length == fwrite(data, 1, length, file)) ? SHREWD_BASIC_ERR_OK
                                                     : SHREWD_BASIC_ERR_JSON_SYNTAX;
}

/* Collecting sink for the text-returning wrappers. */
static int json_write_collect(void *ctx, const char *data, size_t length) {
    return json_buf_append((json_buf_t *)ctx, data, length);
}

int shrewd_basic_json_serialize(const shrewd_basic_json_value_t *value,
                                char **out_text,
                                const shrewd_basic_json_options_t *options,
                                shrewd_basic_json_error_t *error) {
    json_buf_t buf = {0};
    int status = 0;
    if ((NULL == value) || (NULL == out_text)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_json_log,
                         "serialize",
                         "serialize: value or out_text is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out_text = NULL;
    status = json_buf_init(&buf);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = json_write_value(json_write_collect, &buf, value, options, error, 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        json_buf_release(&buf);
        return status;
    }
    *out_text = buf.data; /* ownership moves to the caller */
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_json_log,
                     "serialize",
                     "serialize ok (%zu bytes)\n",
                     buf.len);
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_json_text_free(char *text) {
    if (NULL != text) {
        (void)shrewd_basic_platform_free((void **)&text);
    }
}

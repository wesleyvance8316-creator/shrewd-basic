#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * RFC 8259 parser: strict by default (no comments, no trailing commas,
 * no leading '+' or leading zeros in numbers, no unpaired surrogates),
 * with options relaxing comments and trailing commas. Text bytes inside
 * strings are passed through as-is (a lone invalid UTF-8 byte sequence
 * round-trips unchanged; only well-formed \u escapes are decoded).
 */
typedef struct json_parser {
    const char *text;
    size_t length;
    size_t pos;
    const shrewd_basic_json_options_t *options;
    shrewd_basic_json_error_t *error;
} json_parser_t;

static int json_fail(json_parser_t *p, int status, const char *message)
{
    if (NULL != p->error) {
        p->error->offset = p->pos;
        (void)snprintf(p->error->message, sizeof(p->error->message), "%s",
                       message);
    }
    return status;
}

static int json_at_end(const json_parser_t *p)
{
    return (p->pos >= p->length) ? 1 : 0;
}

static char json_peek(const json_parser_t *p)
{
    return (p->pos < p->length) ? p->text[p->pos] : '\0';
}

/* --- whitespace and comments ------------------------------------------ */

static void json_skip_ws(json_parser_t *p)
{
    for (;;) {
        while ((p->pos < p->length)
                && ((' ' == p->text[p->pos]) || ('\t' == p->text[p->pos])
                    || ('\n' == p->text[p->pos]) || ('\r' == p->text[p->pos]))) {
            ++p->pos;
        }
        if ((0 == p->options->allow_comments) || (p->pos + 1 >= p->length)
                || ('/' != p->text[p->pos])) {
            return;
        }
        if ('/' == p->text[p->pos + 1]) {
            p->pos += 2;
            while ((p->pos < p->length) && ('\n' != p->text[p->pos])) {
                ++p->pos;
            }
        } else if ('*' == p->text[p->pos + 1]) {
            p->pos += 2;
            while ((p->pos + 1 < p->length)
                    && !(('*' == p->text[p->pos]) && ('/' == p->text[p->pos + 1]))) {
                ++p->pos;
            }
            if (p->pos + 1 < p->length) {
                p->pos += 2;
            } else {
                p->pos = p->length; /* unterminated; the value parse fails later */
            }
        } else {
            return;
        }
    }
}

/* --- strings ---------------------------------------------------------- */

static int json_hex4(json_parser_t *p, unsigned *out)
{
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
        char c = 0;
        if (p->pos >= p->length) {
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "truncated \\u escape");
        }
        c = p->text[p->pos++];
        if (('0' <= c) && ('9' >= c)) {
            value = (value * 16) + (unsigned)(c - '0');
        } else if (('a' <= c) && ('f' >= c)) {
            value = (value * 16) + (unsigned)((c - 'a') + 10);
        } else if (('A' <= c) && ('F' >= c)) {
            value = (value * 16) + (unsigned)((c - 'A') + 10);
        } else {
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "bad hex digit in \\u escape");
        }
    }
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

/* Append one code point as UTF-8. */
static int json_utf8_append(json_buf_t *buf, unsigned code)
{
    char bytes[4];
    size_t n = 0;
    if (0x80u > code) {
        bytes[0] = (char)code;
        n = 1;
    } else if (0x800u > code) {
        bytes[0] = (char)(0xc0u | (code >> 6));
        bytes[1] = (char)(0x80u | (code & 0x3fu));
        n = 2;
    } else if (0x10000u > code) {
        bytes[0] = (char)(0xe0u | (code >> 12));
        bytes[1] = (char)(0x80u | ((code >> 6) & 0x3fu));
        bytes[2] = (char)(0x80u | (code & 0x3fu));
        n = 3;
    } else {
        bytes[0] = (char)(0xf0u | (code >> 18));
        bytes[1] = (char)(0x80u | ((code >> 12) & 0x3fu));
        bytes[2] = (char)(0x80u | ((code >> 6) & 0x3fu));
        bytes[3] = (char)(0x80u | (code & 0x3fu));
        n = 4;
    }
    return json_buf_append(buf, bytes, n);
}

/* Parse a string token (the opening quote already consumed) into a value. */
static int json_parse_string(json_parser_t *p, shrewd_basic_json_value_t **out)
{
    json_buf_t buf;
    int status = json_buf_init(&buf);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (;;) {
        char c = 0;
        if (json_at_end(p)) {
            json_buf_release(&buf);
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "unterminated string");
        }
        c = p->text[p->pos++];
        if ('"' == c) {
            break;
        }
        if ('\\' == c) {
            char escape = 0;
            if (json_at_end(p)) {
                json_buf_release(&buf);
                return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                                 "unterminated escape");
            }
            escape = p->text[p->pos++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                status = json_buf_append(&buf, &escape, 1);
                break;
            case 'b':
                status = json_buf_append(&buf, "\b", 1);
                break;
            case 'f':
                status = json_buf_append(&buf, "\f", 1);
                break;
            case 'n':
                status = json_buf_append(&buf, "\n", 1);
                break;
            case 'r':
                status = json_buf_append(&buf, "\r", 1);
                break;
            case 't':
                status = json_buf_append(&buf, "\t", 1);
                break;
            case 'u': {
                unsigned unit = 0;
                status = json_hex4(p, &unit);
                if (SHREWD_BASIC_ERR_OK != status) {
                    break;
                }
                if ((0xd800u <= unit) && (0xdbffu >= unit)) {
                    /* High surrogate: a low surrogate must follow. */
                    unsigned low = 0;
                    if ((p->pos + 1 >= p->length) || ('\\' != p->text[p->pos])
                            || ('u' != p->text[p->pos + 1])) {
                        status = json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                                           "unpaired surrogate");
                        break;
                    }
                    p->pos += 2;
                    status = json_hex4(p, &low);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        break;
                    }
                    if ((0xdc00u > low) || (0xdfffu < low)) {
                        status = json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                                           "unpaired surrogate");
                        break;
                    }
                    unit = 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u);
                } else if ((0xdc00u <= unit) && (0xdfffu >= unit)) {
                    status = json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                                       "unpaired surrogate");
                    break;
                }
                status = json_utf8_append(&buf, unit);
                break;
            }
            default:
                status = json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                                   "bad escape");
                break;
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                json_buf_release(&buf);
                return status;
            }
            continue;
        }
        if (0x20 > (unsigned char)c) {
            json_buf_release(&buf);
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "control character in string");
        }
        status = json_buf_append(&buf, &c, 1);
        if (SHREWD_BASIC_ERR_OK != status) {
            json_buf_release(&buf);
            return status;
        }
    }
    status = shrewd_basic_json_new_string_n(buf.data, buf.len, out);
    json_buf_release(&buf);
    return status;
}

/* --- numbers ---------------------------------------------------------- */

static int json_parse_number(json_parser_t *p, shrewd_basic_json_value_t **out)
{
    size_t start = p->pos;
    if ('-' == json_peek(p)) {
        ++p->pos;
    }
    if (json_at_end(p)) {
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad number");
    }
    if ('0' == json_peek(p)) {
        ++p->pos;
        if ((!json_at_end(p)) && ('0' <= json_peek(p)) && ('9' >= json_peek(p))) {
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "leading zero in number");
        }
    } else if (('1' <= json_peek(p)) && ('9' >= json_peek(p))) {
        while ((!json_at_end(p)) && ('0' <= json_peek(p)) && ('9' >= json_peek(p))) {
            ++p->pos;
        }
    } else {
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad number");
    }
    if ((!json_at_end(p)) && ('.' == json_peek(p))) {
        ++p->pos;
        if (json_at_end(p) || ('0' > json_peek(p)) || ('9' < json_peek(p))) {
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "digit expected after '.'");
        }
        while ((!json_at_end(p)) && ('0' <= json_peek(p)) && ('9' >= json_peek(p))) {
            ++p->pos;
        }
    }
    if ((!json_at_end(p))
            && (('e' == json_peek(p)) || ('E' == json_peek(p)))) {
        ++p->pos;
        if ((!json_at_end(p))
                && (('+' == json_peek(p)) || ('-' == json_peek(p)))) {
            ++p->pos;
        }
        if (json_at_end(p) || ('0' > json_peek(p)) || ('9' < json_peek(p))) {
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "digit expected in exponent");
        }
        while ((!json_at_end(p)) && ('0' <= json_peek(p)) && ('9' >= json_peek(p))) {
            ++p->pos;
        }
    }
    return json_value_new_number(p->text + start, p->pos - start, out);
}

/* --- containers and values -------------------------------------------- */

static int json_parse_value(json_parser_t *p, int depth,
                            shrewd_basic_json_value_t **out);

static int json_parse_array(json_parser_t *p, int depth,
                            shrewd_basic_json_value_t **out)
{
    int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, out);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    ++p->pos; /* '[' */
    json_skip_ws(p);
    if (']' == json_peek(p)) {
        ++p->pos;
        return SHREWD_BASIC_ERR_OK;
    }
    for (;;) {
        shrewd_basic_json_value_t *item = NULL;
        json_skip_ws(p);
        status = json_parse_value(p, depth + 1, &item);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return status;
        }
        status = shrewd_basic_json_append(*out, item);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(item);
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return status;
        }
        json_skip_ws(p);
        if (',' == json_peek(p)) {
            ++p->pos;
            json_skip_ws(p);
            if ((0 != p->options->allow_trailing_commas)
                    && (']' == json_peek(p))) {
                ++p->pos;
                return SHREWD_BASIC_ERR_OK;
            }
            continue;
        }
        if (']' == json_peek(p)) {
            ++p->pos;
            return SHREWD_BASIC_ERR_OK;
        }
        shrewd_basic_json_value_free(*out);
        *out = NULL;
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                         "',' or ']' expected");
    }
}

static int json_parse_object(json_parser_t *p, int depth,
                             shrewd_basic_json_value_t **out)
{
    int status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, out);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    ++p->pos; /* '{' */
    json_skip_ws(p);
    if ('}' == json_peek(p)) {
        ++p->pos;
        return SHREWD_BASIC_ERR_OK;
    }
    for (;;) {
        shrewd_basic_json_value_t *key_value = NULL;
        shrewd_basic_json_value_t *item = NULL;
        json_skip_ws(p);
        if ('"' != json_peek(p)) {
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "object key expected");
        }
        ++p->pos;
        status = json_parse_string(p, &key_value);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return status;
        }
        json_skip_ws(p);
        if (':' != json_peek(p)) {
            shrewd_basic_json_value_free(key_value);
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                             "':' expected");
        }
        ++p->pos;
        json_skip_ws(p);
        status = json_parse_value(p, depth + 1, &item);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(key_value);
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return status;
        }
        status = json_object_add(*out, shrewd_basic_json_to_string(key_value),
                                 item);
        shrewd_basic_json_value_free(key_value);
        if (SHREWD_BASIC_ERR_OK != status) {
            shrewd_basic_json_value_free(item);
            shrewd_basic_json_value_free(*out);
            *out = NULL;
            return status;
        }
        json_skip_ws(p);
        if (',' == json_peek(p)) {
            ++p->pos;
            json_skip_ws(p);
            if ((0 != p->options->allow_trailing_commas)
                    && ('}' == json_peek(p))) {
                ++p->pos;
                return SHREWD_BASIC_ERR_OK;
            }
            continue;
        }
        if ('}' == json_peek(p)) {
            ++p->pos;
            return SHREWD_BASIC_ERR_OK;
        }
        shrewd_basic_json_value_free(*out);
        *out = NULL;
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX,
                         "',' or '}' expected");
    }
}

static int json_parse_value(json_parser_t *p, int depth,
                            shrewd_basic_json_value_t **out)
{
    char c = 0;
    *out = NULL;
    if (depth > json_opt_depth(p->options)) {
        return json_fail(p, SHREWD_BASIC_ERR_JSON_DEPTH,
                         "nesting too deep");
    }
    if (json_at_end(p)) {
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "value expected");
    }
    c = json_peek(p);
    switch (c) {
    case '{':
        return json_parse_object(p, depth, out);
    case '[':
        return json_parse_array(p, depth, out);
    case '"':
        ++p->pos;
        return json_parse_string(p, out);
    case 't':
        if ((p->pos + 4 <= p->length)
                && (0 == memcmp(p->text + p->pos, "true", 4))) {
            p->pos += 4;
            return shrewd_basic_json_new_bool(true, out);
        }
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad literal");
    case 'f':
        if ((p->pos + 5 <= p->length)
                && (0 == memcmp(p->text + p->pos, "false", 5))) {
            p->pos += 5;
            return shrewd_basic_json_new_bool(false, out);
        }
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad literal");
    case 'n':
        if ((p->pos + 4 <= p->length)
                && (0 == memcmp(p->text + p->pos, "null", 4))) {
            p->pos += 4;
            return shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, out);
        }
        return json_fail(p, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad literal");
    default:
        return json_parse_number(p, out);
    }
}

int shrewd_basic_json_parse(const char *text, size_t length,
                            shrewd_basic_json_value_t **out,
                            const shrewd_basic_json_options_t *options,
                            shrewd_basic_json_error_t *error)
{
    static const shrewd_basic_json_options_t strict = {0, 0, 0, 0, 0, 0, 0};
    json_parser_t parser;
    int status = 0;
    if ((NULL == text) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, shrewd_basic_json_log,
                         "parse", "parse: text or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (NULL != error) {
        error->offset = 0;
        error->path[0] = '\0';
        error->message[0] = '\0';
    }
    parser.text = text;
    parser.length = length;
    parser.pos = 0;
    parser.options = (NULL != options) ? options : &strict;
    parser.error = error;
    json_skip_ws(&parser);
    status = json_parse_value(&parser, 1, out);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    json_skip_ws(&parser);
    if (parser.pos < length) {
        shrewd_basic_json_value_free(*out);
        *out = NULL;
        return json_fail(&parser, SHREWD_BASIC_ERR_JSON_SYNTAX,
                         "trailing characters");
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, shrewd_basic_json_log,
                     "parse", "parse ok (%zu bytes)\n", length);
    return SHREWD_BASIC_ERR_OK;
}

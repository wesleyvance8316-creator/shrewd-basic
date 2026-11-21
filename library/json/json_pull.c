#include <stdio.h>
#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * The pull tokenizer: the single home of the RFC 8259 grammar (strict
 * by default) over either a complete buffer or a streaming reader. It
 * yields structural tokens (container begin/end, keys) and scalar
 * values; shrewd_basic_json_pull_capture() builds DOM subtrees from the
 * same stream, and shrewd_basic_json_parse() is that builder over the
 * whole document. Token text lives in an internal scratch buffer valid
 * until the next next()/capture(); the most recent key survives nested
 * captures in pull->key (capture copies it before descending).
 */
#define PULL_FIRST 0 /* first item: value / key / end       */
#define PULL_COMMA 1 /* after ',': an item is required      */
#define PULL_VALUE 2 /* after ':': a value is required      */
#define PULL_SEP 3   /* after an item: ',' or the closer    */
#define PULL_WINDOW 4096

typedef struct pull_frame {
    unsigned char is_object;
    unsigned char phase;
} pull_frame_t;

struct shrewd_basic_json_pull {
    const char *data; /* buffer source */
    size_t length;
    size_t pos;
    shrewd_basic_json_read_fn read; /* reader source */
    void *read_ctx;
    char *window; /* reader slide buffer */
    size_t window_len;
    size_t window_pos;
    int eof;
    shrewd_basic_json_options_t options;
    shrewd_basic_json_error_t *error;
    pull_frame_t stack[64];
    size_t depth;
    int started;
    int root_done;
    int finished;
    json_buf_t tok; /* scratch text of the current token */
    char *tok_text; /* direct source pointer when no copy is needed */
    size_t tok_len;
    json_buf_t key;   /* most recent key */
    char *inline_tok; /* the in-block scratch (not freed) */
    char *inline_key;
};

static int pull_fail(shrewd_basic_json_pull_t *pull, int status, const char *message) {
    if (NULL != pull->error) {
        pull->error->offset = pull->pos + pull->window_pos;
        (void)snprintf(pull->error->message, sizeof(pull->error->message), "%s", message);
    }
    return status;
}

/*
 * Make @p want bytes readable at the cursor: 1 = ready, 0 = end of
 * input, -1 = source failure. End of input is not an error; callers
 * decide what it means for them.
 */
static int pull_ensure(shrewd_basic_json_pull_t *pull, size_t want) {
    if (NULL == pull->read) {
        return ((pull->pos + want) <= pull->length) ? 1 : 0;
    }
    while ((pull->window_len - pull->window_pos) < want) {
        size_t tail = pull->window_len - pull->window_pos;
        size_t got = 0;
        if (0 != pull->eof) {
            return (tail >= want) ? 1 : 0;
        }
        if (0 != tail) {
            memmove(pull->window, pull->window + pull->window_pos, tail);
        }
        pull->window_len = tail;
        pull->window_pos = 0;
        if (SHREWD_BASIC_ERR_OK
            != pull->read(pull->read_ctx, pull->window + tail, PULL_WINDOW - tail, &got)) {
            return -1;
        }
        if (0 == got) {
            pull->eof = 1;
        }
        pull->window_len += got;
    }
    return 1;
}

static int pull_at_end(shrewd_basic_json_pull_t *pull) {
    if (NULL == pull->read) {
        return (pull->pos >= pull->length) ? 1 : 0;
    }
    return ((pull->window_pos >= pull->window_len) && (0 != pull->eof)) ? 1 : 0;
}

static char pull_peek(shrewd_basic_json_pull_t *pull) {
    if (NULL == pull->read) {
        return (pull->pos < pull->length) ? pull->data[pull->pos] : '\0';
    }
    return (pull->window_pos < pull->window_len) ? pull->window[pull->window_pos] : '\0';
}

static char pull_take(shrewd_basic_json_pull_t *pull) {
    char c = pull_peek(pull);
    if (0 == pull_at_end(pull)) {
        if (NULL == pull->read) {
            ++pull->pos;
        } else {
            ++pull->window_pos;
        }
    }
    return c;
}

/* Pointer/length of the contiguous run available at the cursor. */
static const char *pull_run(shrewd_basic_json_pull_t *pull, size_t *out_avail) {
    if (NULL == pull->read) {
        *out_avail = pull->length - pull->pos;
        return pull->data + pull->pos;
    }
    *out_avail = pull->window_len - pull->window_pos;
    return pull->window + pull->window_pos;
}

/* Advance the cursor by @p n consumed bytes. */
static void pull_skip(shrewd_basic_json_pull_t *pull, size_t n) {
    if (NULL == pull->read) {
        pull->pos += n;
    } else {
        pull->window_pos += n;
    }
}

static int pull_skip_ws(shrewd_basic_json_pull_t *pull) {
    for (;;) {
        char c = '\0';
        int ready = pull_ensure(pull, 1);
        if (0 > ready) {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "read error");
        }
        if ((0 == ready) || (0 != pull_at_end(pull))) {
            return SHREWD_BASIC_ERR_OK;
        }
        c = pull_peek(pull);
        if ((' ' == c) || ('\t' == c) || ('\n' == c) || ('\r' == c)) {
            (void)pull_take(pull);
            continue;
        }
        if (('/' == c) && (0 != pull->options.allow_comments)) {
            char next_c = '\0';
            const char *probe = NULL;
            size_t avail = 0;
            if (0 > pull_ensure(pull, 2)) {
                return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "read error");
            }
            probe = pull_run(pull, &avail);
            next_c = (1 < avail) ? probe[1] : '\0';
            if ('/' == next_c) {
                (void)pull_take(pull);
                (void)pull_take(pull);
                while ((0 == pull_at_end(pull)) && ('\n' != pull_take(pull))) {
                    /* skip to end of line */
                }
                continue;
            }
            if ('*' == next_c) {
                (void)pull_take(pull);
                (void)pull_take(pull);
                for (;;) {
                    if (0 > pull_ensure(pull, 2)) {
                        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "read error");
                    }
                    if (0 != pull_at_end(pull)) {
                        return pull_fail(
                            pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated comment");
                    }
                    probe = pull_run(pull, &avail);
                    if ((1 < avail) && ('*' == probe[0]) && ('/' == probe[1])) {
                        (void)pull_take(pull);
                        (void)pull_take(pull);
                        break;
                    }
                    (void)pull_take(pull);
                }
                continue;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
}

static int pull_tok_push(shrewd_basic_json_pull_t *pull, char c) {
    return json_buf_append(&pull->tok, &c, 1);
}

static int pull_hex4(shrewd_basic_json_pull_t *pull, unsigned *out) {
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
        char c = pull_take(pull);
        if ('\0' == c) {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "truncated \\u escape");
        }
        if (('0' <= c) && ('9' >= c)) {
            value = (value * 16) + (unsigned)(c - '0');
        } else if (('a' <= c) && ('f' >= c)) {
            value = (value * 16) + (unsigned)((c - 'a') + 10);
        } else if (('A' <= c) && ('F' >= c)) {
            value = (value * 16) + (unsigned)((c - 'A') + 10);
        } else {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad hex digit in \\u escape");
        }
    }
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

/*
 * Parse a string (opening quote consumed). For the buffer source an
 * unescaped string is returned as a direct pointer into the text (no
 * copy at all); escapes or the streaming reader fall back to the
 * scratch buffer.
 */
static int pull_parse_string(shrewd_basic_json_pull_t *pull) {
    if (NULL == pull->read) {
        const char *start = pull->data + pull->pos;
        size_t scan = 0;
        while ((pull->pos + scan) < pull->length) {
            unsigned char raw = (unsigned char)start[scan];
            if ('"' == raw) {
                if ((0 != pull->options.validate_utf8) && (0 == json_utf8_valid(start, scan))) {
                    pull->pos += scan;
                    return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "invalid UTF-8 in string");
                }
                pull->pos += scan + 1;
                pull->tok_len = scan;
                pull->tok.data[0] = '\0';
                pull->tok_text = (char *)start;
                return SHREWD_BASIC_ERR_OK;
            }
            if (('\\' == raw) || (0x20 > raw)) {
                break;
            }
            ++scan;
        }
    }
    pull->tok_text = NULL;
    pull->tok.len = 0;
    pull->tok.data[0] = '\0';
    for (;;) {
        char c = '\0';
        const char *run = NULL;
        size_t avail = 0;
        size_t used = 0;
        if (0 > pull_ensure(pull, 1)) {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "read error");
        }
        if (0 != pull_at_end(pull)) {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated string");
        }
        /* Bulk-copy the raw span before the next escape or quote. */
        run = pull_run(pull, &avail);
        while (used < avail) {
            unsigned char raw = (unsigned char)run[used];
            if (('"' == raw) || ('\\' == raw) || (0x20 > raw)) {
                break;
            }
            ++used;
        }
        if (0 < used) {
            (void)json_buf_append(&pull->tok, run, used);
            pull_skip(pull, used);
            continue;
        }
        c = pull_take(pull);
        if ('"' == c) {
            break;
        }
        if ('\\' == c) {
            char escape = '\0';
            if (0 != pull_at_end(pull)) {
                return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated escape");
            }
            escape = pull_take(pull);
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                (void)pull_tok_push(pull, escape);
                break;
            case 'b':
                (void)pull_tok_push(pull, '\b');
                break;
            case 'f':
                (void)pull_tok_push(pull, '\f');
                break;
            case 'n':
                (void)pull_tok_push(pull, '\n');
                break;
            case 'r':
                (void)pull_tok_push(pull, '\r');
                break;
            case 't':
                (void)pull_tok_push(pull, '\t');
                break;
            case 'u': {
                unsigned unit = 0;
                int status = pull_hex4(pull, &unit);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                if ((0xd800u <= unit) && (0xdbffu >= unit)) {
                    unsigned low = 0;
                    if ('\\' != pull_peek(pull)) {
                        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                    }
                    (void)pull_take(pull);
                    if ('u' != pull_take(pull)) {
                        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                    }
                    status = pull_hex4(pull, &low);
                    if (SHREWD_BASIC_ERR_OK != status) {
                        return status;
                    }
                    if ((0xdc00u > low) || (0xdfffu < low)) {
                        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                    }
                    unit = 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u);
                } else if ((0xdc00u <= unit) && (0xdfffu >= unit)) {
                    return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unpaired surrogate");
                }
                (void)json_utf8_append(&pull->tok, unit);
                break;
            }
            default:
                return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad escape");
            }
            continue;
        }
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "control character in string");
    }
    if ((0 != pull->options.validate_utf8)
        && (0 == json_utf8_valid(pull->tok.data, pull->tok.len))) {
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "invalid UTF-8 in string");
    }
    pull->tok_text = pull->tok.data;
    pull->tok_len = pull->tok.len;
    return SHREWD_BASIC_ERR_OK;
}

/* Append the run of digits at the cursor to the token scratch. */
static void pull_take_digits(shrewd_basic_json_pull_t *pull) {
    for (;;) {
        const char *run = NULL;
        size_t avail = 0;
        size_t used = 0;
        run = pull_run(pull, &avail);
        while ((used < avail) && ('0' <= run[used]) && ('9' >= run[used])) {
            ++used;
        }
        if (0 == used) {
            return;
        }
        (void)json_buf_append(&pull->tok, run, used);
        pull_skip(pull, used);
    }
}

/* Parse a number token (first char peeked) into the scratch. */
static int pull_parse_number(shrewd_basic_json_pull_t *pull) {
    const char *direct = (NULL == pull->read) ? (pull->data + pull->pos) : NULL;
    pull->tok_text = NULL;
    pull->tok.len = 0;
    pull->tok.data[0] = '\0';
    if ('-' == pull_peek(pull)) {
        (void)pull_tok_push(pull, pull_take(pull));
    }
    if (0 != pull_at_end(pull)) {
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad number");
    }
    if ('0' == pull_peek(pull)) {
        (void)pull_tok_push(pull, pull_take(pull));
        if ((0 == pull_at_end(pull)) && ('0' <= pull_peek(pull)) && ('9' >= pull_peek(pull))) {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "leading zero in number");
        }
    } else if (('1' <= pull_peek(pull)) && ('9' >= pull_peek(pull))) {
        pull_take_digits(pull);
    } else {
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad number");
    }
    if ((0 == pull_at_end(pull)) && ('.' == pull_peek(pull))) {
        (void)pull_tok_push(pull, pull_take(pull));
        if ((0 == pull_at_end(pull)) && ('0' <= pull_peek(pull)) && ('9' >= pull_peek(pull))) {
            pull_take_digits(pull);
        } else {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "digit expected after '.'");
        }
    }
    if ((0 == pull_at_end(pull)) && (('e' == pull_peek(pull)) || ('E' == pull_peek(pull)))) {
        (void)pull_tok_push(pull, pull_take(pull));
        if ((0 == pull_at_end(pull)) && (('+' == pull_peek(pull)) || ('-' == pull_peek(pull)))) {
            (void)pull_tok_push(pull, pull_take(pull));
        }
        if ((0 == pull_at_end(pull)) && ('0' <= pull_peek(pull)) && ('9' >= pull_peek(pull))) {
            pull_take_digits(pull);
        } else {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "digit expected in exponent");
        }
    }
    if (NULL != direct) {
        /* Buffer source: the token lives in the text itself. */
        pull->tok_text = (char *)direct;
        pull->tok_len = (size_t)((pull->data + pull->pos) - direct);
        pull->tok.data[0] = '\0';
    } else {
        pull->tok_text = pull->tok.data;
        pull->tok_len = pull->tok.len;
    }
    return SHREWD_BASIC_ERR_OK;
}

static int pull_parse_value_token(shrewd_basic_json_pull_t *pull,
                                  shrewd_basic_json_token_t *token) {
    char c = pull_peek(pull);
    token->length = 0;
    token->text = NULL;
    token->value_kind = SHREWD_BASIC_JSON_NULL;
    if ((pull->depth + 1) > (size_t)json_opt_depth(&pull->options)) {
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_DEPTH, "nesting too deep");
    }
    switch (c) {
    case '{':
        (void)pull_take(pull);
        pull->stack[pull->depth].is_object = 1;
        pull->stack[pull->depth].phase = PULL_FIRST;
        ++pull->depth;
        token->kind = SHREWD_BASIC_JSON_TOKEN_OBJECT_BEGIN;
        return SHREWD_BASIC_ERR_OK;
    case '[':
        (void)pull_take(pull);
        pull->stack[pull->depth].is_object = 0;
        pull->stack[pull->depth].phase = PULL_FIRST;
        ++pull->depth;
        token->kind = SHREWD_BASIC_JSON_TOKEN_ARRAY_BEGIN;
        return SHREWD_BASIC_ERR_OK;
    case '"': {
        int status = 0;
        (void)pull_take(pull);
        status = pull_parse_string(pull);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        token->kind = SHREWD_BASIC_JSON_TOKEN_VALUE;
        token->value_kind = SHREWD_BASIC_JSON_STRING;
        token->text = pull->tok_text;
        token->length = pull->tok_len;
        return SHREWD_BASIC_ERR_OK;
    }
    case 't':
    case 'f':
    case 'n': {
        const char *word = ('t' == c) ? "true" : (('f' == c) ? "false" : "null");
        size_t n = strlen(word);
        if (0 > pull_ensure(pull, n)) {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "read error");
        }
        for (size_t i = 0; i < n; ++i) {
            if (word[i] != pull_take(pull)) {
                return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "bad literal");
            }
        }
        token->kind = SHREWD_BASIC_JSON_TOKEN_VALUE;
        token->value_kind = ('n' == c) ? SHREWD_BASIC_JSON_NULL : SHREWD_BASIC_JSON_BOOL;
        token->text = word;
        token->length = n;
        return SHREWD_BASIC_ERR_OK;
    }
    default:
        if (('-' == c) || (('0' <= c) && ('9' >= c))) {
            int status = pull_parse_number(pull);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
            token->kind = SHREWD_BASIC_JSON_TOKEN_VALUE;
            token->value_kind = SHREWD_BASIC_JSON_NUMBER;
            token->text = pull->tok_text;
            token->length = pull->tok_len;
            return SHREWD_BASIC_ERR_OK;
        }
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "value expected");
    }
}

static void pull_root_check(shrewd_basic_json_pull_t *pull) {
    if ((0 == pull->depth) && (0 != pull->started)) {
        pull->root_done = 1;
    }
}

int shrewd_basic_json_pull_next(shrewd_basic_json_pull_t *pull,
                                shrewd_basic_json_token_t *out_token) {
    int status = 0;
    if ((NULL == pull) || (NULL == out_token)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    out_token->kind = SHREWD_BASIC_JSON_TOKEN_END;
    out_token->value_kind = SHREWD_BASIC_JSON_NULL;
    out_token->text = NULL;
    out_token->length = 0;
    if (0 != pull->finished) {
        return SHREWD_BASIC_ERR_OK;
    }
    status = pull_skip_ws(pull);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 != pull->root_done) {
        if (0 != pull_at_end(pull)) {
            pull->finished = 1;
            return SHREWD_BASIC_ERR_OK;
        }
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "trailing characters");
    }
    if (0 != pull_at_end(pull)) {
        if (0 < pull->depth) {
            return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "unterminated container");
        }
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "value expected");
    }
    if (0 == pull->depth) {
        pull->started = 1;
        status = pull_parse_value_token(pull, out_token);
        if ((SHREWD_BASIC_ERR_OK == status) && (SHREWD_BASIC_JSON_TOKEN_VALUE == out_token->kind)) {
            pull->root_done = 1;
        }
        return status;
    }
    {
        pull_frame_t *top = &pull->stack[pull->depth - 1];
        char c = pull_peek(pull);
        if (PULL_SEP == top->phase) {
            if (',' == c) {
                (void)pull_take(pull);
                top->phase = PULL_COMMA;
                return shrewd_basic_json_pull_next(pull, out_token);
            }
            if ((0 != top->is_object) && ('}' == c)) {
                (void)pull_take(pull);
                --pull->depth;
                pull_root_check(pull);
                out_token->kind = SHREWD_BASIC_JSON_TOKEN_OBJECT_END;
                return SHREWD_BASIC_ERR_OK;
            }
            if ((0 == top->is_object) && (']' == c)) {
                (void)pull_take(pull);
                --pull->depth;
                pull_root_check(pull);
                out_token->kind = SHREWD_BASIC_JSON_TOKEN_ARRAY_END;
                return SHREWD_BASIC_ERR_OK;
            }
            return pull_fail(pull,
                             SHREWD_BASIC_ERR_JSON_SYNTAX,
                             top->is_object ? "',' or '}' expected" : "',' or ']' expected");
        }
        if ((PULL_FIRST == top->phase) || (PULL_COMMA == top->phase)) {
            int after_comma = (PULL_COMMA == top->phase) ? 1 : 0;
            if ((0 != top->is_object) && ('}' == c)) {
                if ((0 != after_comma) && (0 == pull->options.allow_trailing_commas)) {
                    return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "trailing comma");
                }
                (void)pull_take(pull);
                --pull->depth;
                pull_root_check(pull);
                out_token->kind = SHREWD_BASIC_JSON_TOKEN_OBJECT_END;
                return SHREWD_BASIC_ERR_OK;
            }
            if ((0 == top->is_object) && (']' == c)) {
                if ((0 != after_comma) && (0 == pull->options.allow_trailing_commas)) {
                    return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "trailing comma");
                }
                (void)pull_take(pull);
                --pull->depth;
                pull_root_check(pull);
                out_token->kind = SHREWD_BASIC_JSON_TOKEN_ARRAY_END;
                return SHREWD_BASIC_ERR_OK;
            }
            if (0 != top->is_object) {
                if ('"' != c) {
                    return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "object key expected");
                }
                (void)pull_take(pull);
                status = pull_parse_string(pull);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                status = pull_skip_ws(pull);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
                if (':' != pull_peek(pull)) {
                    return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "':' expected");
                }
                (void)pull_take(pull);
                top->phase = PULL_VALUE;
                pull->key.len = 0;
                (void)json_buf_append(&pull->key, pull->tok_text, pull->tok_len);
                out_token->kind = SHREWD_BASIC_JSON_TOKEN_KEY;
                out_token->text = pull->key.data;
                out_token->length = pull->key.len;
                return SHREWD_BASIC_ERR_OK;
            }
            top->phase = PULL_SEP;
            return pull_parse_value_token(pull, out_token);
        }
        /* PULL_VALUE (after a key's ':'): a value is required. */
        top->phase = PULL_SEP;
        return pull_parse_value_token(pull, out_token);
    }
}

/* Build a DOM value from an already-fetched token. */
static int json_capture_token(shrewd_basic_json_pull_t *pull,
                              const shrewd_basic_json_token_t *token,
                              shrewd_basic_json_value_t **out) {
    int status = SHREWD_BASIC_ERR_OK;
    *out = NULL;
    switch (token->kind) {
    case SHREWD_BASIC_JSON_TOKEN_VALUE:
        switch (token->value_kind) {
        case SHREWD_BASIC_JSON_NULL:
            return shrewd_basic_json_new(SHREWD_BASIC_JSON_NULL, out);
        case SHREWD_BASIC_JSON_BOOL:
            return shrewd_basic_json_new_bool(0 == memcmp(token->text, "true", 4), out);
        case SHREWD_BASIC_JSON_NUMBER:
            return json_value_new_number(token->text, token->length, out);
        default:
            return shrewd_basic_json_new_string_n(token->text, token->length, out);
        }
    case SHREWD_BASIC_JSON_TOKEN_OBJECT_BEGIN: {
        shrewd_basic_json_value_t *object = NULL;
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_OBJECT, &object);
        while (SHREWD_BASIC_ERR_OK == status) {
            shrewd_basic_json_token_t step = {0};
            shrewd_basic_json_value_t *item = NULL;
            status = shrewd_basic_json_pull_next(pull, &step);
            if (SHREWD_BASIC_ERR_OK != status) {
                break;
            }
            if (SHREWD_BASIC_JSON_TOKEN_OBJECT_END == step.kind) {
                *out = object;
                return SHREWD_BASIC_ERR_OK;
            }
            if (SHREWD_BASIC_JSON_TOKEN_KEY != step.kind) {
                status = pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "object key expected");
                break;
            }
            {
                /* Nested captures overwrite pull->key: copy it, then hand
                   ownership to the object. */
                char *key = NULL;
                size_t key_len = pull->key.len;
                if (SHREWD_BASIC_ERR_OK
                    != shrewd_basic_platform_alloc((void **)&key, key_len + 1)) {
                    shrewd_basic_json_value_free(object);
                    return SHREWD_BASIC_ERR_MEM_NOMEM;
                }
                memcpy(key, pull->key.data, key_len + 1);
                status = shrewd_basic_json_pull_capture(pull, &item);
                if (SHREWD_BASIC_ERR_OK == status) {
                    status = json_object_add_steal(object, key, item);
                    key = NULL; /* ownership moved */
                }
                if (NULL != key) {
                    (void)shrewd_basic_platform_free((void **)&key);
                }
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(item);
                break;
            }
        }
        shrewd_basic_json_value_free(object);
        return status;
    }
    case SHREWD_BASIC_JSON_TOKEN_ARRAY_BEGIN: {
        shrewd_basic_json_value_t *array = NULL;
        status = shrewd_basic_json_new(SHREWD_BASIC_JSON_ARRAY, &array);
        while (SHREWD_BASIC_ERR_OK == status) {
            shrewd_basic_json_token_t step = {0};
            shrewd_basic_json_value_t *item = NULL;
            status = shrewd_basic_json_pull_next(pull, &step);
            if (SHREWD_BASIC_ERR_OK != status) {
                break;
            }
            if (SHREWD_BASIC_JSON_TOKEN_ARRAY_END == step.kind) {
                *out = array;
                return SHREWD_BASIC_ERR_OK;
            }
            status = json_capture_token(pull, &step, &item);
            if (SHREWD_BASIC_ERR_OK == status) {
                status = shrewd_basic_json_append(array, item);
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                shrewd_basic_json_value_free(item);
                break;
            }
        }
        shrewd_basic_json_value_free(array);
        return status;
    }
    default:
        return pull_fail(pull, SHREWD_BASIC_ERR_JSON_SYNTAX, "value expected");
    }
}

int shrewd_basic_json_pull_capture(shrewd_basic_json_pull_t *pull,
                                   shrewd_basic_json_value_t **out) {
    shrewd_basic_json_token_t token = {0};
    int status = 0;
    if ((NULL == pull) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    status = shrewd_basic_json_pull_next(pull, &token);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    return json_capture_token(pull, &token, out);
}

int shrewd_basic_json_pull_open(shrewd_basic_json_pull_t **out,
                                const char *text,
                                size_t length,
                                const shrewd_basic_json_options_t *options,
                                shrewd_basic_json_error_t *error) {
    shrewd_basic_json_pull_t *pull = NULL;
    void *block = NULL;
    if ((NULL == out) || (NULL == text)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    /*
     * One allocation carries the parser and both token buffers - the
     * per-parse scaffolding is a single free-list entry instead of
     * three (allocator-friendly for parse loops).
     */
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, sizeof(*pull) + 256 + 256)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    pull = block;
    pull->data = text;
    pull->length = length;
    if (NULL != options) {
        pull->options = *options;
    }
    pull->error = error;
    pull->inline_tok = (char *)block + sizeof(*pull);
    pull->inline_key = (char *)block + sizeof(*pull) + 256;
    pull->tok.data = pull->inline_tok;
    pull->tok.cap = 256;
    pull->tok.data[0] = '\0';
    pull->key.data = pull->inline_key;
    pull->key.cap = 256;
    pull->key.data[0] = '\0';
    *out = pull;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_json_pull_open_reader(shrewd_basic_json_pull_t **out,
                                       shrewd_basic_json_read_fn read,
                                       void *ctx,
                                       const shrewd_basic_json_options_t *options,
                                       shrewd_basic_json_error_t *error) {
    shrewd_basic_json_pull_t *pull = NULL;
    int status = 0;
    void *block = NULL;
    if ((NULL == out) || (NULL == read)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    status = shrewd_basic_json_pull_open(out, "", 0, options, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    pull = *out;
    pull->data = NULL;
    pull->length = 0;
    pull->read = read;
    pull->read_ctx = ctx;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&block, PULL_WINDOW)) {
        shrewd_basic_json_pull_close(pull);
        *out = NULL;
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    pull->window = block;
    return SHREWD_BASIC_ERR_OK;
}

void shrewd_basic_json_pull_close(shrewd_basic_json_pull_t *pull) {
    if (NULL == pull) {
        return;
    }
    {
        if ((NULL != pull->tok.data) && (pull->tok.data != pull->inline_tok)) {
            (void)shrewd_basic_platform_free((void **)&pull->tok.data);
        }
        if ((NULL != pull->key.data) && (pull->key.data != pull->inline_key)) {
            (void)shrewd_basic_platform_free((void **)&pull->key.data);
        }
    }
    if (NULL != pull->window) {
        (void)shrewd_basic_platform_free((void **)&pull->window);
    }
    (void)shrewd_basic_platform_free((void **)&pull);
}

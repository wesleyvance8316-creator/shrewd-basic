#include <string.h>

#include "json-internal.h"
#include "shrewd-basic/log/log.h"

/* Shared json log module, defined in library/json/json_value.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_json_log);

/*
 * shrewd_basic_json_parse() = the pull tokenizer + the DOM builder: the
 * grammar lives in json_pull.c and this wrapper only sequences the
 * capture of the single root value and the end-of-input check.
 */
int shrewd_basic_json_parse(const char *text,
                            size_t length,
                            shrewd_basic_json_value_t **out,
                            const shrewd_basic_json_options_t *options,
                            shrewd_basic_json_error_t *error) {
    shrewd_basic_json_pull_t *pull = NULL;
    shrewd_basic_json_token_t token = {0};
    int status = 0;
    if ((NULL == text) || (NULL == out)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_json_log,
                         "parse",
                         "parse: text or out is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = NULL;
    status = shrewd_basic_json_pull_open(&pull, text, length, options, error);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    status = shrewd_basic_json_pull_capture(pull, out);
    if ((SHREWD_BASIC_ERR_OK == status) && (NULL == *out)) {
        /* jsonl stream end without a document: parse is single-doc. */
        status = SHREWD_BASIC_ERR_JSON_SYNTAX;
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        status = shrewd_basic_json_pull_next(pull, &token);
    }
    if ((SHREWD_BASIC_ERR_OK == status) && (SHREWD_BASIC_JSON_TOKEN_END != token.kind)) {
        /* parse stays single-document even with the jsonl option. */
        status = SHREWD_BASIC_ERR_JSON_SYNTAX;
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        shrewd_basic_json_value_free(*out);
        *out = NULL;
    }
    shrewd_basic_json_pull_close(pull);
    if (SHREWD_BASIC_ERR_OK == status) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                         shrewd_basic_json_log,
                         "parse",
                         "parse ok (%zu bytes)\n",
                         length);
    }
    return status;
}

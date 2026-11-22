/**
 * @file reflect_gen_json_smoke.c
 * @brief Smoke program for json policy generation (reflect_gen_check.sh).
 *
 * Drives the generated policy table through decode/encode: alias keys
 * and the canonical rename, defaults materialization (use_defaults),
 * REQUIRED enforcement, range validation, hidden/omit_empty shaping
 * and the member-name-always-accepted invariant. Prints one summary
 * line and exits non-zero on failure.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/formats/json.h"
#include "shrewd-basic/platform/memory.h"

#include "generated.h"
#include "generated_json.h"
#include "reflect_gen_sample_json.h"

static int failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("reflect_gen_json_smoke: FAILED %s\n", name);
        ++failures;
    }
}

const struct shrewd_basic_json_ops g_sample_ops = {0};

static int decode_cfg(const char *wire, cfg_t *out, int use_defaults) {
    shrewd_basic_json_options_t options;
    shrewd_basic_json_error_t error;
    int status = 0;
    memset(&options, 0, sizeof(options));
    memset(&error, 0, sizeof(error));
    memset(out, 0, sizeof(*out));
    options.use_defaults = use_defaults;
    status = shrewd_basic_json_decode(
        wire, strlen(wire), out, &cfg_t_reflect, &cfg_t_json, &options, &error);
    return status;
}

int main(void) {
    cfg_t cfg;

    /* Alias key + renamed key decode; defaults fill the rest. */
    expect("alias decode",
           SHREWD_BASIC_ERR_OK == decode_cfg("{\"full_name\":\"bob\",\"lv\":4}", &cfg, 1)
               && (NULL != cfg.name) && (0 == strcmp(cfg.name, "bob")) && (4 == cfg.level));
    expect("default json materialized", (3 == cfg.box.w) && (4 == cfg.box.h));
    expect("alias free",
           SHREWD_BASIC_ERR_OK == shrewd_basic_json_free(&cfg, &cfg_t_reflect, &cfg_t_json));

    /* The member name itself stays acceptable (library invariant). */
    expect("member-name decode",
           SHREWD_BASIC_ERR_OK == decode_cfg("{\"name\":\"zed\",\"level\":1}", &cfg, 0)
               && (NULL != cfg.name) && (0 == strcmp(cfg.name, "zed")) && (1 == cfg.level));
    (void)shrewd_basic_json_free(&cfg, &cfg_t_reflect, &cfg_t_json);

    /* Empty object under use_defaults: default_str + default= +
       default_json= all materialize. */
    expect("empty decode",
           SHREWD_BASIC_ERR_OK == decode_cfg("{}", &cfg, 1) && (NULL != cfg.name)
               && (0 == strcmp(cfg.name, "anonymous")) && (2 == cfg.level) && (3 == cfg.box.w));
    (void)shrewd_basic_json_free(&cfg, &cfg_t_reflect, &cfg_t_json);

    /* REQUIRED: missing key without defaults is KEY_NOTFOUND. */
    expect("required enforced", SHREWD_BASIC_ERR_KEY_NOTFOUND == decode_cfg("{\"lv\":4}", &cfg, 0));
    (void)shrewd_basic_json_free(&cfg, &cfg_t_reflect, &cfg_t_json);

    /* RANGE: out-of-bounds value refuses cleanly. */
    expect("range enforced",
           SHREWD_BASIC_ERR_OK != decode_cfg("{\"full_name\":\"a\",\"lv\":9}", &cfg, 0));
    (void)shrewd_basic_json_free(&cfg, &cfg_t_reflect, &cfg_t_json);

    /* HIDDEN: the key is invisible on decode. */
    expect("hidden decode",
           SHREWD_BASIC_ERR_OK == decode_cfg("{\"full_name\":\"a\",\"secret\":9}", &cfg, 0)
               && (0 == cfg.secret));
    (void)shrewd_basic_json_free(&cfg, &cfg_t_reflect, &cfg_t_json);

    /* Encode: canonical keys, omit_empty and hidden shape the text. */
    {
        char *text = NULL;
        memset(&cfg, 0, sizeof(cfg));
        cfg.name = "bob";
        cfg.level = 4;
        cfg.secret = 7;
        expect(
            "encode",
            SHREWD_BASIC_ERR_OK
                == shrewd_basic_json_encode(&cfg, &cfg_t_reflect, &cfg_t_json, &text, NULL, NULL));
        expect("canonical keys",
               (NULL != text) && (NULL != strstr(text, "\"full_name\":\"bob\""))
                   && (NULL != strstr(text, "\"lv\":4")));
        expect("omit empty and hidden",
               (NULL != text) && (NULL == strstr(text, "\"score\""))
                   && (NULL == strstr(text, "\"secret\"")));
        shrewd_basic_json_text_free(text);
        cfg.score = 1.5;
        text = NULL;
        expect(
            "encode score",
            SHREWD_BASIC_ERR_OK
                == shrewd_basic_json_encode(&cfg, &cfg_t_reflect, &cfg_t_json, &text, NULL, NULL));
        expect("score present", (NULL != text) && (NULL != strstr(text, "\"score\":1.5")));
        shrewd_basic_json_text_free(text);
    }

    if (0 != failures) {
        printf("reflect_gen_json_smoke: %d failure(s)\n", failures);
        return 1;
    }
    printf("reflect_gen_json_smoke: ok\n");
    return 0;
}

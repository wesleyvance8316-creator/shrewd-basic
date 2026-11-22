#define _POSIX_C_SOURCE 200809L

/**
 * @file test_env.c
 * @brief Tests for env/args scanning (twelve-factor configuration
 *        through the shared text layer). One summary line, non-zero
 *        exit on failure.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/formats/env.h"
#include "shrewd-basic/platform/memory.h"

static int g_failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("test_env: FAILED %s\n", name);
        ++g_failures;
    }
}

typedef struct {
    int32_t log_level;
    bool debug;
    const char *host; /* owned */
    char tag[8];
} env_cfg_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(g_env_cfg_reflect,
                                 env_cfg_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(env_cfg_t, log_level),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(env_cfg_t, debug),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(env_cfg_t, host),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_BUFFER(env_cfg_t, tag));

int main(void) {
    env_cfg_t cfg;

    /* Prefix + UPPER(member): APP_ + log_level -> APP_LOG_LEVEL. */
    memset(&cfg, 0, sizeof(cfg));
    expect("setenv level", 0 == setenv("APP_LOG_LEVEL", "42", 1));
    expect("setenv host", 0 == setenv("APP_HOST", "example.test", 1));
    expect("setenv debug", 0 == setenv("APP_DEBUG", "true", 1));
    expect("setenv tag", 0 == setenv("APP_TAG", "v1", 1));
    expect("setenv junk", 0 == setenv("APP_UNKNOWN", "x", 1));
    expect("env scan",
           SHREWD_BASIC_ERR_OK == shrewd_basic_env_scan(&cfg, &g_env_cfg_reflect, "APP_"));
    expect("env level", 42 == cfg.log_level);
    expect("env debug", cfg.debug);
    expect("env host", (NULL != cfg.host) && (0 == strcmp(cfg.host, "example.test")));
    expect("env tag", 0 == strcmp(cfg.tag, "v1"));
    expect("env free", SHREWD_BASIC_ERR_OK == shrewd_basic_env_free(&cfg, &g_env_cfg_reflect));

    /* Absent names leave fields untouched (override semantics). */
    cfg.log_level = 7;
    expect("env absent",
           SHREWD_BASIC_ERR_OK == shrewd_basic_env_scan(&cfg, &g_env_cfg_reflect, "NOPE_"));
    expect("env untouched", 7 == cfg.log_level);

    /* Bad values fail strictly. */
    expect("setenv bad", 0 == setenv("APP_LOG_LEVEL", "42x", 1));
    expect("env bad value",
           SHREWD_BASIC_ERR_REFLECT_TEXT
               == shrewd_basic_env_scan(&cfg, &g_env_cfg_reflect, "APP_"));

    /* args: NAME=VALUE and --NAME=VALUE; first match wins. */
    {
        char *argv[] = {"prog", "--APP_LOG_LEVEL=9", "APP_DEBUG=false", "--other=1", "APP_TAG=zz"};
        memset(&cfg, 0, sizeof(cfg));
        expect("args scan",
               SHREWD_BASIC_ERR_OK
                   == shrewd_basic_args_scan(&cfg, &g_env_cfg_reflect, "APP_", 5, argv));
        expect("args level", 9 == cfg.log_level);
        expect("args debug", !cfg.debug);
        expect("args tag", 0 == strcmp(cfg.tag, "zz"));
        expect("args free", SHREWD_BASIC_ERR_OK == shrewd_basic_env_free(&cfg, &g_env_cfg_reflect));
    }

    if (0 != g_failures) {
        printf("test_env: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_env: ok\n");
    return 0;
}

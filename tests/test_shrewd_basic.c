#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/config/build.h"
#include "shrewd-basic/config/config.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/shrewd-basic.h"

static int g_failures = 0;

static void expect_eq_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, actual, expected);
        ++g_failures;
    }
}

static void expect_true(const char *name, int condition)
{
    if (0 == condition) {
        fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

int main(void)
{
    expect_eq_int("clamp above range", shrewd_basic_clamp(15, 0, 10), 10);
    expect_eq_int("clamp below range", shrewd_basic_clamp(-5, 0, 10), 0);
    expect_eq_int("clamp inside range", shrewd_basic_clamp(5, 0, 10), 5);
    expect_eq_int("clamp empty range", shrewd_basic_clamp(5, 10, 0), 10);

    const char *version = NULL;
    expect_eq_int("version status", shrewd_basic_version(&version), SHREWD_BASIC_ERR_OK);
    expect_eq_int("version rejects NULL out", shrewd_basic_version(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    expect_true("build time is positive", 0 < SHREWD_BASIC_BUILD_TIME);
    expect_true("version string shape",
                (0 == strncmp(SHREWD_BASIC_VERSION_STRING, "v", 1))
                && (NULL != strchr(SHREWD_BASIC_VERSION_STRING, ' ')));
    expect_true("version() matches config.h",
                (NULL != version)
                && (0 == strcmp(version, SHREWD_BASIC_VERSION_STRING)));
    expect_true("build env records host and toolchain",
                (NULL != strchr(SHREWD_BASIC_BUILD_ENV, '/'))
                && (3 < strlen(SHREWD_BASIC_BUILD_ENV)));

    expect_true("asserts flag is 0 or 1",
                (0 == SHREWD_BASIC_ENABLE_ASSERTS) || (1 == SHREWD_BASIC_ENABLE_ASSERTS));
    /* Level mask semantics from shrewd-basic/log/log.h: bits 0..5. */
    expect_true("log level mask is in range",
                (0 <= SHREWD_BASIC_LOG_LEVEL)
                && (SHREWD_BASIC_LOG_LEVEL_ALL >= SHREWD_BASIC_LOG_LEVEL));

    /* Error module: names for the shared error codes. */
    expect_true("error name OK",
                0 == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_OK),
                            "SHREWD_BASIC_ERR_OK"));
    expect_true("error name ARG_INVALID",
                0 == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_ARG_INVALID),
                            "SHREWD_BASIC_ERR_ARG_INVALID"));
    expect_true("error name KEY_NOTFOUND",
                0 == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_KEY_NOTFOUND),
                            "SHREWD_BASIC_ERR_KEY_NOTFOUND"));
    expect_true("error name unknown code",
                0 == strcmp(shrewd_basic_error_name(-12345),
                            "SHREWD_BASIC_ERR_UNKNOWN"));
    expect_true("error name REFLECT_NOT_SUPPORTED",
                0 == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED),
                            "SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED"));
    expect_true("error name REFLECT_MISMATCH",
                0 == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_REFLECT_MISMATCH),
                            "SHREWD_BASIC_ERR_REFLECT_MISMATCH"));

    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

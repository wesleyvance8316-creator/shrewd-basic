#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/config/build.h"
#include "shrewd-basic/config/config.h"
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
    if (!condition) {
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

    expect_true("build time is positive", SHREWD_BASIC_BUILD_TIME > 0);
    expect_true("version string shape",
                strncmp(SHREWD_BASIC_VERSION_STRING, "v", 1) == 0
                && strchr(SHREWD_BASIC_VERSION_STRING, ' ') != NULL);
    expect_true("version() matches config.h",
                strcmp(shrewd_basic_version(), SHREWD_BASIC_VERSION_STRING) == 0);
    expect_true("build env records host and toolchain",
                strchr(SHREWD_BASIC_BUILD_ENV, '/') != NULL
                && strlen(SHREWD_BASIC_BUILD_ENV) > 3);

    expect_true("asserts flag is 0 or 1",
                SHREWD_BASIC_ENABLE_ASSERTS == 0 || SHREWD_BASIC_ENABLE_ASSERTS == 1);
    expect_true("log level is in range",
                SHREWD_BASIC_LOG_LEVEL >= 0 && SHREWD_BASIC_LOG_LEVEL <= 4);

    return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

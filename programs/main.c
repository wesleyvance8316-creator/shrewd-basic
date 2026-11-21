#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "shrewd-basic/config/build.h"

int main(void)
{
    const char *version = NULL;
    int major = 0;
    int minor = 0;
    int patch = 0;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_version(&version)) {
        return EXIT_FAILURE;
    }
    (void)shrewd_basic_version_parts(&major, &minor, &patch);
    printf("shrewd-basic %s\n", version);
    printf("version     %d.%d.%d\n", major, minor, patch);
    printf("build time  %" PRId64 "\n", shrewd_basic_build_time());
    printf("build env   %s\n", shrewd_basic_build_env());
    printf("clamp(15, 0, 10) = %d\n", shrewd_basic_clamp(15, 0, 10));
    return EXIT_SUCCESS;
}

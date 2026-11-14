#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "shrewd-basic/config/build.h"
#include "shrewd-basic/shrewd-basic.h"

int main(void)
{
    const char *version = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_version(&version)) {
        return EXIT_FAILURE;
    }
    printf("shrewd-basic %s\n", version);
    printf("build time  %" PRId64 "\n", SHREWD_BASIC_BUILD_TIME);
    printf("build env   %s\n", SHREWD_BASIC_BUILD_ENV);
    printf("clamp(15, 0, 10) = %d\n", shrewd_basic_clamp(15, 0, 10));
    return EXIT_SUCCESS;
}

#include <inttypes.h>
#include <stdio.h>

#include "shrewd-basic/build.h"
#include "shrewd-basic/shrewd-basic.h"

int main(void)
{
    printf("shrewd-basic %s\n", shrewd_basic_version());
    printf("build time  %" PRId64 "\n", SHREWD_BASIC_BUILD_TIME);
    printf("build env   %s\n", SHREWD_BASIC_BUILD_ENV);
    printf("clamp(15, 0, 10) = %d\n", shrewd_basic_clamp(15, 0, 10));
    return 0;
}

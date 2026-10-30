#include "shrewd-basic/shrewd-basic.h"

#include "shrewd-basic/build.h"
#include "shrewd-basic-internal.h"

const char *shrewd_basic_version(void)
{
    return SHREWD_BASIC_VERSION_STRING;
}

int shrewd_basic_clamp(int value, int low, int high)
{
    if (low > high) {
        return low;
    }
    return shrewd_basic_max(low, shrewd_basic_min(value, high));
}

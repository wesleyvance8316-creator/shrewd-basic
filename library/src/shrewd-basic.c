#include "shrewd-basic/shrewd-basic.h"

#include <stddef.h>

#include "shrewd-basic/config/build.h"
#include "shrewd-basic-internal.h"

int shrewd_basic_version(const char **out)
{
    if (NULL == out) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    *out = SHREWD_BASIC_VERSION_STRING;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_clamp(int value, int low, int high)
{
    if (low > high) {
        return low;
    }
    return shrewd_basic_max(low, shrewd_basic_min(value, high));
}

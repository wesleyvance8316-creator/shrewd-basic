#include "shrewd-basic/platform/error.h"

const char *shrewd_basic_error_name(int status)
{
    switch (status) {
    case SHREWD_BASIC_ERR_OK:
        return "SHREWD_BASIC_ERR_OK";
    case SHREWD_BASIC_ERR_MEM_NOMEM:
        return "SHREWD_BASIC_ERR_MEM_NOMEM";
    case SHREWD_BASIC_ERR_ARG_INVALID:
        return "SHREWD_BASIC_ERR_ARG_INVALID";
    case SHREWD_BASIC_ERR_KEY_NOTFOUND:
        return "SHREWD_BASIC_ERR_KEY_NOTFOUND";
    case SHREWD_BASIC_ERR_KEY_EXISTS:
        return "SHREWD_BASIC_ERR_KEY_EXISTS";
    case SHREWD_BASIC_ERR_LOG_FOPEN:
        return "SHREWD_BASIC_ERR_LOG_FOPEN";
    case SHREWD_BASIC_ERR_REFLECT_MISMATCH:
        return "SHREWD_BASIC_ERR_REFLECT_MISMATCH";
    case SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED:
        return "SHREWD_BASIC_ERR_REFLECT_NOT_SUPPORTED";
    default:
        return "SHREWD_BASIC_ERR_UNKNOWN";
    }
}

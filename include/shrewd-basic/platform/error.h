#ifndef SHREWD_BASIC_PLATFORM_ERROR_H
#define SHREWD_BASIC_PLATFORM_ERROR_H

/**
 * @file error.h
 * @brief Generic error codes shared by the whole library.
 *
 * Every non-boolean interface returns one of these codes as an @c int:
 * SHREWD_BASIC_ERR_OK on success and a negative code on failure. Data is
 * handed back through out parameters. Boolean predicates (like the empty()
 * family) return @c bool instead.
 *
 * Naming follows SHREWD_BASIC_ERR_<MODULE>_<INFO>, where the module slot
 * names the error domain (MEM = memory, ARG = arguments, KEY = keys,
 * LOG = log). Every code of the library is registered in this header so
 * shrewd_basic_error_name() can name each one: generic domains occupy
 * -1..-99 and module extension codes are allocated one hundred codes per
 * module (log = -100..-199).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Error codes returned by the library interfaces. */
typedef enum shrewd_basic_error {
    /* Generic codes: -1 .. -99. */
    SHREWD_BASIC_ERR_OK = 0,            /**< Operation succeeded. */
    SHREWD_BASIC_ERR_MEM_NOMEM = -1,    /**< Out of memory. */
    SHREWD_BASIC_ERR_ARG_INVALID = -2,  /**< Invalid argument. */
    SHREWD_BASIC_ERR_KEY_NOTFOUND = -3, /**< Key not found or container empty. */
    SHREWD_BASIC_ERR_KEY_EXISTS = -4,   /**< Duplicate key already stored. */
    /* Log module codes: -100 .. -199. */
    SHREWD_BASIC_ERR_LOG_FOPEN = -100   /**< Log: failed to open the output file. */
} shrewd_basic_error_t;

/**
 * Name of an error code, e.g. "SHREWD_BASIC_ERR_OK"; every code registered
 * in this header is covered. Codes the library does not define map to
 * "SHREWD_BASIC_ERR_UNKNOWN".
 */
const char *shrewd_basic_error_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_PLATFORM_ERROR_H */

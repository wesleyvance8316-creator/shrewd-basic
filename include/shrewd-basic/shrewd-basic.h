#ifndef SHREWD_BASIC_H
#define SHREWD_BASIC_H

/**
 * @file shrewd-basic.h
 * @brief Public API of the shrewd-basic library.
 */

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Store the library version string, e.g. "0.1.0", into @p *out.
 * Returns SHREWD_BASIC_ERR_OK or SHREWD_BASIC_ERR_ARG_INVALID.
 */
int shrewd_basic_version(const char **out);

/**
 * Return @p value clamped into the inclusive range [@p low, @p high].
 * If the range is empty (@p low > @p high), @p low is returned.
 */
int shrewd_basic_clamp(int value, int low, int high);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_H */

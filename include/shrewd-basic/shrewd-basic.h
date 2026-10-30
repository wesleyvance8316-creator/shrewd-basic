#ifndef SHREWD_BASIC_H
#define SHREWD_BASIC_H

/**
 * @file shrewd-basic.h
 * @brief Public API of the shrewd-basic library.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Library version string, e.g. "0.1.0". */
const char *shrewd_basic_version(void);

/**
 * Return @p value clamped into the inclusive range [@p low, @p high].
 * If the range is empty (@p low > @p high), @p low is returned.
 */
int shrewd_basic_clamp(int value, int low, int high);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_H */

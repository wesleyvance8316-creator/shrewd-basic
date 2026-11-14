#ifndef SHREWD_BASIC_TESTS_TEST_REFLECT_EXT_H
#define SHREWD_BASIC_TESTS_TEST_REFLECT_EXT_H

/**
 * @file test_reflect_ext.h
 * @brief Helpers proving reflect descriptors share across translation units.
 *
 * test_reflect_ext.c imports the point_type descriptor defined in
 * test_reflect.c with SHREWD_BASIC_REFLECT_TYPE_DECLARE() and operates on
 * a point_t purely through the metadata - the foreign translation unit
 * never sees the struct definition.
 */

#include <stdint.h>

/** Read point->x through the imported descriptor. */
int shrewd_test_reflect_ext_get_x(const void *point, int32_t *out);

/** Write @p value into point->x through the imported descriptor. */
int shrewd_test_reflect_ext_set_x(void *point, int32_t value);

/** Field count the imported descriptor reports. */
int shrewd_test_reflect_ext_field_count(void);

#endif /* SHREWD_BASIC_TESTS_TEST_REFLECT_EXT_H */

#ifndef SHREWD_BASIC_PROGRAMS_REFLECT_DEMO_EXT_H
#define SHREWD_BASIC_PROGRAMS_REFLECT_DEMO_EXT_H

/**
 * @file reflect_demo_ext.h
 * @brief Second translation unit of the reflect demo.
 *
 * reflect_demo_ext.c imports the point_type descriptor defined in
 * reflect_demo.c with SHREWD_BASIC_REFLECT_TYPE_DECLARE() and works on a
 * point_t purely through the metadata - this file never sees the struct.
 */

#include <stdint.h>

/** Read x + y through the imported descriptor. */
int32_t shrewd_demo_reflect_ext_sum(const void *point);

/** Dump the point through the imported descriptor. */
void shrewd_demo_reflect_ext_dump(const void *point);

#endif /* SHREWD_BASIC_PROGRAMS_REFLECT_DEMO_EXT_H */

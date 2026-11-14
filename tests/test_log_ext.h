#ifndef SHREWD_BASIC_TESTS_TEST_LOG_EXT_H
#define SHREWD_BASIC_TESTS_TEST_LOG_EXT_H

/**
 * @file test_log_ext.h
 * @brief Helpers proving a log module can be shared across translation units.
 *
 * test_log_ext.c imports the module defined in test_log.c with
 * SHREWD_BASIC_LOG_MODULE_DECLARE() and logs through it.
 */

#include "shrewd-basic/log/log.h"

/** The module as seen from the foreign translation unit. */
shrewd_basic_log_module_t *shrewd_test_log_ext_module(void);

/** Log @p msg through the shared module from another translation unit. */
void shrewd_test_log_ext_write(const char *msg);

#endif /* SHREWD_BASIC_TESTS_TEST_LOG_EXT_H */

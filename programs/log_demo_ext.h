#ifndef SHREWD_BASIC_PROGRAMS_LOG_DEMO_EXT_H
#define SHREWD_BASIC_PROGRAMS_LOG_DEMO_EXT_H

/**
 * @file log_demo_ext.h
 * @brief Second translation unit of the log demo.
 *
 * log_demo_ext.c imports the module defined in log_demo.c with
 * SHREWD_BASIC_LOG_MODULE_DECLARE() and logs through it, showing that
 * one module (one level mask, one output, one registry entry) can be
 * shared by every .c of a component.
 */

/** Log a few lines through the shared module from another .c file. */
void shrewd_demo_log_ext_run(void);

#endif /* SHREWD_BASIC_PROGRAMS_LOG_DEMO_EXT_H */

/**
 * @file ds.c
 * @brief Shared log module for the data-structure components.
 *
 * Every ds component (slist, dlist, queue, stack, btree, rbtree) logs
 * through this one "ds" module; a component .c imports it with
 * SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_ds_log). The label part
 * of each line names the component ("slist", "rbtree", ...).
 *
 * Logging policy:
 *   WARN  - invalid arguments (caller misuse)
 *   ERROR - allocation or free failures
 *   DEBUG - container lifecycle (create/destroy)
 *   TRACE - operations and their outcomes (empty, key exists/not found),
 *           plus aborted foreach visits
 * Successful queries (size, empty, front, search hit, ...) stay silent.
 *
 * The module starts at SHREWD_BASIC_LOG_LEVEL_ALL; tune it like any
 * other module:
 *   shrewd_basic_log_module_t *ds = NULL;
 *   shrewd_basic_log_module_find("ds", &ds);
 *   shrewd_basic_log_module_set_level(ds, SHREWD_BASIC_LOG_LEVEL_WARN);
 */

#include "shrewd-basic/log/log.h"

SHREWD_BASIC_LOG_MODULE_DEFINE(shrewd_basic_ds_log, "ds", SHREWD_BASIC_LOG_LEVEL_ALL);

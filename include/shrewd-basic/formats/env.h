#ifndef SHREWD_BASIC_FORMATS_ENV_H
#define SHREWD_BASIC_FORMATS_ENV_H

/**
 * @file env.h
 * @brief Environment variables and command-line arguments -> struct
 *        (twelve-factor style configuration), leaf conversion through
 *        the shared text layer.
 *
 * The variable/argument name is @p prefix + the member name uppercased
 * (underscores kept): prefix "APP_" + member `log_level` matches
 * `APP_LOG_LEVEL`. Only root-level leaf fields map (nested structs are
 * REFLECT_NOT_SUPPORTED); absent names leave the field untouched, so
 * zero the object first for default-zero semantics. Values convert
 * strictly (scan_text), owned strings allocate - release with
 * shrewd_basic_env_free().
 */

#include <stddef.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/**
 * Fill @p object's leaf fields from the environment: a field is set
 * when @c getenv(NAME) returns non-NULL. Unknown variables are ignored.
 */
int shrewd_basic_env_scan(void *object,
                          const shrewd_basic_reflect_type_t *type,
                          const char *prefix);

/**
 * Fill @p object's leaf fields from @p argv entries of the form
 * NAME=VALUE or --NAME=VALUE (NAME matched like env_scan). Other
 * arguments are ignored; the first matching argument wins.
 */
int shrewd_basic_args_scan(void *object,
                           const shrewd_basic_reflect_type_t *type,
                           const char *prefix,
                           int argc,
                           char **argv);

/** Release everything env/args_scan() allocated inside @p object. */
int shrewd_basic_env_free(void *object, const shrewd_basic_reflect_type_t *type);

#endif /* SHREWD_BASIC_FORMATS_ENV_H */

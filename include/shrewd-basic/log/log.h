#ifndef SHREWD_BASIC_LOG_LOG_H
#define SHREWD_BASIC_LOG_LOG_H

/**
 * @file log.h
 * @brief Leveled logging with per-module masks and outputs.
 *
 * Line format (timestamp is local time with microseconds):
 * [YYYY-MM-DD HH:mm:ss.xxxxxx][LEVEL][module][file:line::func()][label] msg
 *
 * When the output stream is a terminal (stdout/stderr connected to a tty),
 * each bracketed part of the line prefix is painted in its own ANSI color
 * and the level tag gets a per-level color; the formatted message itself
 * (and the hex dump) stays uncolored. Streams that are not terminals
 * (files, pipes) receive plain text with no escape sequences.
 *
 * Levels are bit masks: enable a single level, or combine several by
 * adding (bitwise OR) them. Modules are registered at compile time with
 * SHREWD_BASIC_LOG_MODULE_DEFINE, or at runtime with
 * shrewd_basic_log_module_create() for external users. Each module carries
 * its own level mask and its own output: an arbitrary FILE stream or a
 * file path.
 *
 * SHREWD_BASIC_LOG_MODULE_DEFINE() keeps the module storage inside the
 * defining translation unit but exposes the module pointer, so exactly one
 * .c defines it and any sibling .c of the same module shares it through
 * SHREWD_BASIC_LOG_MODULE_DECLARE().
 *
 * Output resolution: a module's own stream (set_stream/set_file) always
 * wins; modules without one fall back to the global default output -
 * stderr while it is unset, or the stream/file configured with
 * shrewd_basic_log_set_global_stream() and shrewd_basic_log_set_global_file().
 * File outputs are opened in append mode and owned by the log module:
 * replacing the output closes the file, and lines are flushed as they are
 * written so the file is complete even after a crash.
 *
 * The logging macros filter on the module level mask before calling into
 * the library, so a disabled level costs a single bit test (no function
 * call). Macros are statements; their arguments must have no side effects
 * beyond the call itself (module and level are evaluated once each).
 * Non-boolean interfaces return shrewd-basic/platform/error.h codes
 * (including the log code SHREWD_BASIC_ERR_LOG_FOPEN):
 *
 *   shrewd_basic_log(level, module, "label", "fmt\n", x, y);
 *   shrewd_basic_log_hex(level, module, "label", "msg", data_ptr, data_size);
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Log levels as bit masks; combine with | to enable several at once. */
typedef enum shrewd_basic_log_level {
    SHREWD_BASIC_LOG_LEVEL_NONE  = 0,
    SHREWD_BASIC_LOG_LEVEL_FATAL = 1 << 0,
    SHREWD_BASIC_LOG_LEVEL_ERROR = 1 << 1,
    SHREWD_BASIC_LOG_LEVEL_WARN  = 1 << 2,
    SHREWD_BASIC_LOG_LEVEL_INFO  = 1 << 3,
    SHREWD_BASIC_LOG_LEVEL_DEBUG = 1 << 4,
    SHREWD_BASIC_LOG_LEVEL_TRACE = 1 << 5,
    SHREWD_BASIC_LOG_LEVEL_ALL   = (1 << 6) - 1
} shrewd_basic_log_level_t;

/** A logging module: name, enabled levels, output and registry linkage. */
typedef struct shrewd_basic_log_module {
    const char *name;
    uint32_t level_mask;
    FILE *out;       /**< NULL falls back to the global default output. */
    int owns_out;    /**< Close out when it is replaced or the module dies. */
    int heap;        /**< Created by shrewd_basic_log_module_create(). */
    int registered;  /**< Already linked into the global registry. */
    struct shrewd_basic_log_module *next;
} shrewd_basic_log_module_t;

/*
 * Compile-time module registration: define `var` exactly once, in a single
 * .c of the module. The module storage itself stays file-local; only the
 * pointer `var` gets external linkage so sibling translation units can
 * share the very same module with SHREWD_BASIC_LOG_MODULE_DECLARE().
 * The module joins the global registry automatically on first use
 * (shrewd_basic_log_module_register() is optional).
 */
#define SHREWD_BASIC_LOG_MODULE_DEFINE(var, module_name, mask)                 \
    static shrewd_basic_log_module_t var##_impl = {                            \
        .name = (module_name), .level_mask = (uint32_t)(mask) };               \
    shrewd_basic_log_module_t *const var = &var##_impl

/*
 * Import a module defined by SHREWD_BASIC_LOG_MODULE_DEFINE() in another
 * translation unit of the same module, so every .c logs through one shared
 * module (one level mask, one output, one registry entry):
 *   SHREWD_BASIC_LOG_MODULE_DECLARE(var);
 */
#define SHREWD_BASIC_LOG_MODULE_DECLARE(var)                                   \
    extern shrewd_basic_log_module_t *const var

/** Runtime module registration: create and register a module by name. */
int shrewd_basic_log_module_create(const char *name, uint32_t level_mask,
                                   shrewd_basic_log_module_t **out);

/**
 * Unregister the module, close its owned output and free it when it was
 * created at runtime (compile-time modules only lose their registration).
 */
int shrewd_basic_log_module_destroy(shrewd_basic_log_module_t *module);

/** Explicitly register a compile-time defined module (optional). */
int shrewd_basic_log_module_register(shrewd_basic_log_module_t *module);

/**
 * Find a registered module by name.
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no module matches.
 */
int shrewd_basic_log_module_find(const char *name, shrewd_basic_log_module_t **out);

/** Set which levels (bit mask) are enabled for this module. */
int shrewd_basic_log_module_set_level(shrewd_basic_log_module_t *module,
                                      uint32_t level_mask);

/** Send this module's output to @p stream (e.g. stdout/stderr). */
int shrewd_basic_log_module_set_stream(shrewd_basic_log_module_t *module,
                                       FILE *stream);

/**
 * Send this module's output to the file at @p path (opened in append mode;
 * the module closes it when the output is replaced or the module dies).
 * Returns SHREWD_BASIC_ERR_LOG_FOPEN when the file cannot be opened.
 */
int shrewd_basic_log_module_set_file(shrewd_basic_log_module_t *module,
                                     const char *path);

/**
 * Send every module without its own output to @p stream (e.g. stdout or
 * stderr); replacing a file-backed global output closes that file.
 */
int shrewd_basic_log_set_global_stream(FILE *stream);

/**
 * Send every module without its own output to the file at @p path (opened
 * in append mode; owned by the log module and closed when the output is
 * replaced). Returns SHREWD_BASIC_ERR_LOG_FOPEN when the file cannot be
 * opened - the previous global output stays in place.
 */
int shrewd_basic_log_set_global_file(const char *path);

/** Core writer used by shrewd_basic_log(); the format is printf-style. */
int shrewd_basic_log_write(shrewd_basic_log_level_t level,
                           shrewd_basic_log_module_t *module, const char *label,
                           const char *file, int line, const char *func,
                           const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 7, 8)))
#endif
    ;

/** Core writer used by shrewd_basic_log_hex(). */
int shrewd_basic_log_hex_write(shrewd_basic_log_level_t level,
                               shrewd_basic_log_module_t *module,
                               const char *label, const char *msg,
                               const void *data, size_t size,
                               const char *file, int line, const char *func);

/**
 * Log one printf-style message; include "\n" in @c fmt yourself:
 * shrewd_basic_log(level, module, "label", "fmt\n", x, y);
 *
 * Statement macro: the level mask is tested inline and nothing is called
 * when the level is disabled. @p module and @p level are evaluated once.
 */
#define shrewd_basic_log(level, module, label, ...)                            \
    do {                                                                       \
        shrewd_basic_log_module_t *const _log_module = (module);               \
        shrewd_basic_log_level_t const _log_level = (level);                   \
        if ((NULL != _log_module) && (0u != (_log_module->level_mask           \
                & (uint32_t)_log_level))) {                                    \
            (void)shrewd_basic_log_write(_log_level, _log_module,              \
                                         (label), __FILE__, __LINE__,          \
                                         __func__, __VA_ARGS__);               \
        }                                                                      \
    } while (0)

/**
 * Log a message plus a hex dump of a buffer:
 * shrewd_basic_log_hex(level, module, "label", "msg", data_ptr, data_size);
 *
 * First line:
 * [...]...][label] msg, data_ptr: 0xAAFFAAFFAAFFAAFF, size: (0xaabb) 1234
 * Then one line per 16 bytes, offset as (0xHEX) DECIMAL:
 * (0x0000) 0000 | 01 02 03 04 05 06 07 08  01 02 03 04 05 06 07 08 | ................ |
 *
 * Statement macro: the level mask is tested inline and nothing is called
 * when the level is disabled. @p module and @p level are evaluated once.
 */
#define shrewd_basic_log_hex(level, module, label, msg, data_ptr, data_size)   \
    do {                                                                       \
        shrewd_basic_log_module_t *const _log_module = (module);               \
        shrewd_basic_log_level_t const _log_level = (level);                   \
        if ((NULL != _log_module) && (0u != (_log_module->level_mask           \
                & (uint32_t)_log_level))) {                                    \
            (void)shrewd_basic_log_hex_write(_log_level, _log_module,          \
                                             (label), (msg), (data_ptr),       \
                                             (data_size), __FILE__, __LINE__,  \
                                             __func__);                        \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_LOG_LOG_H */

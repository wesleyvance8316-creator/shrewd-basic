/* fileno()/isatty() for terminal color detection (POSIX, not plain C11). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "shrewd-basic/log/log.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "shrewd-basic/platform/memory.h"

#if defined(_WIN32)
#include <io.h>
#define log_isatty(stream) _isatty(_fileno(stream))
#else
#include <unistd.h>
#define log_isatty(stream) isatty(fileno(stream))
#endif

/*
 * ANSI colors for the line prefix. Each bracketed part gets its own color
 * and the level tag gets a per-level color; the message (and the hex dump)
 * stays uncolored. Empty strings are substituted when color is off, so the
 * printf shape never changes.
 */
#define LOG_COLOR_RESET   "\033[0m"
#define LOG_COLOR_TIME    "\033[0;90m"  /* gray    */
#define LOG_COLOR_MODULE  "\033[0;34m"  /* blue    */
#define LOG_COLOR_WHERE   "\033[0;35m"  /* magenta */
#define LOG_COLOR_LABEL   "\033[0;33m"  /* yellow  */

/* Registry of every module seen so far (compile-time and runtime). */
static shrewd_basic_log_module_t *g_modules = NULL;

/* Global default output for modules without their own stream. */
static FILE *g_global_out = NULL;
static int g_global_owns_out = 0;

/*
 * Optional lock hooks (shrewd_basic_log_set_lock): when installed, every
 * registry operation, stream replacement and record write runs inside
 * lock/unlock; when absent the module behaves exactly as before. The
 * enabled-level short-circuit stays outside the lock either way.
 */
static void (*g_lock_fn)(void *ctx) = NULL;
static void (*g_unlock_fn)(void *ctx) = NULL;
static void *g_lock_ctx = NULL;

static void log_lock(void)
{
    if (NULL != g_lock_fn) {
        g_lock_fn(g_lock_ctx);
    }
}

static void log_unlock(void)
{
    if (NULL != g_unlock_fn) {
        g_unlock_fn(g_lock_ctx);
    }
}

static void log_touch(shrewd_basic_log_module_t *module)
{
    if (0 == module->registered) {
        module->next = g_modules;
        g_modules = module;
        module->registered = 1;
    }
}

static void log_unlink(shrewd_basic_log_module_t *module)
{
    if (0 == module->registered) {
        return;
    }
    shrewd_basic_log_module_t **slot = &g_modules;
    while (NULL != *slot) {
        if (*slot == module) {
            *slot = module->next;
            break;
        }
        slot = &(*slot)->next;
    }
    module->next = NULL;
    module->registered = 0;
}

static FILE *log_output(shrewd_basic_log_module_t *module)
{
    if (NULL != module->out) {
        return module->out;
    }
    return (NULL != g_global_out) ? g_global_out : stderr;
}

static const char *log_level_name(shrewd_basic_log_level_t level)
{
    const char *name = "UNKNOWN";
    switch (level) {
    case SHREWD_BASIC_LOG_LEVEL_FATAL:
        name = "FATAL";
        break;
    case SHREWD_BASIC_LOG_LEVEL_ERROR:
        name = "ERROR";
        break;
    case SHREWD_BASIC_LOG_LEVEL_WARN:
        name = "WARN";
        break;
    case SHREWD_BASIC_LOG_LEVEL_INFO:
        name = "INFO";
        break;
    case SHREWD_BASIC_LOG_LEVEL_DEBUG:
        name = "DEBUG";
        break;
    case SHREWD_BASIC_LOG_LEVEL_TRACE:
        name = "TRACE";
        break;
    case SHREWD_BASIC_LOG_LEVEL_NONE:
    case SHREWD_BASIC_LOG_LEVEL_ALL:
    default:
        name = "UNKNOWN";
        break;
    }
    return name;
}

static const char *log_level_color(shrewd_basic_log_level_t level)
{
    const char *color = "\033[1;37m";  /* bold white */
    switch (level) {
    case SHREWD_BASIC_LOG_LEVEL_FATAL:
        color = "\033[1;31m";  /* bold red    */
        break;
    case SHREWD_BASIC_LOG_LEVEL_ERROR:
        color = "\033[0;31m";  /* red         */
        break;
    case SHREWD_BASIC_LOG_LEVEL_WARN:
        color = "\033[1;33m";  /* bold yellow */
        break;
    case SHREWD_BASIC_LOG_LEVEL_INFO:
        color = "\033[0;32m";  /* green       */
        break;
    case SHREWD_BASIC_LOG_LEVEL_DEBUG:
        color = "\033[0;36m";  /* cyan        */
        break;
    case SHREWD_BASIC_LOG_LEVEL_TRACE:
        color = "\033[0;37m";  /* white       */
        break;
    case SHREWD_BASIC_LOG_LEVEL_NONE:
    case SHREWD_BASIC_LOG_LEVEL_ALL:
    default:
        color = "\033[1;37m";  /* bold white */
        break;
    }
    return color;
}

/* Colors only reach real terminals; files and pipes get plain text. */
static int log_use_color(FILE *stream)
{
    return 0 != log_isatty(stream);
}

static const char *log_basename(const char *path)
{
    const char *base = path;
    const char *cursor = path;
    while ('\0' != *cursor) {
        if (('/' == *cursor) || ('\\' == *cursor)) {
            base = cursor + 1;
        }
        ++cursor;
    }
    return base;
}

/* "[YYYY-MM-DD HH:mm:ss.xxxxxx]", local time, microseconds. */
static void log_timestamp(char *buf, size_t size)
{
    struct timespec ts;
    char timebuf[32];

    if (TIME_UTC != timespec_get(&ts, TIME_UTC)) {
        snprintf(buf, size, "[1970-01-01 00:00:00.000000]");
        return;
    }
    time_t seconds = ts.tv_sec;
    struct tm *local = localtime(&seconds);
    if (NULL == local) {
        snprintf(buf, size, "[1970-01-01 00:00:00.000000]");
        return;
    }
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", local);
    snprintf(buf, size, "[%s.%06d]", timebuf, (int)(ts.tv_nsec / 1000));
}

static int log_enabled(const shrewd_basic_log_module_t *module,
                       shrewd_basic_log_level_t level)
{
    return 0u != (module->level_mask & (uint32_t)level);
}

int shrewd_basic_log_set_lock(void (*lock_fn)(void *ctx),
                              void (*unlock_fn)(void *ctx), void *ctx)
{
    if (((NULL == lock_fn) && (NULL != unlock_fn))
            || ((NULL != lock_fn) && (NULL == unlock_fn))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    g_lock_fn = lock_fn;
    g_unlock_fn = unlock_fn;
    g_lock_ctx = ctx;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_module_create(const char *name, uint32_t level_mask,
                                   shrewd_basic_log_module_t **out)
{
    if ((NULL == name) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    void *block = NULL;
    int status = shrewd_basic_platform_calloc(&block, 1,
                                              sizeof(shrewd_basic_log_module_t));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    shrewd_basic_log_module_t *module = block;
    module->name = name;
    module->level_mask = level_mask;
    module->heap = 1;
    log_lock();
    log_touch(module);
    log_unlock();
    *out = module;
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_module_destroy(shrewd_basic_log_module_t *module)
{
    int status = SHREWD_BASIC_ERR_OK;
    if (NULL == module) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    log_lock();
    log_unlink(module);
    if ((0 != module->owns_out) && (NULL != module->out)) {
        fclose(module->out);
        module->out = NULL;
        module->owns_out = 0;
    }
    if (0 != module->heap) {
        status = shrewd_basic_platform_free((void **)&module);
    }
    log_unlock();
    return status;
}

int shrewd_basic_log_module_register(shrewd_basic_log_module_t *module)
{
    if (NULL == module) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    log_lock();
    log_touch(module);
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_module_find(const char *name, shrewd_basic_log_module_t **out)
{
    if ((NULL == name) || (NULL == out)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    log_lock();
    shrewd_basic_log_module_t *module = g_modules;
    while (NULL != module) {
        if (0 == strcmp(module->name, name)) {
            *out = module;
            log_unlock();
            return SHREWD_BASIC_ERR_OK;
        }
        module = module->next;
    }
    log_unlock();
    return SHREWD_BASIC_ERR_KEY_NOTFOUND;
}

int shrewd_basic_log_module_set_level(shrewd_basic_log_module_t *module,
                                      uint32_t level_mask)
{
    if (NULL == module) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    log_lock();
    log_touch(module);
    module->level_mask = level_mask;
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_module_set_stream(shrewd_basic_log_module_t *module,
                                       FILE *stream)
{
    if ((NULL == module) || (NULL == stream)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    log_lock();
    log_touch(module);
    if ((0 != module->owns_out) && (NULL != module->out)) {
        fclose(module->out);
    }
    module->out = stream;
    module->owns_out = 0;
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_module_set_file(shrewd_basic_log_module_t *module,
                                     const char *path)
{
    if ((NULL == module) || (NULL == path)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    FILE *file = fopen(path, "a");
    if (NULL == file) {
        return SHREWD_BASIC_ERR_LOG_FOPEN;
    }
    log_lock();
    log_touch(module);
    if ((0 != module->owns_out) && (NULL != module->out)) {
        fclose(module->out);
    }
    module->out = file;
    module->owns_out = 1;
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_set_global_stream(FILE *stream)
{
    if (NULL == stream) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    log_lock();
    if ((0 != g_global_owns_out) && (NULL != g_global_out)) {
        fclose(g_global_out);
    }
    g_global_out = stream;
    g_global_owns_out = 0;
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_set_global_file(const char *path)
{
    if (NULL == path) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    FILE *file = fopen(path, "a");
    if (NULL == file) {
        return SHREWD_BASIC_ERR_LOG_FOPEN;
    }
    log_lock();
    if ((0 != g_global_owns_out) && (NULL != g_global_out)) {
        fclose(g_global_out);
    }
    g_global_out = file;
    g_global_owns_out = 1;
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_write(shrewd_basic_log_level_t level,
                           shrewd_basic_log_module_t *module, const char *label,
                           const char *file, int line, const char *func,
                           const char *fmt, ...)
{
    if ((NULL == module) || (NULL == label) || (NULL == file)
            || (NULL == func) || (NULL == fmt)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == log_enabled(module, level)) {
        return SHREWD_BASIC_ERR_OK;
    }
    log_lock();
    log_touch(module);

    char timestamp[64];
    log_timestamp(timestamp, sizeof(timestamp));

    FILE *out = log_output(module);
    const int color = log_use_color(out);
    const char *end = (0 != color) ? LOG_COLOR_RESET : "";
    const char *c_time = (0 != color) ? LOG_COLOR_TIME : "";
    const char *c_level = (0 != color) ? log_level_color(level) : "";
    const char *c_module = (0 != color) ? LOG_COLOR_MODULE : "";
    const char *c_where = (0 != color) ? LOG_COLOR_WHERE : "";
    const char *c_label = (0 != color) ? LOG_COLOR_LABEL : "";

    fprintf(out, "%s%s%s%s[%s]%s%s[%s]%s%s[%s:%d::%s()]%s%s[%s]%s ",
            c_time, timestamp, end,
            c_level, log_level_name(level), end,
            c_module, module->name, end,
            c_where, log_basename(file), line, func, end,
            c_label, label, end);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fflush(out);
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_log_hex_write(shrewd_basic_log_level_t level,
                               shrewd_basic_log_module_t *module,
                               const char *label, const char *msg,
                               const void *data, size_t size,
                               const char *file, int line, const char *func)
{
    if ((NULL == module) || (NULL == label) || (NULL == msg) || (NULL == file)
            || (NULL == func) || ((0 != size) && (NULL == data))) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    if (0 == log_enabled(module, level)) {
        return SHREWD_BASIC_ERR_OK;
    }
    log_lock();
    log_touch(module);

    char timestamp[64];
    log_timestamp(timestamp, sizeof(timestamp));

    FILE *out = log_output(module);
    const int color = log_use_color(out);
    const char *end = (0 != color) ? LOG_COLOR_RESET : "";
    const char *c_time = (0 != color) ? LOG_COLOR_TIME : "";
    const char *c_level = (0 != color) ? log_level_color(level) : "";
    const char *c_module = (0 != color) ? LOG_COLOR_MODULE : "";
    const char *c_where = (0 != color) ? LOG_COLOR_WHERE : "";
    const char *c_label = (0 != color) ? LOG_COLOR_LABEL : "";

    /* Colored line prefix; msg and the hex dump below stay uncolored. */
    fprintf(out, "%s%s%s%s[%s]%s%s[%s]%s%s[%s:%d::%s()]%s%s[%s]%s"
                 " %s, data_ptr: 0x%016llX, size: (0x%04zx) %zu\n",
            c_time, timestamp, end,
            c_level, log_level_name(level), end,
            c_module, module->name, end,
            c_where, log_basename(file), line, func, end,
            c_label, label, end,
            msg, (unsigned long long)(uintptr_t)data, size, size);

    const unsigned char *bytes = data;
    for (size_t offset = 0; offset < size; offset += 16) {
        size_t row = size - offset;
        if (16 < row) {
            row = 16;
        }
        /* Offset twice: (0xHEX) DECIMAL, matching the size field above. */
        fprintf(out, "(0x%04zx) %04zu | ", offset, offset);
        for (size_t i = 0; 16 > i; ++i) {
            if (8 == i) {
                fputc(' ', out);
            }
            if (i < row) {
                fprintf(out, "%02x ", bytes[offset + i]);
            } else {
                fprintf(out, "   ");
            }
        }
        fprintf(out, "| ");
        for (size_t i = 0; 16 > i; ++i) {
            unsigned char c = ' ';
            if (i < row) {
                c = bytes[offset + i];
                if ((' ' > c) || ('~' < c)) {
                    c = '.';
                }
            }
            fputc((int)c, out);
        }
        fprintf(out, " |\n");
    }
    fflush(out);
    log_unlock();
    return SHREWD_BASIC_ERR_OK;
}

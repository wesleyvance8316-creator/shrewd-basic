#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/log/log.h"

#include "test_log_ext.h"

static int g_failures = 0;

static void expect_eq_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, actual, expected);
        ++g_failures;
    }
}

static void expect_true(const char *name, int condition)
{
    if (0 == condition) {
        fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

/* Compile-time module registration. */
SHREWD_BASIC_LOG_MODULE_DEFINE(g_cmod, "cmod", SHREWD_BASIC_LOG_LEVEL_ALL);

/* Read a stream's whole content into buf (NUL-terminated). */
static void slurp(FILE *stream, char *buf, size_t size)
{
    size_t n = 0;
    rewind(stream);
    n = fread(buf, 1, size - 1, stream);
    buf[n] = '\0';
}

static void test_format(void)
{
    FILE *stream = tmpfile();
    expect_true("log format tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    expect_eq_int("log format set_stream",
                  shrewd_basic_log_module_set_stream(g_cmod, stream),
                  SHREWD_BASIC_ERR_OK);

    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, g_cmod, "lab", "hello %d\n", 42);

    char buf[1024] = {0};
    slurp(stream, buf, sizeof(buf));

    /* [YYYY-MM-DD HH:mm:ss.xxxxxx] occupies the first 28 characters. */
    expect_true("log timestamp shape",
                ('[' == buf[0]) && ('-' == buf[5]) && ('-' == buf[8])
                && (' ' == buf[11]) && (':' == buf[14]) && (':' == buf[17])
                && ('.' == buf[20]) && (']' == buf[27]));
    expect_true("log header fields",
                (NULL != strstr(buf, "][INFO][cmod]["))
                && (NULL != strstr(buf, "][test_log.c:"))
                && (NULL != strstr(buf, "::test_format()][lab] hello 42\n")));
    /* Non-terminal output must stay free of ANSI color escapes. */
    expect_true("log file has no color", NULL == strstr(buf, "\033"));

    shrewd_basic_log_module_set_stream(g_cmod, stderr);
    fclose(stream);
}

static void test_level_mask(void)
{
    FILE *stream = tmpfile();
    expect_true("log mask tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(g_cmod, stream);

    expect_eq_int("log level single",
                  shrewd_basic_log_module_set_level(g_cmod, SHREWD_BASIC_LOG_LEVEL_ERROR),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, g_cmod, "m", "info hidden\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, g_cmod, "m", "error shown\n");

    char buf[1024] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("log single level filtered", NULL == strstr(buf, "info hidden"));
    expect_true("log single level passes",
                (NULL != strstr(buf, "error shown"))
                && (NULL != strstr(buf, "[ERROR][cmod]")));

    /* Several levels enabled by adding the masks. */
    expect_eq_int("log level mask sum",
                  shrewd_basic_log_module_set_level(g_cmod,
                      SHREWD_BASIC_LOG_LEVEL_ERROR | SHREWD_BASIC_LOG_LEVEL_WARN),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, g_cmod, "m", "warn shown\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, g_cmod, "m", "debug hidden\n");

    slurp(stream, buf, sizeof(buf));
    expect_true("log mask passes both levels",
                (NULL != strstr(buf, "error shown"))
                && (NULL != strstr(buf, "warn shown"))
                && (NULL != strstr(buf, "[WARN][cmod]")));
    expect_true("log mask filters others", NULL == strstr(buf, "debug hidden"));

    shrewd_basic_log_module_set_level(g_cmod, SHREWD_BASIC_LOG_LEVEL_ALL);
    shrewd_basic_log_module_set_stream(g_cmod, stderr);
    fclose(stream);
}

static void test_hex(void)
{
    unsigned char data[20] = {0};
    for (size_t i = 0; i < sizeof(data); ++i) {
        data[i] = (unsigned char)(i + 1);
    }

    FILE *stream = tmpfile();
    expect_true("log hex tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(g_cmod, stream);

    shrewd_basic_log_hex(SHREWD_BASIC_LOG_LEVEL_INFO, g_cmod, "hex", "msg",
                         data, sizeof(data));

    char buf[4096] = {0};
    slurp(stream, buf, sizeof(buf));

    expect_true("log hex info line",
                (NULL != strstr(buf, "::test_hex()][hex] msg, data_ptr: 0x"))
                && (NULL != strstr(buf, ", size: (0x0014) 20\n")));
    /* Full first row: offset (0xHEX) DECIMAL, 16 bytes, 16 ASCII dots. */
    expect_true("log hex row 1",
                NULL != strstr(buf,
                    "(0x0000) 0000 | 01 02 03 04 05 06 07 08  09 0a 0b 0c 0d 0e 0f 10"
                    " | ................ |"));
    /* Second row holds the remaining 4 bytes. */
    expect_true("log hex row 2", NULL != strstr(buf, "(0x0010) 0016 | 11 12 13 14 "));

    shrewd_basic_log_module_set_stream(g_cmod, stderr);
    fclose(stream);
}

static void test_runtime_module(void)
{
    shrewd_basic_log_module_t *mod = NULL;
    expect_eq_int("log module create",
                  shrewd_basic_log_module_create("rmod", SHREWD_BASIC_LOG_LEVEL_ALL, &mod),
                  SHREWD_BASIC_ERR_OK);
    expect_true("log module create out", NULL != mod);
    expect_eq_int("log module create rejects NULL name",
                  shrewd_basic_log_module_create(NULL, SHREWD_BASIC_LOG_LEVEL_ALL, &mod),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    const char *path = "shrewd-basic-log-test.txt";
    expect_eq_int("log set_file",
                  shrewd_basic_log_module_set_file(mod, path), SHREWD_BASIC_ERR_OK);
    expect_eq_int("log set_file bad path",
                  shrewd_basic_log_module_set_file(mod, "/no/such/dir/log.txt"),
                  SHREWD_BASIC_ERR_LOG_FOPEN);
    /* Module extension codes are named by the error module too. */
    expect_true("log error name",
                0 == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_LOG_FOPEN),
                            "SHREWD_BASIC_ERR_LOG_FOPEN"));
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, mod, "file", "to file\n");

    shrewd_basic_log_module_t *found = NULL;
    expect_eq_int("log find", shrewd_basic_log_module_find("rmod", &found),
                  SHREWD_BASIC_ERR_OK);
    expect_true("log find pointer", found == mod);

    expect_eq_int("log module destroy", shrewd_basic_log_module_destroy(mod),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("log module destroy NULL", shrewd_basic_log_module_destroy(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    found = NULL;
    expect_eq_int("log find after destroy", shrewd_basic_log_module_find("rmod", &found),
                  SHREWD_BASIC_ERR_KEY_NOTFOUND);

    FILE *file = fopen(path, "r");
    expect_true("log file exists", NULL != file);
    if (NULL != file) {
        char buf[1024] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, file);
        buf[n] = '\0';
        fclose(file);
        expect_true("log file content",
                    (NULL != strstr(buf, "[ERROR][rmod]["))
                    && (NULL != strstr(buf, "to file\n")));
    }
    remove(path);
}

static void test_registry(void)
{
    expect_eq_int("log register", shrewd_basic_log_module_register(g_cmod),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_log_module_t *found = NULL;
    expect_eq_int("log find compile-time", shrewd_basic_log_module_find("cmod", &found),
                  SHREWD_BASIC_ERR_OK);
    expect_true("log find compile-time pointer", found == g_cmod);

    expect_eq_int("log register NULL", shrewd_basic_log_module_register(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("log find rejects NULL name",
                  shrewd_basic_log_module_find(NULL, &found),
                  SHREWD_BASIC_ERR_ARG_INVALID);
}

static void test_shared_module(void)
{
    /* The foreign translation unit must see the very same module. */
    expect_true("log shared module pointer", shrewd_test_log_ext_module() == g_cmod);

    FILE *stream = tmpfile();
    expect_true("log shared tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }
    shrewd_basic_log_module_set_stream(g_cmod, stream);

    shrewd_test_log_ext_write("from ext");

    char buf[1024] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("log shared module output",
                (NULL != strstr(buf, "][INFO][cmod]["))
                && (NULL != strstr(buf, "][test_log_ext.c:"))
                && (NULL != strstr(buf, "::shrewd_test_log_ext_write()][ext] from ext\n")));

    shrewd_basic_log_module_set_stream(g_cmod, stderr);
    fclose(stream);
}

static void test_global_output(void)
{
    const char *path = "shrewd-basic-log-global-test.txt";

    expect_eq_int("log global set_file",
                  shrewd_basic_log_set_global_file(path), SHREWD_BASIC_ERR_OK);
    /* A failed open must keep the previous global output in place. */
    expect_eq_int("log global set_file bad path",
                  shrewd_basic_log_set_global_file("/no/such/dir/log.txt"),
                  SHREWD_BASIC_ERR_LOG_FOPEN);
    expect_eq_int("log global set_file NULL",
                  shrewd_basic_log_set_global_file(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("log global set_stream NULL",
                  shrewd_basic_log_set_global_stream(NULL),
                  SHREWD_BASIC_ERR_ARG_INVALID);

    /* A module without its own stream follows the global file. */
    shrewd_basic_log_module_t *mod = NULL;
    expect_eq_int("log global module create",
                  shrewd_basic_log_module_create("gmod", SHREWD_BASIC_LOG_LEVEL_ALL, &mod),
                  SHREWD_BASIC_ERR_OK);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, mod, "global", "to global file\n");
    expect_eq_int("log global module destroy",
                  shrewd_basic_log_module_destroy(mod), SHREWD_BASIC_ERR_OK);

    /* A module with its own stream still wins over the global file. */
    FILE *stream = tmpfile();
    expect_true("log global tmpfile", NULL != stream);
    if (NULL == stream) {
        (void)shrewd_basic_log_set_global_stream(stderr);
        return;
    }
    shrewd_basic_log_module_t *own = NULL;
    (void)shrewd_basic_log_module_create("gown", SHREWD_BASIC_LOG_LEVEL_ALL, &own);
    (void)shrewd_basic_log_module_set_stream(own, stream);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, own, "own", "to own stream\n");
    (void)shrewd_basic_log_module_destroy(own);

    char buf[1024] = {0};
    slurp(stream, buf, sizeof(buf));
    expect_true("log global own stream wins", NULL != strstr(buf, "to own stream\n"));
    fclose(stream);

    memset(buf, 0, sizeof(buf));
    FILE *file = fopen(path, "r");
    expect_true("log global file exists", NULL != file);
    if (NULL != file) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, file);
        buf[n] = '\0';
        fclose(file);
        expect_true("log global file content",
                    (NULL != strstr(buf, "][gmod]"))
                    && (NULL != strstr(buf, "to global file\n"))
                    && (NULL == strstr(buf, "to own stream")));
    }
    remove(path);

    /* Resetting closes the owned file and restores the stderr default. */
    expect_eq_int("log global reset",
                  shrewd_basic_log_set_global_stream(stderr), SHREWD_BASIC_ERR_OK);
}

/* Lock-hook accounting for test_lock_hooks. */
static int g_lock_calls = 0;
static int g_unlock_calls = 0;
static void *g_lock_ctx_seen = NULL;
static void *g_unlock_ctx_seen = NULL;

static void test_lock_cb(void *ctx)
{
    ++g_lock_calls;
    g_lock_ctx_seen = ctx;
}

static void test_unlock_cb(void *ctx)
{
    ++g_unlock_calls;
    g_unlock_ctx_seen = ctx;
}

static void test_lock_hooks(void)
{
    static int marker = 0;
    shrewd_basic_log_module_t *mod = NULL;
    shrewd_basic_log_module_t *found = NULL;
    FILE *stream = tmpfile();
    int before = 0;

    expect_true("log lock tmpfile", NULL != stream);
    if (NULL == stream) {
        return;
    }

    /* A half-installed pair is rejected. */
    expect_eq_int("log set_lock half 1",
                  shrewd_basic_log_set_lock(test_lock_cb, NULL, &marker),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("log set_lock half 2",
                  shrewd_basic_log_set_lock(NULL, test_unlock_cb, &marker),
                  SHREWD_BASIC_ERR_ARG_INVALID);
    expect_eq_int("log set_lock install",
                  shrewd_basic_log_set_lock(test_lock_cb, test_unlock_cb, &marker),
                  SHREWD_BASIC_ERR_OK);

    /* create/find/set_level/set_stream/write/destroy all pass the lock. */
    expect_eq_int("lock create",
                  shrewd_basic_log_module_create("lmod",
                                                 SHREWD_BASIC_LOG_LEVEL_ALL, &mod),
                  SHREWD_BASIC_ERR_OK);
    expect_true("lock create took lock", 1 == g_lock_calls);
    expect_true("lock create balanced", g_lock_calls == g_unlock_calls);
    expect_eq_int("lock set_stream",
                  shrewd_basic_log_module_set_stream(mod, stream),
                  SHREWD_BASIC_ERR_OK);
    expect_eq_int("lock find", shrewd_basic_log_module_find("lmod", &found),
                  SHREWD_BASIC_ERR_OK);
    expect_true("lock find pointer", found == mod);
    expect_true("lock find took lock", 3 == g_lock_calls);
    expect_true("lock ctx passed", (&marker == g_lock_ctx_seen)
                && (&marker == g_unlock_ctx_seen));

    /* A disabled level short-circuits outside the lock. */
    expect_eq_int("lock level none",
                  shrewd_basic_log_module_set_level(mod,
                                                    SHREWD_BASIC_LOG_LEVEL_NONE),
                  SHREWD_BASIC_ERR_OK);
    before = g_lock_calls;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, mod, "lock", "hidden\n");
    expect_true("lock skipped when disabled", before == g_lock_calls);
    expect_true("lock balanced when disabled", g_lock_calls == g_unlock_calls);

    /* An enabled write holds the lock across the whole record. */
    expect_eq_int("lock level all",
                  shrewd_basic_log_module_set_level(mod,
                                                    SHREWD_BASIC_LOG_LEVEL_ALL),
                  SHREWD_BASIC_ERR_OK);
    before = g_lock_calls;
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, mod, "lock", "visible\n");
    expect_true("lock taken once per write", (before + 1) == g_lock_calls);
    expect_true("lock write balanced", g_lock_calls == g_unlock_calls);

    /* Global stream replacement passes the lock. */
    before = g_lock_calls;
    expect_eq_int("lock set_global_stream",
                  shrewd_basic_log_set_global_stream(stderr),
                  SHREWD_BASIC_ERR_OK);
    expect_true("lock global took lock", (before + 1) == g_lock_calls);

    expect_eq_int("lock destroy", shrewd_basic_log_module_destroy(mod),
                  SHREWD_BASIC_ERR_OK);
    expect_true("lock destroy balanced", g_lock_calls == g_unlock_calls);

    /* Uninstall: the module behaves exactly as before. */
    expect_eq_int("log set_lock off",
                  shrewd_basic_log_set_lock(NULL, NULL, NULL),
                  SHREWD_BASIC_ERR_OK);
    before = g_lock_calls;
    expect_eq_int("lock off register",
                  shrewd_basic_log_module_register(g_cmod), SHREWD_BASIC_ERR_OK);
    expect_true("lock off silent", before == g_lock_calls);
    fclose(stream);
}

int main(void)
{
    test_format();
    test_level_mask();
    test_hex();
    test_runtime_module();
    test_registry();
    test_shared_module();
    test_global_output();
    test_lock_hooks();
    return (0 == g_failures) ? EXIT_SUCCESS : EXIT_FAILURE;
}

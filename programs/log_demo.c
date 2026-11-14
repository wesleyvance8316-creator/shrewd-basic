/**
 * @file log_demo.c
 * @brief Walkthrough of every shrewd-basic/log interface.
 *
 * Run it in a terminal to see the full effect: when a module's output is
 * a tty (stderr/stdout here) each bracketed part of the line prefix is
 * painted in its own color and the level tag gets a per-level color. The
 * file section writes plain text and prints it back, so the difference is
 * visible at a glance.
 *
 * Covered interfaces:
 *   - SHREWD_BASIC_LOG_MODULE_DEFINE / SHREWD_BASIC_LOG_MODULE_DECLARE
 *   - shrewd_basic_log / shrewd_basic_log_hex (inline level filtering)
 *   - shrewd_basic_log_write / shrewd_basic_log_hex_write (core writers)
 *   - shrewd_basic_log_module_register / _find / _create / _destroy
 *   - shrewd_basic_log_module_set_level / _set_stream / _set_file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shrewd-basic/log/log.h"

#include "log_demo_ext.h"

/* One module for the whole demo, shared with log_demo_ext.c. */
SHREWD_BASIC_LOG_MODULE_DEFINE(g_demo, "demo", SHREWD_BASIC_LOG_LEVEL_ALL);

#define DEMO_FILE "shrewd-basic-log-demo.txt"

static void banner(const char *title)
{
    printf("\n=== %s ===\n", title);
    fflush(stdout);
}

static const char *status_name(int status)
{
    switch (status) {
    case SHREWD_BASIC_ERR_OK:
        return "SHREWD_BASIC_ERR_OK";
    case SHREWD_BASIC_ERR_MEM_NOMEM:
        return "SHREWD_BASIC_ERR_MEM_NOMEM";
    case SHREWD_BASIC_ERR_ARG_INVALID:
        return "SHREWD_BASIC_ERR_ARG_INVALID";
    case SHREWD_BASIC_ERR_KEY_NOTFOUND:
        return "SHREWD_BASIC_ERR_KEY_NOTFOUND";
    case SHREWD_BASIC_ERR_KEY_EXISTS:
        return "SHREWD_BASIC_ERR_KEY_EXISTS";
    case SHREWD_BASIC_ERR_LOG_FOPEN:
        return "SHREWD_BASIC_ERR_LOG_FOPEN";
    default:
        return "unknown";
    }
}

static void print_status(const char *what, int status)
{
    printf("  %-42s -> %s (%d)\n", what, status_name(status), status);
    fflush(stdout);
}

int main(void)
{
    int status = SHREWD_BASIC_ERR_OK;

    banner("1. compile-time module: one line per level (colored on a tty)");
    printf("  parts: [time gray][level color][module blue][where magenta]"
           "[label yellow] message\n");
    fflush(stdout);
    status = shrewd_basic_log_module_register(g_demo);
    print_status("register(g_demo)  (optional)", status);
    shrewd_basic_log_module_t *found = NULL;
    status = shrewd_basic_log_module_find("demo", &found);
    print_status("find(\"demo\")", status);
    printf("  find() sees the DEFINE module: %s\n",
           (found == g_demo) ? "yes" : "no");
    fflush(stdout);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_FATAL, g_demo, "level", "FATAL message\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, g_demo, "level", "ERROR message\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, g_demo, "level", "WARN message\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, g_demo, "level", "INFO message\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, g_demo, "level", "DEBUG message\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, g_demo, "level", "TRACE message\n");

    banner("2. level mask filtering: only ERROR | WARN pass");
    status = shrewd_basic_log_module_set_level(g_demo,
                SHREWD_BASIC_LOG_LEVEL_ERROR | SHREWD_BASIC_LOG_LEVEL_WARN);
    print_status("set_level(ERROR | WARN)", status);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_FATAL, g_demo, "filter", "FATAL is masked out\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, g_demo, "filter", "ERROR passes\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, g_demo, "filter", "WARN passes\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, g_demo, "filter", "INFO is masked out\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_DEBUG, g_demo, "filter", "DEBUG is masked out\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE, g_demo, "filter", "TRACE is masked out\n");
    status = shrewd_basic_log_module_set_level(g_demo, SHREWD_BASIC_LOG_LEVEL_ALL);
    print_status("set_level(ALL)", status);

    banner("3. hex dump: colored prefix, plain msg and dump");
    unsigned char data[36];
    for (size_t i = 0; i < sizeof(data); ++i) {
        data[i] = (unsigned char)(i + 1);
    }
    shrewd_basic_log_hex(SHREWD_BASIC_LOG_LEVEL_INFO, g_demo, "hex",
                         "one row per 16 bytes", data, sizeof(data));

    banner("4. file output is plain text (no colors)");
    status = shrewd_basic_log_module_set_file(g_demo, DEMO_FILE);
    print_status("set_file(\"shrewd-basic-log-demo.txt\")", status);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, g_demo, "file",
                     "this line goes to %s\n", DEMO_FILE);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, g_demo, "file",
                     "plain text: files never get ANSI escapes\n");
    /* Replacing the stream closes the owned file. */
    shrewd_basic_log_module_set_stream(g_demo, stderr);
    printf("  file content read back:\n");
    {
        FILE *file = fopen(DEMO_FILE, "r");
        if (NULL == file) {
            printf("  (cannot reopen %s)\n", DEMO_FILE);
        } else {
            char line[256];
            while (NULL != fgets(line, sizeof(line), file)) {
                printf("  | %s", line);
            }
            fclose(file);
        }
    }
    fflush(stdout);

    banner("5. set_stream(): same logger, different stream");
    status = shrewd_basic_log_module_set_stream(g_demo, stdout);
    print_status("set_stream(stdout)", status);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN, g_demo, "stdout",
                     "this WARN went to stdout\n");
    status = shrewd_basic_log_module_set_stream(g_demo, stderr);
    print_status("set_stream(stderr)  (the default)", status);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, g_demo, "stderr",
                     "and this INFO is back on stderr\n");

    banner("6. core writers called directly (the macros wrap these)");
    status = shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_INFO, g_demo,
                                    "direct", __FILE__, __LINE__, __func__,
                                    "shrewd_basic_log_write() return value "
                                    "shown above\n");
    print_status("shrewd_basic_log_write(...)", status);
    status = shrewd_basic_log_hex_write(SHREWD_BASIC_LOG_LEVEL_DEBUG, g_demo,
                                        "direct", "shrewd_basic_log_hex_write()",
                                        data, 8, __FILE__, __LINE__, __func__);
    print_status("shrewd_basic_log_hex_write(...)", status);
    status = shrewd_basic_log_write(SHREWD_BASIC_LOG_LEVEL_INFO, NULL,
                                    "direct", __FILE__, __LINE__, __func__,
                                    "not logged\n");
    print_status("shrewd_basic_log_write(NULL module)", status);

    banner("7. runtime module: create / find / set_level / destroy");
    shrewd_basic_log_module_t *net = NULL;
    status = shrewd_basic_log_module_create("net", SHREWD_BASIC_LOG_LEVEL_ALL, &net);
    print_status("create(\"net\", ALL, &net)", status);
    found = NULL;
    status = shrewd_basic_log_module_find("net", &found);
    print_status("find(\"net\")", status);
    printf("  find() returned the created module: %s\n",
           (found == net) ? "yes" : "no");
    fflush(stdout);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, net, "net",
                     "runtime module logging to the default stream\n");
    status = shrewd_basic_log_module_set_level(net, SHREWD_BASIC_LOG_LEVEL_ERROR);
    print_status("set_level(net, ERROR)", status);
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, net, "net",
                     "masked out on the runtime module\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_ERROR, net, "net",
                     "ERROR passes on the runtime module\n");
    status = shrewd_basic_log_module_destroy(net);
    print_status("destroy(net)", status);
    found = NULL;
    status = shrewd_basic_log_module_find("net", &found);
    print_status("find(\"net\") after destroy", status);
    status = shrewd_basic_log_module_create(NULL, SHREWD_BASIC_LOG_LEVEL_ALL, &net);
    print_status("create(NULL, ...) rejects", status);
    status = shrewd_basic_log_module_set_file(g_demo, "/no/such/dir/log.txt");
    print_status("set_file(bad path) rejects", status);

    banner("8. SHREWD_BASIC_LOG_MODULE_DECLARE: shared across .c files");
    shrewd_demo_log_ext_run();

    printf("\nDemo done. The plain-text file is kept at ./%s for inspection.\n",
           DEMO_FILE);
    return EXIT_SUCCESS;
}

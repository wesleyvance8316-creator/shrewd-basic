#include "test_log_ext.h"

/* Import the module defined in test_log.c: same storage, same registry. */
SHREWD_BASIC_LOG_MODULE_DECLARE(g_cmod);

shrewd_basic_log_module_t *shrewd_test_log_ext_module(void) {
    return g_cmod;
}

void shrewd_test_log_ext_write(const char *msg) {
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO, g_cmod, "ext", "%s\n", msg);
}

#include "log_demo_ext.h"

#include "shrewd-basic/log/log.h"

/* Import the module defined in log_demo.c: same storage, same registry. */
SHREWD_BASIC_LOG_MODULE_DECLARE(g_demo);

void shrewd_demo_log_ext_run(void)
{
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_INFO,
                     g_demo,
                     "ext",
                     "logged from log_demo_ext.c through the shared module\n");
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                     g_demo,
                     "ext",
                     "same module name \"demo\", same output, same mask\n");
}

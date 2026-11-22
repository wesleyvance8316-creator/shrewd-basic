/**
 * @file main.c
 * @brief Minimal consumer: descriptors come from the generated stub
 *        (config_reflect.c), compiled at build time by reflect_gen.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/reflect/reflect.h"

#include "config.h"
#include "config_reflect.h"

int main(void) {
    demo_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "demo";
    cfg.level = DEMO_LEVEL_HIGH;
    cfg.origin.x = 3;
    cfg.origin.y = -4;
    cfg.flags[0] = 0xdeadbeefu;

    demo_config_t twin;
    memset(&twin, 0, sizeof(twin));
    twin.name = "demo";
    twin.level = DEMO_LEVEL_HIGH;
    twin.origin.x = 3;
    twin.origin.y = -4;
    twin.flags[0] = 0xdeadbeefu;

    bool same = false;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_reflect_equal(&cfg, &twin, &demo_config_t_reflect, &same)) {
        return 1;
    }
    if (!same) {
        printf("demo: equal FAILED\n");
        return 1;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_dump(&demo_config_t_reflect, &cfg, "cfg")) {
        return 1;
    }
    printf("demo: ok (generated descriptors work)\n");
    return 0;
}

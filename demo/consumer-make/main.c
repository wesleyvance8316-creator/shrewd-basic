/**
 * @file main.c
 * @brief Minimal consumer: descriptors come from the generated stub
 *        (config_reflect.c), compiled at build time by reflect_gen.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/reflect/reflect.h"

#include "shrewd-basic/formats/json.h"

#include "config.h"
#include "config_reflect.h"
#include "config_reflect_json.h"

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
    /* JSON policy table: encode honors the "label" rename. */
    {
        char *text = NULL;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_json_encode(
                &cfg, &demo_config_t_reflect, &demo_config_t_json, &text, NULL, NULL)) {
            return 1;
        }
        printf("json: %s\n", text);
        shrewd_basic_json_text_free(text);
    }
    printf("demo: ok (generated descriptors work)\n");
    return 0;
}

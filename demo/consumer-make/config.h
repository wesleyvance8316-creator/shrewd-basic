#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

/**
 * @file config.h
 * @brief Demo configuration record - the annotated header the
 *        generator turns into descriptors + a json policy table at
 *        build time. Annotations lead their comment block and may sit
 *        above the member (recommended), at the line end, or anywhere
 *        else - targets are fully qualified, position carries zero
 *        semantics.
 */

#include <stdint.h>

/* reflect: type=demo_level_t; */
typedef enum { DEMO_LEVEL_LOW = 0, DEMO_LEVEL_HIGH = 1 } demo_level_t;

/* reflect: type=demo_point_t; */
typedef struct {
    int32_t x;
    int32_t y;
} demo_point_t;

/* reflect: type=demo_config_t;
   json:    type=demo_config_t; */
typedef struct {
    /* reflect: member=demo_config_t.name, owned;
       json:    member=demo_config_t.name, key="label", required; */
    const char *name;
    /* json: member=demo_config_t.level, default=1; */
    demo_level_t level;
    demo_point_t origin;
    uint32_t flags[2];
} demo_config_t;

#endif /* DEMO_CONFIG_H */

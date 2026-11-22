#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

/**
 * @file config.h
 * @brief Demo configuration record - the annotated header the
 *        generator turns into descriptors at build time.
 */

#include <stdint.h>

/* reflect: type=demo_level_t */
typedef enum { DEMO_LEVEL_LOW = 0, DEMO_LEVEL_HIGH = 1 } demo_level_t;

/* reflect: type=demo_point_t */
typedef struct {
    int32_t x;
    int32_t y;
} demo_point_t;

/* reflect: type=demo_config_t */
typedef struct {
    const char *name; /* reflect: member=demo_config_t.name owned */
    demo_level_t level;
    demo_point_t origin;
    uint32_t flags[2];
} demo_config_t;

#endif /* DEMO_CONFIG_H */

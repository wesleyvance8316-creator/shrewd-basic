#ifndef REFLECT_GEN_SAMPLE_H
#define REFLECT_GEN_SAMPLE_H

/**
 * @file reflect_gen_sample.h
 * @brief Sample input for tools/reflect_gen.py (verification fixture).
 */

#include <stdint.h>

typedef enum {
    COLOR_RED = 0,
    COLOR_GREEN = 1,
    COLOR_BLUE = 2
} color_t;

typedef struct {
    int32_t x;
    int32_t y;
} geo_point_t;

typedef struct {
    const char *name;
    color_t color;
    geo_point_t origin;
    int32_t samples[4];
    char tag[8];
    geo_point_t *link;
    void *ctx;
    uint64_t count;
} shape_t;

#endif /* REFLECT_GEN_SAMPLE_H */

#ifndef REFLECT_GEN_SAMPLE_H
#define REFLECT_GEN_SAMPLE_H

/**
 * @file reflect_gen_sample.h
 * @brief Sample input for tools/reflect_gen.py (verification fixture).
 */

#include <stddef.h>
#include <stdint.h>

/* reflect: type=color_t; */
typedef enum { COLOR_RED = 0, COLOR_GREEN = 1, COLOR_BLUE = 2 } color_t;

/* reflect: type=geo_point_t; */
typedef struct {
    int32_t x;
    int32_t y;
} geo_point_t;

/* reflect: type=shape_t; */
typedef struct {
    const char *name;
    color_t color;
    geo_point_t origin;
    int32_t samples[4];
    char tag[8];
    int32_t grid[2][3];
    geo_point_t *link;
    void *ctx;
    uint64_t count;
} shape_t;

/* reflect: type=packet_t; */
typedef struct {
    size_t len;
    int32_t data[];
} packet_t;

#endif /* REFLECT_GEN_SAMPLE_H */

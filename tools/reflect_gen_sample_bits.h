#ifndef REFLECT_GEN_SAMPLE_BITS_H
#define REFLECT_GEN_SAMPLE_BITS_H

/**
 * @file reflect_gen_sample_bits.h
 * @brief C bit-field fixture for tools/reflect_gen.py (dwarf backend).
 */

#include <stdint.h>

/* Classic packed register layout: groups share one unsigned unit. */
/* reflect: type=flags_t; */
typedef struct {
    uint32_t id;
    unsigned ok : 1;
    unsigned level : 3;
    unsigned mode : 4;
    unsigned rest : 24;
} flags_t;

/* Mixed declared widths: the dwarf backend derives each unit window. */
/* reflect: type=mixed_t; */
typedef struct {
    unsigned a : 3;
    unsigned char b : 2;
    unsigned c : 10;
    uint16_t tag;
} mixed_t;

#endif /* REFLECT_GEN_SAMPLE_BITS_H */

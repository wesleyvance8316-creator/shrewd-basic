#ifndef REFLECT_GEN_SAMPLE_JSON_H
#define REFLECT_GEN_SAMPLE_JSON_H

/**
 * @file reflect_gen_sample_json.h
 * @brief Fixture for json policy generation: one row per directive,
 *        the exact macro vocabulary of docs/json.md section 6.
 */

#include <stdint.h>

extern const struct shrewd_basic_json_ops g_sample_ops;

/* reflect: type=box_t; */
typedef struct {
    int32_t w;
    int32_t h;
} box_t;

/* reflect: type=payload_tag_t; */
typedef enum { PAYLOAD_INT = 1, PAYLOAD_REAL = 2 } payload_tag_t;

/* reflect: type=payload_u_t; */
typedef union {
    /* reflect: member=payload_u_t.i, case=PAYLOAD_INT; */
    int32_t i;
    /* reflect: member=payload_u_t.f, case=PAYLOAD_REAL; */
    float f;
} payload_u_t;

/* reflect: type=cfg_t;
   json:    type=cfg_t; */
typedef struct {
    /* json: member=cfg_t.name, keys={"full_name", "nameID"}, required,
              default_str="anonymous"; */
    const char *name;
    /* json: member=cfg_t.level, key="lv", range=0..5, default=2; */
    int32_t level;
    /* json: member=cfg_t.score, omit_empty; */
    double score;
    /* json: member=cfg_t.secret, hidden; */
    uint32_t secret;
    /* json: member=cfg_t.hook, borrowed; */
    void *hook;
    /* json: member=cfg_t.memo, ops=g_sample_ops, format="uri",
              pattern="^/"; */
    const char *memo;
    /* json: member=cfg_t.box, default_json='{"w":3,"h":4}'; */
    box_t box;
} cfg_t;

/* reflect: type=event_t;
   json:    type=event_t; */
typedef struct {
    int32_t kind;
    /* reflect: member=event_t.payload, union_tag=kind;
       json:    member=event_t.payload, union_style=flat; */
    payload_u_t payload;
} event_t;

#endif /* REFLECT_GEN_SAMPLE_JSON_H */

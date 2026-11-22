#ifndef REFLECT_GEN_SAMPLE_ANN_H
#define REFLECT_GEN_SAMPLE_ANN_H

/**
 * @file reflect_gen_sample_ann.h
 * @brief Annotation fixture for tools/reflect_gen.py: owned strings,
 *        text buffers, explicit count pairing and tagged unions.
 */

#include <stddef.h>
#include <stdint.h>
#include <uchar.h>

/* reflect: type=doc_t; */
typedef struct {
    const char *title;        /* reflect: member=doc_t.title, owned; */
    char banner[16];          /* reflect: member=doc_t.banner, stringbuf; */
    const char16_t *subtitle; /* reflect: member=doc_t.subtitle, owned; */
    char16_t tagline[8];      /* reflect: member=doc_t.tagline, wstringbuf; */
    const wchar_t *author;    /* reflect: member=doc_t.author, owned; */
    uint64_t words;
    uint8_t body[]; /* reflect: member=doc_t.body, count=words; */
} doc_t;

/* Not annotated - stays hand-written (referenced as external below). */
typedef struct {
    int32_t x;
} helper_t;

/* reflect: type=stamp_t; */
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
} stamp_t;

/* reflect: type=payload_tag_t; */
typedef enum {
    PAYLOAD_INT = 1,
    PAYLOAD_REAL = 2,
    PAYLOAD_TEXT = 3,
    PAYLOAD_NAME = 4
} payload_tag_t;

/* reflect: type=payload_u_t; */
typedef union {
    int32_t i; /* reflect: member=payload_u_t.i, case=PAYLOAD_INT; */
    float f;   /* reflect: member=payload_u_t.f, case=PAYLOAD_REAL; */
    const char
        *text; /* reflect: member=payload_u_t.text, case={PAYLOAD_TEXT, PAYLOAD_NAME}, owned; */
} payload_u_t;

/* reflect: type=event_t; */
typedef struct {
    int32_t kind;        /* reflect: member=event_t.kind; */
    payload_u_t payload; /* reflect: member=event_t.payload, union_tag=kind; */
    stamp_t when;
} event_t;

#endif /* REFLECT_GEN_SAMPLE_ANN_H */

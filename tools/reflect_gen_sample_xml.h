#ifndef REFLECT_GEN_SAMPLE_XML_H
#define REFLECT_GEN_SAMPLE_XML_H

/**
 * @file reflect_gen_sample_xml.h
 * @brief Fixture for the xml: policy-table namespace and the csv:/ini:
 *        equivalence entrances onto the json policy tables.
 */

#include <stdint.h>

/* reflect: type=xml_child_t; */
typedef struct {
    int32_t w;
    int32_t h;
} xml_child_t;

/* reflect: type=cfg_t;
   xml:     type=cfg_t; */
typedef struct {
    /* xml: member=cfg_t.name, name="id", attr, required; */
    const char *name;
    /* xml: member=cfg_t.secret, hidden; */
    uint32_t secret;
    /* xml: member=cfg_t.size, attr; */
    int32_t size;
    /* xml: member=cfg_t.child; */
    xml_child_t child;
} cfg_t;

/* reflect: type=row_t;
   xml:     type=row_t, name=row_map;
   csv:     type=row_t;
   ini:     type=row_t; */
typedef struct {
    /* csv: member=row_t.a, key="A";
       ini: member=row_t.a, required; */
    int32_t a;
    /* xml: member=row_t.b, name="beta", required, hidden; */
    int32_t b;
} row_t;

#endif /* REFLECT_GEN_SAMPLE_XML_H */

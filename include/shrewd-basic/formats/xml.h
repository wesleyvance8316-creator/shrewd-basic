#ifndef SHREWD_BASIC_FORMATS_XML_H
#define SHREWD_BASIC_FORMATS_XML_H

/**
 * @file xml.h
 * @brief struct <-> XML (strict subset) over the shared text layer.
 *
 * Mapping: the root element is named after the type; leaves (scalars,
 * enums, char buffers, owned strings, wide strings) become either child
 * elements with text content or - via policy - attributes of their
 * parent; nested structs become child elements; fixed arrays become
 * repeated elements. Pointers map like the binary formats: NULL is
 * omitted, a target recurses. Bit groups, unions, flexible/dynamic/
 * sentinel arrays, BIT_INT and complex values are refused loudly
 * (SHREWD_BASIC_ERR_JSON_UNSUPPORTED).
 *
 * Policy rows (optional, NULL map = all defaults):
 *   name     the XML tag/attribute name (decode accepts member or name)
 *   as_attr  the leaf is written as an attribute of its parent
 *   REQUIRED the field must appear (missing -> JSON_TYPE_MISMATCH)
 *   HIDDEN   never encoded, never decoded
 * The map applies at every struct level of the tree, matched by member
 * name. Unknown elements/attributes are skipped on decode.
 *
 * Parser (strict subset, XXE-safe by construction): an optional
 * `<?xml ...?>` prolog and comments are ignored; elements, attributes
 * (either quote style), empty elements and CDATA are accepted; entity
 * references are the five predefined ones plus numeric character
 * references. DOCTYPE, ENTITY, foreign entity references and
 * processing instructions are rejected with a line number - there is
 * no DTD, so no external entity can ever be resolved.
 *
 * Ownership follows the family contract: decode zeroes the target
 * first and allocates string/pointer targets; release with
 * shrewd_basic_xml_free() (contents only - the root object stays the
 * caller's). Error codes reuse the JSON range (SYNTAX = malformed text,
 * TYPE_MISMATCH = shape, UNSUPPORTED = no XML form) plus REFLECT_*.
 */

#include <stddef.h>
#include <stdint.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/reflect/reflect.h"

/** Policy flags for shrewd_basic_xml_policy_t. */
#define SHREWD_BASIC_XML_REQUIRED ((uint32_t)1u) /**< Field must appear on decode. */
#define SHREWD_BASIC_XML_HIDDEN ((uint32_t)2u)   /**< Never encoded or decoded. */

/** Per-field mapping policy. */
typedef struct shrewd_basic_xml_policy {
    const char *field; /**< Member name. */
    const char *name;  /**< Tag/attribute name; NULL = member name. */
    int as_attr;       /**< 1: the leaf is an attribute of its parent. */
    uint32_t flags;    /**< SHREWD_BASIC_XML_REQUIRED / _HIDDEN. */
} shrewd_basic_xml_policy_t;

/** Fault detail: line of the input and a short description. */
typedef struct shrewd_basic_xml_error {
    size_t line;
    char message[128];
} shrewd_basic_xml_error_t;

/**
 * Encode @p object as XML text (root element named after @p type).
 * Release @p out_text with shrewd_basic_xml_text_free().
 */
int shrewd_basic_xml_encode(const void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_xml_policy_t *map,
                            size_t map_count,
                            char **out_text,
                            shrewd_basic_xml_error_t *error);

/**
 * Decode XML text into @p object (zeroed first). The root element name
 * must match @p type; trailing text after the root is an error.
 */
int shrewd_basic_xml_decode(const char *text,
                            size_t length,
                            void *object,
                            const shrewd_basic_reflect_type_t *type,
                            const shrewd_basic_xml_policy_t *map,
                            size_t map_count,
                            shrewd_basic_xml_error_t *error);

/** Release text produced by shrewd_basic_xml_encode(). */
void shrewd_basic_xml_text_free(char *text);

/** Release everything shrewd_basic_xml_decode() allocated inside @p object. */
int shrewd_basic_xml_free(void *object, const shrewd_basic_reflect_type_t *type);

#endif /* SHREWD_BASIC_FORMATS_XML_H */

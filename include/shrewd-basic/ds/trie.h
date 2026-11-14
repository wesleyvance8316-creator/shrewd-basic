#ifndef SHREWD_BASIC_DS_TRIE_H
#define SHREWD_BASIC_DS_TRIE_H

/**
 * @file trie.h
 * @brief Trie (prefix tree) mapping C-string keys to @c void* values.
 *
 * One node per key byte; children are kept sorted by byte value, so
 * shrewd_basic_trie_foreach() walks keys in lexicographic (byte) order -
 * which is also codepoint order for UTF-8 keys. Keys are unique: inserting
 * a key that is already stored fails with SHREWD_BASIC_ERR_KEY_EXISTS.
 * The empty string is a valid key (it lives on the root node).
 *
 * Values are never freed by the trie; ownership stays with the caller.
 * Every non-boolean interface returns a generic error code
 * (shrewd-basic/platform/error.h) and hands data back through out parameters;
 * an out parameter may be NULL to discard a removed value, but is required
 * where a value is only queried.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shrewd-basic/platform/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One child edge: a single key byte plus the child it leads to. */
typedef struct shrewd_basic_trie_link {
    unsigned char ch;
    struct shrewd_basic_trie_node *child;
    struct shrewd_basic_trie_link *next;
} shrewd_basic_trie_link_t;

/** Trie node: optional value plus children sorted by @c ch ascending. */
typedef struct shrewd_basic_trie_node {
    void *value;
    bool has_value;
    struct shrewd_basic_trie_link *children;
} shrewd_basic_trie_node_t;

/** Trie: root node, key count and longest key length (foreach buffer). */
typedef struct shrewd_basic_trie {
    shrewd_basic_trie_node_t *root;
    size_t size;
    size_t max_key_len;
} shrewd_basic_trie_t;

/**
 * Visitor callback for foreach: returns SHREWD_BASIC_ERR_OK to continue,
 * any other code stops the walk and is returned by foreach. @p key is only
 * valid for the duration of the call.
 */
typedef int (*shrewd_basic_trie_visit_fn)(const char *key, void *value, void *ctx);

/** Create an empty trie and store it into @p *out. */
int shrewd_basic_trie_create(shrewd_basic_trie_t **out);

/** Free all nodes (not the values). */
int shrewd_basic_trie_destroy(shrewd_basic_trie_t *trie);

/**
 * Insert @p value under @p key. Returns SHREWD_BASIC_ERR_KEY_EXISTS if the
 * key is already stored.
 */
int shrewd_basic_trie_insert(shrewd_basic_trie_t *trie, const char *key,
                             void *value);

/**
 * Store the value matching @p key into @p *out (required).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no key matches.
 */
int shrewd_basic_trie_search(const shrewd_basic_trie_t *trie, const char *key,
                             void **out);

/**
 * Remove @p key and store its value into @p *out (NULL discards).
 * Returns SHREWD_BASIC_ERR_KEY_NOTFOUND when no key matches.
 */
int shrewd_basic_trie_remove(shrewd_basic_trie_t *trie, const char *key,
                             void **out);

/** Visit all keys in lexicographic order until @p visit returns non-OK. */
int shrewd_basic_trie_foreach(const shrewd_basic_trie_t *trie,
                              shrewd_basic_trie_visit_fn visit, void *ctx);

/**
 * Visit every key starting with @p prefix, in lexicographic order; @p visit
 * receives the full key. A prefix matching no key simply visits nothing.
 * An empty prefix walks the whole trie.
 */
int shrewd_basic_trie_foreach_prefix(const shrewd_basic_trie_t *trie,
                                     const char *prefix,
                                     shrewd_basic_trie_visit_fn visit, void *ctx);

/** Store the key count into @p *out. */
int shrewd_basic_trie_size(const shrewd_basic_trie_t *trie, size_t *out);

/** True when the trie holds no keys. */
bool shrewd_basic_trie_empty(const shrewd_basic_trie_t *trie);

#ifdef __cplusplus
}
#endif

#endif /* SHREWD_BASIC_DS_TRIE_H */

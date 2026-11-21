#include "shrewd-basic/reflect/reflect.h"

#include <stdint.h>
#include <string.h>

#include "reflect-internal.h"
#include "shrewd-basic/log/log.h"
#include "shrewd-basic/platform/memory.h"

/* Shared reflect log module, defined in library/reflect/reflect.c. */
SHREWD_BASIC_LOG_MODULE_DECLARE(shrewd_basic_reflect_log);

/*
 * Hashing mirrors shrewd_basic_reflect_equal(): equal objects must hash
 * equal. Leaves fold through canonical encodings (integers by value,
 * floats with -0.0 folded and NaNs as a fixed marker, strings by
 * content, opaque targets by address) into a 64-bit FNV-1a stream that
 * is endianness- and width-independent.
 *
 * Shared and cyclic targets are folded by COLOR REFINEMENT instead of
 * by walk shape: every struct/array pointer target is a graph node, and
 * a fixed number of rounds refines each node's color by folding its
 * inline content with its edges contributing the previous round's
 * target colors. Bisimilar graphs - a self-loop versus a two-cycle
 * versus a longer ring, diamonds, shared tails - carry equal colors at
 * every round and therefore hash equal; the old "differently shaped
 * cycles hash differently" caveat is gone. Nodes are discovered up to
 * the depth cap (HASH_MAX_DEPTH, mirroring equal); edges beyond it
 * fold as a marker, so exotic structures differing only past the cap
 * may still collide - equal treats such regions as equal too.
 */
#define HASH_MAX_DEPTH 16 /* mirrors EQUAL_MAX_DEPTH in reflect_equal.c */
#define HASH_ROUNDS 16    /* fixed rounds: independent of graph size */
#define HASH_OFFSET UINT64_C(0xcbf29ce484222325)
#define HASH_PRIME UINT64_C(0x100000001b3)
#define HASH_CAPMARK UINT64_C(0x4841534843415000) /* "HASHCAP\0" */
#define HASH_EDGE0 UINT64_C(0x4841534845444730)   /* "HASHEDG0" */

static uint64_t hash_bytes(uint64_t h, const void *data, size_t nbytes)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < nbytes; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= HASH_PRIME;
    }
    return h;
}

/* Fold a 64-bit word in little-endian byte order (stable everywhere). */
static uint64_t hash_u64(uint64_t h, uint64_t word)
{
    unsigned char bytes[8] = {0};
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)((word >> (8 * i)) & UINT64_C(0xff));
    }
    return hash_bytes(h, bytes, sizeof(bytes));
}

typedef struct hash_node {
    const void *addr;
    const shrewd_basic_reflect_type_t *type;
    const shrewd_basic_reflect_ops_t *ops;
    uint64_t color;
    size_t depth; /* discovery depth (cap bookkeeping) */
    int walked;   /* discovery walked its content */
} hash_node_t;

typedef struct hash_graph {
    hash_node_t *nodes;
    size_t count;
    size_t cap;
    int nomem;
} hash_graph_t;

typedef struct hash_ctx {
    hash_graph_t *graph;
    int discover; /* 1 = register nodes; 0 = fold with current colors */
} hash_ctx_t;

static hash_node_t *
hash_graph_find(hash_graph_t *graph, const void *addr, const shrewd_basic_reflect_type_t *type)
{
    for (size_t i = 0; i < graph->count; ++i) {
        if ((graph->nodes[i].addr == addr) && (graph->nodes[i].type == type)) {
            return &graph->nodes[i];
        }
    }
    return NULL;
}

static hash_node_t *hash_graph_add(hash_graph_t *graph,
                                   const void *addr,
                                   const shrewd_basic_reflect_type_t *type,
                                   const shrewd_basic_reflect_ops_t *ops,
                                   size_t depth)
{
    hash_node_t *node = hash_graph_find(graph, addr, type);
    if (NULL != node) {
        return node;
    }
    if (graph->count == graph->cap) {
        size_t cap = (0 == graph->cap) ? 16 : (graph->cap * 2);
        void *block = NULL;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_calloc(&block, 1, cap * sizeof(hash_node_t))) {
            graph->nomem = 1;
            return NULL;
        }
        if (NULL != graph->nodes) {
            memcpy(block, graph->nodes, graph->count * sizeof(hash_node_t));
        }
        {
            void *old = graph->nodes;
            (void)shrewd_basic_platform_free(&old);
        }
        graph->nodes = block;
        graph->cap = cap;
    }
    node = &graph->nodes[graph->count];
    ++graph->count;
    node->addr = addr;
    node->type = type;
    node->ops = ops;
    node->color = HASH_OFFSET;
    node->depth = depth;
    node->walked = 0;
    return node;
}

static int hash_content_fields(hash_ctx_t *ctx,
                               const shrewd_basic_reflect_field_t *fields,
                               size_t count,
                               const unsigned char *value,
                               size_t depth,
                               uint64_t *h);

static int hash_content_value(hash_ctx_t *ctx,
                              const shrewd_basic_reflect_type_t *type,
                              const unsigned char *value,
                              size_t nbytes,
                              const shrewd_basic_reflect_ops_t *ops,
                              size_t depth,
                              uint64_t *h);

/* Fold one pointer edge: a node edge contributes a color (or registers
   and expands the node during discovery); leaf targets fold as content. */
static int hash_edge(hash_ctx_t *ctx,
                     const void *target,
                     const shrewd_basic_reflect_type_t *type,
                     const shrewd_basic_reflect_ops_t *ops,
                     size_t depth,
                     uint64_t *h)
{
    int node_kind = (SHREWD_BASIC_REFLECT_KIND_STRUCT == type->sb_refl_kind)
                    || (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind);
    if (0 == node_kind) {
        /* Scalar/string/opaque targets compare by value (or address for
           opaque); they are content, not identity nodes. */
        return hash_content_value(
            ctx, type, (const unsigned char *)target, type->sb_refl_size, ops, depth, h);
    }
    if (HASH_MAX_DEPTH <= depth) {
        *h = hash_u64(*h, HASH_CAPMARK);
        return SHREWD_BASIC_ERR_OK;
    }
    if (0 != ctx->discover) {
        hash_node_t *node = hash_graph_add(ctx->graph, target, type, ops, depth);
        if (NULL == node) {
            return SHREWD_BASIC_ERR_OK; /* nomem flagged; caller reports */
        }
        if (0 == node->walked) {
            node->walked = 1;
            return hash_content_value(
                ctx, type, (const unsigned char *)target, type->sb_refl_size, ops, depth, h);
        }
        return SHREWD_BASIC_ERR_OK;
    }
    {
        hash_node_t *node = hash_graph_find(ctx->graph, target, type);
        *h = hash_u64(*h, (NULL != node) ? node->color : HASH_CAPMARK);
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Fold one value's inline content (the leaves and inline structure;
   pointer edges dispatch through hash_edge). */
static int hash_content_value(hash_ctx_t *ctx,
                              const shrewd_basic_reflect_type_t *type,
                              const unsigned char *value,
                              size_t nbytes,
                              const shrewd_basic_reflect_ops_t *ops,
                              size_t depth,
                              uint64_t *h)
{
    if (HASH_MAX_DEPTH <= depth) {
        /* Same cutoff as reflect_equal(): past the cap nothing is read. */
        *h = hash_u64(*h, HASH_CAPMARK);
        return SHREWD_BASIC_ERR_OK;
    }
    if ((NULL != ops) && (NULL != ops->hash)) {
        size_t custom = 0;
        int status = ops->hash(value, &custom);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        *h = hash_u64(*h, (uint64_t)custom);
        return SHREWD_BASIC_ERR_OK;
    }
    switch (type->sb_refl_kind) {
    case SHREWD_BASIC_REFLECT_KIND_BOOL:
        *h = hash_u64(*h, (0 != reflect_load_u64(value, nbytes)) ? 1u : 0u);
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_INT8:
    case SHREWD_BASIC_REFLECT_KIND_INT16:
    case SHREWD_BASIC_REFLECT_KIND_INT32:
    case SHREWD_BASIC_REFLECT_KIND_INT64:
    case SHREWD_BASIC_REFLECT_KIND_ENUM:
        *h = hash_u64(*h, (uint64_t)reflect_load_i64(value, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_UINT8:
    case SHREWD_BASIC_REFLECT_KIND_UINT16:
    case SHREWD_BASIC_REFLECT_KIND_UINT32:
    case SHREWD_BASIC_REFLECT_KIND_UINT64:
        *h = hash_u64(*h, reflect_load_u64(value, nbytes));
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_CHAR:
        /* equal() compares char fields byte-wise. */
        *h = hash_bytes(*h, value, 1);
        return SHREWD_BASIC_ERR_OK;
    case SHREWD_BASIC_REFLECT_KIND_FLOAT:
    case SHREWD_BASIC_REFLECT_KIND_DOUBLE: {
        double number = reflect_load_f64(value, nbytes);
        uint64_t bits = 0;
        if (number != number) {
            bits = UINT64_C(0x7ff8000000000000); /* NaN marker */
        } else {
            if (0.0 == number) {
                number = 0.0; /* fold -0.0 into +0.0 (they compare equal) */
            }
            memcpy(&bits, &number, sizeof(bits));
        }
        *h = hash_u64(*h, bits);
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_STRING: {
        const char *text = reflect_load_str(value);
        if (NULL == text) {
            *h = hash_u64(*h, 0);
        } else {
            *h = hash_u64(*h, 1);
            *h = hash_bytes(*h, text, strlen(text));
        }
        return SHREWD_BASIC_ERR_OK;
    }
    case SHREWD_BASIC_REFLECT_KIND_OPAQUE:
        /* Without ops equal() is address identity. */
        *h = hash_u64(*h, (uint64_t)(uintptr_t)value);
        return SHREWD_BASIC_ERR_OK;
    default:
        break;
    }
    if (SHREWD_BASIC_REFLECT_KIND_ARRAY == type->sb_refl_kind) {
        for (size_t i = 0; i < type->sb_refl_elem_count; ++i) {
            int status = hash_content_value(ctx,
                                            type->sb_refl_elem,
                                            value + (i * type->sb_refl_elem_stride),
                                            type->sb_refl_elem_stride,
                                            type->sb_refl_elem->sb_refl_ops,
                                            depth + 1,
                                            h);
            if (SHREWD_BASIC_ERR_OK != status) {
                return status;
            }
        }
        return SHREWD_BASIC_ERR_OK;
    }
    if (SHREWD_BASIC_REFLECT_KIND_STRUCT != type->sb_refl_kind) {
        *h = hash_bytes(*h, value, nbytes);
        return SHREWD_BASIC_ERR_OK;
    }
    return hash_content_fields(
        ctx, type->sb_refl_fields, type->sb_refl_field_count, value, depth, h);
}

/* Fold one field list - a struct's fields or an active union variant's. */
static int hash_content_fields(hash_ctx_t *ctx,
                               const shrewd_basic_reflect_field_t *fields,
                               size_t count,
                               const unsigned char *value,
                               size_t depth,
                               uint64_t *h)
{
    for (size_t i = 0; i < count; ++i) {
        const shrewd_basic_reflect_field_t *field = &fields[i];
        const shrewd_basic_reflect_ops_t *field_ops = reflect_field_ops(field);
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_UNION)) {
            /* Tagged union: fold the tag, then the active variant. */
            const unsigned char *storage = value + field->sb_refl_offset;
            int64_t tag
                = reflect_load_i64(value + field->sb_refl_limit_offset, field->sb_refl_limit_size);
            const shrewd_basic_reflect_variant_t *variant = reflect_union_variant(field, storage);
            *h = hash_u64(*h, (uint64_t)tag);
            if (NULL == variant) {
                *h = hash_bytes(*h, storage, field->sb_refl_stride);
            } else {
                int status = hash_content_fields(ctx,
                                                 variant->sb_refl_fields,
                                                 variant->sb_refl_field_count,
                                                 storage,
                                                 depth + 1,
                                                 h);
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_STRINGBUF)) {
            /* Text buffers fold by content up to the first NUL. */
            const char *text = (const char *)(value + field->sb_refl_offset);
            size_t len = reflect_strn_len(text, field->sb_refl_count);
            *h = hash_u64(*h, 1);
            *h = hash_bytes(*h, text, len);
            continue;
        }
        if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_BITS)) {
            /* Bit groups fold their extracted bits. */
            uint64_t unit = reflect_load_u64(value + field->sb_refl_offset, field->sb_refl_size);
            *h = hash_u64(
                *h,
                reflect_bits_extract(unit, field->sb_refl_bit_offset, field->sb_refl_bit_width));
            continue;
        }
        {
            reflect_array_view_t view = reflect_array_view(value, field);
            *h = hash_u64(*h, (uint64_t)view.count);
            for (size_t j = 0; j < view.count; ++j) {
                const unsigned char *elem = view.base + (j * view.stride);
                int status = 0;
                if (0u != (field->sb_refl_flags & SHREWD_BASIC_REFLECT_FIELD_F_PTR)) {
                    void *pointer = reflect_load_ptr(elem);
                    if (NULL == pointer) {
                        *h = hash_u64(*h, 0);
                        continue;
                    }
                    *h = hash_u64(*h, 1);
                    status = hash_edge(ctx, pointer, field->sb_refl_type, field_ops, depth + 1, h);
                } else {
                    status = hash_content_value(
                        ctx, field->sb_refl_type, elem, view.stride, field_ops, depth + 1, h);
                }
                if (SHREWD_BASIC_ERR_OK != status) {
                    return status;
                }
            }
        }
    }
    return SHREWD_BASIC_ERR_OK;
}

int shrewd_basic_reflect_hash(const void *object,
                              const shrewd_basic_reflect_type_t *type,
                              size_t *out_hash)
{
    hash_graph_t graph = {0};
    hash_ctx_t ctx = {0};
    uint64_t h = HASH_OFFSET;
    uint64_t *next = NULL;
    int status = SHREWD_BASIC_ERR_OK;
    if ((NULL == object) || (NULL == type) || (NULL == out_hash)) {
        shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_WARN,
                         shrewd_basic_reflect_log,
                         "hash",
                         "hash: object, type or out_hash is NULL\n");
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    memset(&graph, 0, sizeof(graph));
    ctx.graph = &graph;
    ctx.discover = 1;

    /* Phase one: discover every struct/array pointer target (and the
       root) as a graph node, each expanded once. */
    {
        hash_node_t *root = hash_graph_add(&graph, object, type, type->sb_refl_ops, 0);
        if (NULL == root) {
            return SHREWD_BASIC_ERR_MEM_NOMEM;
        }
        root->walked = 1;
        status = hash_content_value(&ctx,
                                    type,
                                    (const unsigned char *)object,
                                    type->sb_refl_size,
                                    type->sb_refl_ops,
                                    0,
                                    &h);
    }

    /* Phase two: refine colors for a fixed number of rounds; each round
       folds every node's content with edges contributing the previous
       round's colors (simultaneous update). */
    if ((SHREWD_BASIC_ERR_OK == status) && (0 == graph.nomem)) {
        status = shrewd_basic_platform_calloc((void **)&next, graph.count, sizeof(uint64_t));
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        ctx.discover = 0;
        for (size_t round = 0; round < HASH_ROUNDS; ++round) {
            for (size_t i = 0; i < graph.count; ++i) {
                hash_node_t *node = &graph.nodes[i];
                uint64_t color = HASH_OFFSET;
                status = hash_content_value(&ctx,
                                            node->type,
                                            (const unsigned char *)node->addr,
                                            node->type->sb_refl_size,
                                            node->ops,
                                            node->depth,
                                            &color);
                if (SHREWD_BASIC_ERR_OK != status) {
                    break;
                }
                next[i] = color;
            }
            if (SHREWD_BASIC_ERR_OK != status) {
                break;
            }
            for (size_t i = 0; i < graph.count; ++i) {
                graph.nodes[i].color = next[i];
            }
        }
    }
    if (SHREWD_BASIC_ERR_OK == status) {
        if (0 != graph.nomem) {
            status = SHREWD_BASIC_ERR_MEM_NOMEM;
        } else {
            uint64_t final = graph.nodes[0].color;
#if SIZE_MAX < UINT64_MAX
            *out_hash = (size_t)(final ^ (final >> 32));
#else
            *out_hash = (size_t) final;
#endif
        }
    }
    {
        void *old = next;
        (void)shrewd_basic_platform_free(&old);
    }
    {
        void *old = graph.nodes;
        (void)shrewd_basic_platform_free(&old);
    }
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    shrewd_basic_log(SHREWD_BASIC_LOG_LEVEL_TRACE,
                     shrewd_basic_reflect_log,
                     "hash",
                     "hash ok (%s, 0x%zx)\n",
                     type->sb_refl_name,
                     *out_hash);
    return SHREWD_BASIC_ERR_OK;
}

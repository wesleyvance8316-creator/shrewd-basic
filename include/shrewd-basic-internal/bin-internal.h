#ifndef SHREWD_BASIC_INTERNAL_BIN_INTERNAL_H
#define SHREWD_BASIC_INTERNAL_BIN_INTERNAL_H

/*
 * Shared binary wire scaffolding for the binary format modules
 * (msgpack, cbor): a growable big-endian writer and a bounds-checked
 * reader. Format-specific encodings (tag bytes, integer forms) stay in
 * each module - only the byte movement is common.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/platform/memory.h"

/* --- writer (grows by doubling from 256) -------------------------------- */

typedef struct binio_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
} binio_buf_t;

static inline int binio_reserve(binio_buf_t *b, size_t extra) {
    size_t want = 0;
    void *block = NULL;
    if (extra > (SIZE_MAX - b->len)) {
        return SHREWD_BASIC_ERR_BIN_UNSUPPORTED;
    }
    want = b->len + extra;
    if (want <= b->cap) {
        return SHREWD_BASIC_ERR_OK;
    }
    while (b->cap < want) {
        b->cap = (0 == b->cap) ? (size_t)256 : (b->cap * 2);
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_realloc(&block, b->data, b->cap)) {
        return SHREWD_BASIC_ERR_MEM_NOMEM;
    }
    b->data = block;
    return SHREWD_BASIC_ERR_OK;
}

static inline int binio_w_u8(binio_buf_t *b, uint8_t byte) {
    int status = binio_reserve(b, 1);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    b->data[b->len] = byte;
    b->len += 1;
    return SHREWD_BASIC_ERR_OK;
}

static inline int binio_w_bytes(binio_buf_t *b, const void *bytes, size_t n) {
    int status = binio_reserve(b, n);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    if (0 < n) {
        memcpy(b->data + b->len, bytes, n);
        b->len += n;
    }
    return SHREWD_BASIC_ERR_OK;
}

/* Big-endian multi-byte number (wire order of both format specs). */
static inline int binio_w_be(binio_buf_t *b, uint64_t value, size_t width) {
    int status = binio_reserve(b, width);
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    for (size_t i = 0; i < width; ++i) {
        b->data[b->len + i] = (uint8_t)((value >> (8u * (width - 1 - i))) & 0xffu);
    }
    b->len += width;
    return SHREWD_BASIC_ERR_OK;
}

static inline void binio_buf_free(binio_buf_t *b) {
    if (NULL != b->data) {
        void *old = b->data;
        (void)shrewd_basic_platform_free(&old);
    }
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* --- reader (bounds-checked cursor) ------------------------------------- */

typedef struct binio_rd {
    const unsigned char *data;
    size_t len;
    size_t pos;
} binio_rd_t;

static inline void binio_rd_init(binio_rd_t *r, const void *data, size_t len) {
    r->data = (const unsigned char *)data;
    r->len = len;
    r->pos = 0;
}

static inline size_t binio_r_left(const binio_rd_t *r) {
    return r->len - r->pos;
}

static inline int binio_r_u8(binio_rd_t *r, uint8_t *out) {
    if (binio_r_left(r) < 1u) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    *out = r->data[r->pos];
    r->pos += 1;
    return SHREWD_BASIC_ERR_OK;
}

static inline int binio_r_be(binio_rd_t *r, size_t width, uint64_t *out) {
    uint64_t value = 0;
    if (binio_r_left(r) < width) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    for (size_t i = 0; i < width; ++i) {
        value = (value << 8) | (uint64_t)r->data[r->pos + i];
    }
    r->pos += width;
    *out = value;
    return SHREWD_BASIC_ERR_OK;
}

static inline int binio_r_bytes(binio_rd_t *r, size_t n, const unsigned char **out) {
    if (binio_r_left(r) < n) {
        return SHREWD_BASIC_ERR_BIN_FORMAT;
    }
    *out = r->data + r->pos;
    r->pos += n;
    return SHREWD_BASIC_ERR_OK;
}

#endif /* SHREWD_BASIC_INTERNAL_BIN_INTERNAL_H */

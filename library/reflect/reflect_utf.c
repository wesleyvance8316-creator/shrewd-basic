/**
 * @file reflect_utf.c
 * @brief Strict UTF-8 <-> UTF-16/32 conversions for wide strings.
 *
 * Shared by the format layers (json maps wide strings to UTF-8 text,
 * bin keeps code units on the wire). Strict both ways: invalid UTF-8,
 * overlong forms, truncated sequences, lone UTF-16 surrogates and
 * out-of-range code points fail with SHREWD_BASIC_ERR_REFLECT_ENCODING.
 * Conversion never writes a terminator and never replaces characters;
 * callers measure first (dst NULL) and convert into an exact buffer.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "shrewd-basic-internal/reflect-internal.h"

/* Emit one code point as UTF-8 at *pos (counted even past dst_cap). */
static void utf8_emit(uint32_t cp, char *dst, size_t dst_cap, size_t *pos) {
    unsigned char u[4] = {0, 0, 0, 0};
    size_t n = 0;
    if (cp < 0x80u) {
        u[0] = (unsigned char)cp;
        n = 1;
    } else if (cp < 0x800u) {
        u[0] = (unsigned char)(0xC0u | (cp >> 6));
        u[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp < 0x10000u) {
        u[0] = (unsigned char)(0xE0u | (cp >> 12));
        u[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else {
        u[0] = (unsigned char)(0xF0u | (cp >> 18));
        u[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        u[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 4;
    }
    if (NULL != dst) {
        for (size_t i = 0; i < n; ++i) {
            if ((*pos + i) < dst_cap) {
                dst[*pos + i] = (char)u[i];
            }
        }
    }
    *pos += n;
}

/* Strict UTF-8 decode of one sequence; advances *i past it. */
static int utf8_next(const unsigned char *s, size_t len, size_t *i, uint32_t *out_cp) {
    size_t n = 0;
    uint32_t cp = 0;
    uint32_t floor = 0;
    if (*i >= len) {
        return SHREWD_BASIC_ERR_REFLECT_ENCODING;
    }
    cp = (uint32_t)s[*i];
    if (cp < 0x80u) {
        *i += 1;
        *out_cp = cp;
        return SHREWD_BASIC_ERR_OK;
    }
    if ((0xC2u <= cp) && (cp <= 0xDFu)) {
        n = 2;
        cp = cp & 0x1Fu;
        floor = 0x80u;
    } else if ((0xE0u <= cp) && (cp <= 0xEFu)) {
        n = 3;
        cp = cp & 0x0Fu;
        floor = 0x800u;
    } else if ((0xF0u <= cp) && (cp <= 0xF4u)) {
        n = 4;
        cp = cp & 0x07u;
        floor = 0x10000u;
    } else {
        return SHREWD_BASIC_ERR_REFLECT_ENCODING; /* C0/C1/F5+ or stray byte */
    }
    if ((*i + n) > len) {
        return SHREWD_BASIC_ERR_REFLECT_ENCODING; /* truncated */
    }
    for (size_t k = 1; k < n; ++k) {
        if (0x80u != (s[*i + k] & 0xC0u)) {
            return SHREWD_BASIC_ERR_REFLECT_ENCODING;
        }
        cp = (cp << 6) | ((uint32_t)s[*i + k] & 0x3Fu);
    }
    if ((cp < floor) || (cp > 0x10FFFFu) || ((0xD800u <= cp) && (cp <= 0xDFFFu))) {
        return SHREWD_BASIC_ERR_REFLECT_ENCODING; /* overlong, out of range, surrogate */
    }
    *i += n;
    *out_cp = cp;
    return SHREWD_BASIC_ERR_OK;
}

/* Load one code unit (native order) from a wide string. */
static uint32_t unit_load(const unsigned char *at, size_t unit) {
    if (2 == unit) {
        uint16_t value = 0;
        memcpy(&value, at, sizeof(value));
        return (uint32_t)value;
    }
    {
        uint32_t value = 0;
        memcpy(&value, at, sizeof(value));
        return value;
    }
}

/* Store one code unit (native order). */
static void unit_store(unsigned char *at, size_t unit, uint32_t value) {
    if (2 == unit) {
        uint16_t narrowed = (uint16_t)value;
        memcpy(at, &narrowed, sizeof(narrowed));
    } else {
        memcpy(at, &value, sizeof(value));
    }
}

int reflect_utf8_from_units(
    const void *src, size_t unit, size_t max_units, char *dst, size_t dst_cap, size_t *out_len) {
    const unsigned char *text = (const unsigned char *)src;
    size_t count = 0;
    size_t pos = 0;
    size_t i = 0;
    if ((NULL == src) || ((2 != unit) && (4 != unit)) || (NULL == out_len)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    count = reflect_wstrn_len(src, unit, max_units);
    while (i < count) {
        uint32_t cp = unit_load(text + (i * unit), unit);
        if (2 == unit) {
            if ((0xD800u <= cp) && (cp <= 0xDBFFu)) {
                uint32_t low = 0;
                if ((i + 1) >= count) {
                    return SHREWD_BASIC_ERR_REFLECT_ENCODING; /* lone high surrogate */
                }
                low = unit_load(text + ((i + 1) * unit), unit);
                if (!((0xDC00u <= low) && (low <= 0xDFFFu))) {
                    return SHREWD_BASIC_ERR_REFLECT_ENCODING;
                }
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                i += 2;
            } else if ((0xDC00u <= cp) && (cp <= 0xDFFFu)) {
                return SHREWD_BASIC_ERR_REFLECT_ENCODING; /* lone low surrogate */
            } else {
                i += 1;
            }
        } else {
            if ((cp > 0x10FFFFu) || ((0xD800u <= cp) && (cp <= 0xDFFFu))) {
                return SHREWD_BASIC_ERR_REFLECT_ENCODING;
            }
            i += 1;
        }
        utf8_emit(cp, dst, dst_cap, &pos);
    }
    if ((NULL != dst) && (pos > dst_cap)) {
        return SHREWD_BASIC_ERR_REFLECT_ENCODING; /* caller sized it wrong */
    }
    *out_len = pos;
    return SHREWD_BASIC_ERR_OK;
}

int reflect_units_from_utf8(const char *src,
                            size_t src_len,
                            size_t unit,
                            void *dst,
                            size_t dst_cap_units,
                            size_t *out_units) {
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *out = (unsigned char *)dst;
    size_t i = 0;
    size_t pos = 0;
    if ((NULL == src) || ((2 != unit) && (4 != unit)) || (NULL == out_units)) {
        return SHREWD_BASIC_ERR_ARG_INVALID;
    }
    while (i < src_len) {
        uint32_t cp = 0;
        int status = utf8_next(s, src_len, &i, &cp);
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
        }
        if (NULL != out) {
            if (pos >= dst_cap_units) {
                return SHREWD_BASIC_ERR_REFLECT_ENCODING; /* caller sized it wrong */
            }
        }
        if ((2 == unit) && (cp >= 0x10000u)) {
            /* Surrogate pair synthesis. */
            uint32_t rest = cp - 0x10000u;
            uint32_t hi = 0xD800u + (rest >> 10);
            uint32_t lo = 0xDC00u + (rest & 0x3FFu);
            if (NULL != out) {
                if ((pos + 1) >= dst_cap_units) {
                    return SHREWD_BASIC_ERR_REFLECT_ENCODING;
                }
                unit_store(out + (pos * unit), unit, hi);
                unit_store(out + ((pos + 1) * unit), unit, lo);
            }
            pos += 2;
        } else {
            if (NULL != out) {
                unit_store(out + (pos * unit), unit, cp);
            }
            pos += 1;
        }
    }
    *out_units = pos;
    return SHREWD_BASIC_ERR_OK;
}

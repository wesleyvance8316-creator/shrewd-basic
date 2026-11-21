/**
 * @file bin_demo.c
 * @brief Walkthrough of the bin format: struct mapping to bytes and back.
 *
 * Three sections: (1) encode a reflect-described struct and show the
 * wire bytes, (2) decode them back and release with bin_free, (3) size
 * a flexible-tail root with bin_value_size and round-trip it.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shrewd-basic/formats/bin.h"
#include "shrewd-basic/platform/error.h"
#include "shrewd-basic/platform/memory.h"

typedef struct {
    int32_t x;
    int32_t y;
} point_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(point_reflect,
                                 point_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, x),
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(point_t, y));

typedef struct {
    int32_t id;
    const char *name;
    point_t home;
} user_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(user_reflect,
                                 user_t,
                                 SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(user_t, id),
                                 SHREWD_BASIC_REFLECT_FIELD_STRING_OWNED(user_t, name),
                                 SHREWD_BASIC_REFLECT_FIELD_STRUCT(user_t, home, point_reflect));

typedef struct {
    int32_t len;
    int32_t data[];
} series_t;

SHREWD_BASIC_REFLECT_TYPE_DEFINE(
    series_reflect,
    series_t,
    SHREWD_BASIC_REFLECT_FIELD_PRIMITIVE(series_t, len),
    SHREWD_BASIC_REFLECT_FIELD_FLEX(series_t, data, len, shrewd_basic_reflect_int32));

static void hexdump(const unsigned char *bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        printf("%02x%s", bytes[i], (0 == ((i + 1) % 16)) ? "\n" : " ");
    }
    if (0 != (size % 16)) {
        printf("\n");
    }
}

int main(void) {
    void *bytes = NULL;
    size_t size = 0;
    user_t src;
    user_t dst;
    bool same = false;

    printf("=== 1. encode: struct -> wire bytes ===\n");
    memset(&src, 0, sizeof(src));
    src.id = 42;
    src.name = "ada";
    src.home.x = 3;
    src.home.y = 4;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_bin_encode(&src, &user_reflect, NULL, &bytes, &size)) {
        return 1;
    }
    printf("user_t -> %zu bytes:\n  ", size);
    hexdump((const unsigned char *)bytes, size);

    printf("\n=== 2. decode: wire bytes -> struct ===\n");
    memset(&dst, 0, sizeof(dst));
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_bin_decode(bytes, size, &dst, &user_reflect, NULL)) {
        return 1;
    }
    shrewd_basic_bin_bytes_free(bytes);
    printf("id=%d name=\"%s\" home=(%d,%d) name copy at %p (src %p)\n",
           dst.id,
           dst.name,
           dst.home.x,
           dst.home.y,
           (const void *)dst.name,
           (const void *)src.name);
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_equal(&src, &dst, &user_reflect, &same)) {
        return 1;
    }
    printf("round trip equal: %s\n", (0 != same) ? "yes" : "no");
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_bin_free(&dst, &user_reflect)) {
        return 1;
    }

    printf("\n=== 3. flexible tail: value_size then decode ===\n");
    {
        void *block = NULL;
        void *wire = NULL;
        size_t wire_size = 0;
        size_t need = 0;
        series_t *series = NULL;
        series_t *copy = NULL;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_calloc(&block, 1, sizeof(series_t) + (3 * sizeof(int32_t)))) {
            return 1;
        }
        series = (series_t *)block;
        series->len = 3;
        series->data[0] = 10;
        series->data[1] = 20;
        series->data[2] = 30;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_bin_encode(series, &series_reflect, NULL, &wire, &wire_size)) {
            return 1;
        }
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_bin_value_size(wire, wire_size, &series_reflect, &need)) {
            return 1;
        }
        printf("series_t wire %zu bytes, root needs %zu bytes\n", wire_size, need);
        block = NULL;
        if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block, 1, need)) {
            return 1;
        }
        copy = (series_t *)block;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_bin_decode(wire, wire_size, copy, &series_reflect, NULL)) {
            return 1;
        }
        shrewd_basic_bin_bytes_free(wire);
        printf("len=%d data=[%d,%d,%d]\n", copy->len, copy->data[0], copy->data[1], copy->data[2]);
        (void)shrewd_basic_bin_free(copy, &series_reflect);
        (void)shrewd_basic_bin_free(series, &series_reflect);
        {
            void *old = copy;
            (void)shrewd_basic_platform_free(&old);
        }
        {
            void *old = series;
            (void)shrewd_basic_platform_free(&old);
        }
    }

    printf("\nDemo done. See docs/reflect-apps.md for the format spec.\n");
    return 0;
}

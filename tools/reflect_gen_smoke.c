/**
 * @file reflect_gen_smoke.c
 * @brief Smoke program for the generated descriptors (reflect_gen_check.sh).
 *
 * Drives the generated stubs through dump, equal and enum lookup so the
 * generator output is proven to compile and behave like hand-written
 * annotations. Prints one summary line and exits non-zero on failure.
 */

#include <stdio.h>
#include <string.h>

#include "shrewd-basic/platform/memory.h"

#include "generated.h"
#include "reflect_gen_sample.h"

int main(void) {
    shape_t s = {0};
    memset(&s, 0, sizeof(s));
    s.name = "shape";
    s.color = COLOR_GREEN;
    s.origin.x = 1;
    s.origin.y = 2;
    s.samples[0] = 7;
    s.tag[0] = 'o';
    s.tag[1] = 'k';
    s.count = 3;
    (void)shrewd_basic_reflect_dump(&shape_t_reflect, &s, "shape");

    shape_t t = {0};
    memset(&t, 0, sizeof(t));
    t.name = "shape";
    t.color = COLOR_GREEN;
    t.origin.x = 1;
    t.origin.y = 2;
    t.samples[0] = 7;
    t.tag[0] = 'o';
    t.tag[1] = 'k';
    t.count = 3;
    bool same = false;
    if ((SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_equal(&s, &t, &shape_t_reflect, &same))
        || (!same)) {
        printf("reflect_gen_smoke: equal FAILED\n");
        return 1;
    }
    const char *name = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_enum_find_name(&color_t_reflect, 1, &name)) {
        printf("reflect_gen_smoke: enum FAILED\n");
        return 1;
    }
    /* Flexible tail: allocated as header + tail, compared deeply. */
    void *block_a = NULL;
    void *block_b = NULL;
    if ((SHREWD_BASIC_ERR_OK
         != shrewd_basic_platform_calloc(&block_a, 1, sizeof(packet_t) + (2 * sizeof(int32_t))))
        || (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_calloc(
                &block_b, 1, sizeof(packet_t) + (2 * sizeof(int32_t))))) {
        printf("reflect_gen_smoke: flex setup FAILED\n");
        return 1;
    }
    packet_t *pa = block_a;
    packet_t *pb = block_b;
    pa->len = 2;
    pa->data[0] = 4;
    pa->data[1] = 5;
    pb->len = 2;
    pb->data[0] = 4;
    pb->data[1] = 5;
    if ((SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_equal(pa, pb, &packet_t_reflect, &same))
        || (!same)) {
        printf("reflect_gen_smoke: flex equal FAILED\n");
        return 1;
    }
    (void)shrewd_basic_reflect_destroy(pa, &packet_t_reflect);
    (void)shrewd_basic_reflect_destroy(pb, &packet_t_reflect);

    printf("reflect_gen_smoke: ok (color 1 = %s)\n", name);
    return 0;
}

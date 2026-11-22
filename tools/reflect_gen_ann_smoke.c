/**
 * @file reflect_gen_ann_smoke.c
 * @brief Smoke program for annotation-driven generation
 *        (reflect_gen_check.sh).
 *
 * Drives the generated stubs for owned wide strings, text buffers and
 * tagged unions: content semantics per code unit, union tag switching,
 * and the count= pairing. Prints one summary line and exits non-zero on
 * failure.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>

#include "shrewd-basic/platform/memory.h"

#include "generated.h"
#include "reflect_gen_sample_ann.h"

static int failures = 0;

static void expect(const char *name, bool ok) {
    if (!ok) {
        printf("reflect_gen_ann_smoke: FAILED %s\n", name);
        ++failures;
    }
}

int main(void) {
    /* Wide strings: content to the NUL unit, buffers too. FAM tails are
       allocated with the header (sizeof covers only the fixed part). */
    void *block_a = NULL;
    void *block_b = NULL;
    if ((SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block_a, 1, sizeof(doc_t) + 2))
        || (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&block_b, 1, sizeof(doc_t) + 2))) {
        expect("doc alloc", false);
        return 1;
    }
    doc_t *doc = block_a;
    doc_t *twin = block_b;
    doc->title = "hello";
    doc->subtitle = (const char16_t *)u"caf\x00e9";
    doc->author = L"ada";
    doc->banner[0] = 'b';
    doc->banner[1] = 'y';
    doc->tagline[0] = 'o';
    doc->tagline[1] = 0x3042;
    doc->words = 2;
    doc->body[0] = 7;
    doc->body[1] = 9;

    twin->title = "hello";
    twin->subtitle = (const char16_t *)u"caf\x00e9";
    twin->author = L"ada";
    memcpy(twin->banner, doc->banner, sizeof(twin->banner));
    twin->tagline[0] = 'o';
    twin->tagline[1] = 0x3042;
    twin->words = 2;
    twin->body[0] = 7;
    twin->body[1] = 9;
    bool same = false;
    expect("doc equal",
           (SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_equal(doc, twin, &doc_t_reflect, &same))
               && same);

    /* Owned wide string: copy clones content, distinct pointer. */
    doc_t *clone = NULL;
    expect("doc copy",
           SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_copy((void **)&clone, doc, &doc_t_reflect));
    expect("doc clone wide",
           (NULL != clone) && (NULL != clone->subtitle) && (clone->subtitle != doc->subtitle)
               && (0 == memcmp(clone->subtitle, doc->subtitle, 5 * sizeof(char16_t))));
    same = false;
    expect("doc clone equal",
           (SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_equal(doc, clone, &doc_t_reflect, &same))
               && same);
    expect("doc destroy",
           SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_destroy(clone, &doc_t_reflect));
    expect("doc free", SHREWD_BASIC_ERR_OK == shrewd_basic_platform_free(&block_a));
    expect("doc free twin", SHREWD_BASIC_ERR_OK == shrewd_basic_platform_free(&block_b));

    /* Tagged union: the active variant drives equal. */
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = PAYLOAD_TEXT;
    ev.payload.text = "hi";
    event_t other;
    memset(&other, 0, sizeof(other));
    other.kind = PAYLOAD_TEXT;
    other.payload.text = "hi";
    same = false;
    expect("union equal",
           (SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_equal(&ev, &other, &event_t_reflect, &same))
               && same);
    other.payload.text = "ho";
    same = true;
    expect("union differ",
           (SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_equal(&ev, &other, &event_t_reflect, &same))
               && (!same));
    /* A variant switch is a difference even with identical raw bytes. */
    memset(&other, 0, sizeof(other));
    other.kind = PAYLOAD_INT;
    memcpy(&other.payload, &ev.payload, sizeof(other.payload));
    same = true;
    expect("union tag differ",
           (SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_equal(&ev, &other, &event_t_reflect, &same))
               && (!same));

    printf("reflect_gen_ann_smoke: %s\n", (0 == failures) ? "ok" : "FAILED");
    return (0 == failures) ? 0 : 1;
}

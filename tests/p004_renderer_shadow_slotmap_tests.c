/**
 * @file p004_renderer_shadow_slotmap_tests.c
 * @brief Unit tests for the shadow texture-array layer allocator (no GL).
 */
#include <stdio.h>

#include "ferrum/renderer/resource/shadow_slotmap.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* Sequential allocations hand out non-overlapping contiguous runs. */
static int test_alloc_sequential(void) {
    uint8_t backing[8];
    shadow_slotmap_t m;
    shadow_slotmap_init(&m, backing, 8);
    ASSERT_TRUE(shadow_slotmap_used(&m) == 0);

    ASSERT_TRUE(shadow_slotmap_alloc(&m, 2) == 0);  /* [0,2) */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 3) == 2);  /* [2,5) */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 1) == 5);  /* [5,6) */
    ASSERT_TRUE(shadow_slotmap_used(&m) == 6);
    return 0;
}

/* Freeing returns the run to the pool; the next fit reuses it (first-fit). */
static int test_free_reuse(void) {
    uint8_t backing[8];
    shadow_slotmap_t m;
    shadow_slotmap_init(&m, backing, 8);
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 4) == 0);  /* [0,4) */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 4) == 4);  /* [4,8) full */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 1) == -1); /* full */

    shadow_slotmap_free(&m, 0, 4);
    ASSERT_TRUE(shadow_slotmap_used(&m) == 4);
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 4) == 0);  /* reuses the freed run. */
    ASSERT_TRUE(shadow_slotmap_used(&m) == 8);
    return 0;
}

/* First-fit fills a hole; a run larger than any hole fails. */
static int test_fragmentation(void) {
    uint8_t backing[6];
    shadow_slotmap_t m;
    shadow_slotmap_init(&m, backing, 6);
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 2) == 0);  /* [0,2) */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 2) == 2);  /* [2,4) */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 2) == 4);  /* [4,6) */
    shadow_slotmap_free(&m, 2, 2);                  /* hole at [2,4) */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 3) == -1); /* no 3-run exists. */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 2) == 2);  /* first-fit into the hole. */
    return 0;
}

/* Zero-count, over-capacity, and out-of-range are handled safely. */
static int test_edges(void) {
    uint8_t backing[4];
    shadow_slotmap_t m;
    shadow_slotmap_init(&m, backing, 4);
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 0) == -1);     /* zero count invalid. */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 5) == -1);     /* larger than capacity. */
    ASSERT_TRUE(shadow_slotmap_used(&m) == 0);          /* unchanged on failure. */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 4) == 0);      /* exactly capacity. */
    ASSERT_TRUE(shadow_slotmap_alloc(&m, 1) == -1);
    shadow_slotmap_free(&m, 2, 10);                     /* over-range: clamped. */
    ASSERT_TRUE(shadow_slotmap_used(&m) == 2);          /* freed [2,4) only. */
    return 0;
}

/* NULL args are safe. */
static int test_null_safe(void) {
    uint8_t backing[4];
    shadow_slotmap_t m;
    shadow_slotmap_init(NULL, backing, 4);              /* no crash. */
    shadow_slotmap_init(&m, NULL, 4);
    ASSERT_TRUE(shadow_slotmap_alloc(NULL, 1) == -1);
    ASSERT_TRUE(shadow_slotmap_used(NULL) == 0);
    shadow_slotmap_free(NULL, 0, 1);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "alloc_sequential", test_alloc_sequential },
    { "free_reuse", test_free_reuse },
    { "fragmentation", test_fragmentation },
    { "edges", test_edges },
    { "null_safe", test_null_safe },
};

int main(void) {
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}

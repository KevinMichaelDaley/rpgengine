/**
 * @file p106_video_capture_frame_ring_tests.c
 * @brief Unit tests for the SPSC frame ring buffer.
 *
 * Tests the CPU-side lock-free ring in isolation (no GL needed).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/renderer/video_capture/frame_ring.h"

#define ASSERT_TRUE(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL: %s:%d: %s\n",                       \
                    __FILE__, __LINE__, #cond);                         \
            return 1;                                                   \
        }                                                               \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                         \
    do {                                                                \
        if ((exp) != (act)) {                                           \
            fprintf(stderr, "FAIL: %s:%d: expected %d got %d\n",       \
                    __FILE__, __LINE__, (int)(exp), (int)(act));        \
            return 1;                                                   \
        }                                                               \
    } while (0)

/* ── Happy path ─────────────────────────────────────────────────── */

/** Push one frame and pop it back. */
static int test_push_pop_one(void) {
    fr_frame_ring_t ring;
    uint32_t frame_bytes = 16;
    fr_frame_ring_init(&ring, frame_bytes);

    uint8_t data[16];
    memset(data, 0xAB, sizeof(data));

    int ok = fr_frame_ring_push(&ring, data, frame_bytes);
    ASSERT_INT_EQ(1, ok);
    ASSERT_INT_EQ(1, (int)fr_frame_ring_count(&ring));

    uint32_t out_bytes = 0;
    const uint8_t *pixels = fr_frame_ring_pop(&ring, &out_bytes);
    ASSERT_TRUE(pixels != NULL);
    ASSERT_INT_EQ((int)frame_bytes, (int)out_bytes);
    ASSERT_INT_EQ(0xAB, pixels[0]);
    ASSERT_INT_EQ(0xAB, pixels[15]);

    ASSERT_INT_EQ(0, (int)fr_frame_ring_count(&ring));

    fr_frame_ring_destroy(&ring);
    return 0;
}

/** Fill ring to capacity and pop all. */
static int test_fill_and_drain(void) {
    fr_frame_ring_t ring;
    uint32_t frame_bytes = 8;
    fr_frame_ring_init(&ring, frame_bytes);

    for (int i = 0; i < FR_FRAME_RING_CAPACITY; i++) {
        uint8_t data[8];
        memset(data, (uint8_t)i, sizeof(data));
        int ok = fr_frame_ring_push(&ring, data, frame_bytes);
        ASSERT_INT_EQ(1, ok);
    }
    ASSERT_INT_EQ(FR_FRAME_RING_CAPACITY, (int)fr_frame_ring_count(&ring));

    for (int i = 0; i < FR_FRAME_RING_CAPACITY; i++) {
        uint32_t nb = 0;
        const uint8_t *p = fr_frame_ring_pop(&ring, &nb);
        ASSERT_TRUE(p != NULL);
        ASSERT_INT_EQ((int)frame_bytes, (int)nb);
        ASSERT_INT_EQ(i, p[0]);
    }
    ASSERT_INT_EQ(0, (int)fr_frame_ring_count(&ring));

    fr_frame_ring_destroy(&ring);
    return 0;
}

/* ── Edge cases ─────────────────────────────────────────────────── */

/** Pop from empty ring returns NULL. */
static int test_pop_empty(void) {
    fr_frame_ring_t ring;
    fr_frame_ring_init(&ring, 4);

    uint32_t nb = 99;
    const uint8_t *p = fr_frame_ring_pop(&ring, &nb);
    ASSERT_TRUE(p == NULL);

    fr_frame_ring_destroy(&ring);
    return 0;
}

/** Overflow drops oldest frame. */
static int test_overflow_drops_oldest(void) {
    fr_frame_ring_t ring;
    uint32_t frame_bytes = 4;
    fr_frame_ring_init(&ring, frame_bytes);

    /* Fill to capacity. */
    for (int i = 0; i < FR_FRAME_RING_CAPACITY; i++) {
        uint8_t data[4] = { (uint8_t)i, 0, 0, 0 };
        fr_frame_ring_push(&ring, data, frame_bytes);
    }

    /* Push one more — should drop frame 0. */
    uint8_t extra[4] = { 0xFF, 0, 0, 0 };
    int ok = fr_frame_ring_push(&ring, extra, frame_bytes);
    ASSERT_INT_EQ(0, ok); /* 0 = dropped a frame */

    /* Count should still be capacity. */
    ASSERT_INT_EQ(FR_FRAME_RING_CAPACITY, (int)fr_frame_ring_count(&ring));

    /* First pop should be frame 1 (frame 0 was dropped). */
    uint32_t nb = 0;
    const uint8_t *p = fr_frame_ring_pop(&ring, &nb);
    ASSERT_TRUE(p != NULL);
    ASSERT_INT_EQ(1, p[0]);

    fr_frame_ring_destroy(&ring);
    return 0;
}

/** Wrap-around: push/pop many more than capacity. */
static int test_wraparound(void) {
    fr_frame_ring_t ring;
    uint32_t frame_bytes = 4;
    fr_frame_ring_init(&ring, frame_bytes);

    for (int round = 0; round < 100; round++) {
        uint8_t data[4] = { (uint8_t)(round & 0xFF), 0, 0, 0 };
        int ok = fr_frame_ring_push(&ring, data, frame_bytes);
        ASSERT_INT_EQ(1, ok);

        uint32_t nb = 0;
        const uint8_t *p = fr_frame_ring_pop(&ring, &nb);
        ASSERT_TRUE(p != NULL);
        ASSERT_INT_EQ((uint8_t)(round & 0xFF), p[0]);
    }

    ASSERT_INT_EQ(0, (int)fr_frame_ring_count(&ring));
    fr_frame_ring_destroy(&ring);
    return 0;
}

/* ── Failure modes ──────────────────────────────────────────────── */

/** NULL ring is safe. */
static int test_null_safety(void) {
    ASSERT_INT_EQ(0, (int)fr_frame_ring_count(NULL));
    ASSERT_TRUE(fr_frame_ring_pop(NULL, NULL) == NULL);
    fr_frame_ring_destroy(NULL); /* Should not crash. */
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };

static struct test_case TESTS[] = {
    { "push_pop_one",          test_push_pop_one },
    { "fill_and_drain",        test_fill_and_drain },
    { "pop_empty",             test_pop_empty },
    { "overflow_drops_oldest", test_overflow_drops_oldest },
    { "wraparound",            test_wraparound },
    { "null_safety",           test_null_safety },
};

int main(void) {
    int n = (int)(sizeof(TESTS) / sizeof(TESTS[0]));
    int passed = 0;
    for (int i = 0; i < n; i++) {
        printf("  RUN  %s\n", TESTS[i].name);
        int result = TESTS[i].fn();
        if (result == 0) {
            printf("  OK   %s\n", TESTS[i].name);
            passed++;
        } else {
            printf("  FAIL %s\n", TESTS[i].name);
            return 1;
        }
    }
    printf("%d/%d tests passed\n", passed, n);
    return 0;
}

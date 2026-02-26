/**
 * @file edit_cmd_ring_tests.c
 * @brief Unit tests for the lock-free SPSC command ring.
 *
 * Tests cover: basic push/pop, full ring, empty ring, wrap-around,
 * payload integrity, and concurrent producer/consumer from two threads.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ferrum/editor/edit_cmd_ring.h"

/* ----------------------------------------------------------------------- */
/* Test harness macros                                                      */
/* ----------------------------------------------------------------------- */

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n",            \
                    #expr, __FILE__, __LINE__);                               \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_INT_EQ(a, b)                                                  \
    do {                                                                     \
        int _a = (int)(a), _b = (int)(b);                                    \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_INT_EQ failed: %d != %d (%s:%d)\n",    \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_UINT_EQ(a, b)                                                 \
    do {                                                                     \
        unsigned _a = (unsigned)(a), _b = (unsigned)(b);                     \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_UINT_EQ failed: %u != %u (%s:%d)\n",   \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                  \
    do {                                                                     \
        if (strcmp((a), (b)) != 0) {                                         \
            fprintf(stderr, "  ASSERT_STR_EQ failed: \"%s\" != \"%s\""       \
                    " (%s:%d)\n", (a), (b), __FILE__, __LINE__);             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Basic tests                                                               */
/* ----------------------------------------------------------------------- */

static int test_init_destroy(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 16, 256));
    ASSERT_TRUE(edit_cmd_ring_empty(&ring));
    ASSERT_UINT_EQ(edit_cmd_ring_count(&ring), 0);
    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_push_pop_single(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 16, 256));

    const char *msg = "hello";
    ASSERT_TRUE(edit_cmd_ring_push(&ring, 42, msg, 5));
    ASSERT_UINT_EQ(edit_cmd_ring_count(&ring), 1);

    char buf[256];
    edit_cmd_slot_t slot;
    ASSERT_TRUE(edit_cmd_ring_pop(&ring, &slot, buf, sizeof(buf)));
    ASSERT_UINT_EQ(slot.id, 42);
    ASSERT_UINT_EQ(slot.payload_len, 5);
    ASSERT_TRUE(memcmp(buf, "hello", 5) == 0);

    ASSERT_TRUE(edit_cmd_ring_empty(&ring));
    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_pop_empty(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 16, 256));

    char buf[64];
    edit_cmd_slot_t slot;
    ASSERT_FALSE(edit_cmd_ring_pop(&ring, &slot, buf, sizeof(buf)));

    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_push_multiple(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 16, 256));

    for (uint32_t i = 0; i < 10; ++i) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "msg_%u", i);
        ASSERT_TRUE(edit_cmd_ring_push(&ring, i, buf, (uint32_t)len));
    }
    ASSERT_UINT_EQ(edit_cmd_ring_count(&ring), 10);

    for (uint32_t i = 0; i < 10; ++i) {
        char pbuf[256];
        edit_cmd_slot_t slot;
        ASSERT_TRUE(edit_cmd_ring_pop(&ring, &slot, pbuf, sizeof(pbuf)));
        ASSERT_UINT_EQ(slot.id, i);
        char expected[32];
        int elen = snprintf(expected, sizeof(expected), "msg_%u", i);
        ASSERT_UINT_EQ(slot.payload_len, (uint32_t)elen);
        ASSERT_TRUE(memcmp(pbuf, expected, (size_t)elen) == 0);
    }

    ASSERT_TRUE(edit_cmd_ring_empty(&ring));
    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_full_ring_rejects_push(void) {
    edit_cmd_ring_t ring;
    /* Capacity 4 (will be rounded to 4 since it's a power of 2). */
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 4, 64));

    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(edit_cmd_ring_push(&ring, i, "x", 1));
    }
    ASSERT_UINT_EQ(edit_cmd_ring_count(&ring), 4);

    /* Ring is full — push should fail. */
    ASSERT_FALSE(edit_cmd_ring_push(&ring, 99, "y", 1));

    /* Pop one to make room. */
    char buf[64];
    edit_cmd_slot_t slot;
    ASSERT_TRUE(edit_cmd_ring_pop(&ring, &slot, buf, sizeof(buf)));
    ASSERT_UINT_EQ(slot.id, 0);

    /* Now push should succeed. */
    ASSERT_TRUE(edit_cmd_ring_push(&ring, 99, "y", 1));

    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_wrap_around(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 4, 64));

    /* Fill and drain multiple times to exercise wrap-around. */
    for (int round = 0; round < 10; ++round) {
        for (uint32_t i = 0; i < 4; ++i) {
            ASSERT_TRUE(edit_cmd_ring_push(&ring, i + (uint32_t)(round * 100),
                                            "data", 4));
        }
        for (uint32_t i = 0; i < 4; ++i) {
            char buf[64];
            edit_cmd_slot_t slot;
            ASSERT_TRUE(edit_cmd_ring_pop(&ring, &slot, buf, sizeof(buf)));
            ASSERT_UINT_EQ(slot.id, i + (uint32_t)(round * 100));
        }
        ASSERT_TRUE(edit_cmd_ring_empty(&ring));
    }

    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_payload_too_large(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 4, 8));

    /* Payload of 16 bytes exceeds max_payload of 8. */
    ASSERT_FALSE(edit_cmd_ring_push(&ring, 1, "0123456789abcdef", 16));

    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_zero_length_payload(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 4, 64));

    ASSERT_TRUE(edit_cmd_ring_push(&ring, 7, NULL, 0));

    char buf[64];
    edit_cmd_slot_t slot;
    ASSERT_TRUE(edit_cmd_ring_pop(&ring, &slot, buf, sizeof(buf)));
    ASSERT_UINT_EQ(slot.id, 7);
    ASSERT_UINT_EQ(slot.payload_len, 0);

    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_capacity_rounds_to_power_of_two(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 5, 64));
    /* 5 rounds up to 8. We should be able to push 8 items. */
    for (uint32_t i = 0; i < 8; ++i) {
        ASSERT_TRUE(edit_cmd_ring_push(&ring, i, "x", 1));
    }
    ASSERT_FALSE(edit_cmd_ring_push(&ring, 99, "x", 1)); /* Full at 8. */

    edit_cmd_ring_destroy(&ring);
    return 0;
}

static int test_peek_advance(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 16, 256));

    /* Peek on empty returns NULL. */
    ASSERT_TRUE(edit_cmd_ring_peek(&ring) == NULL);

    ASSERT_TRUE(edit_cmd_ring_push(&ring, 10, "alpha", 5));
    ASSERT_TRUE(edit_cmd_ring_push(&ring, 20, "beta", 4));

    /* Peek returns first item without consuming. */
    const edit_cmd_slot_t *s = edit_cmd_ring_peek(&ring);
    ASSERT_TRUE(s != NULL);
    ASSERT_UINT_EQ(s->id, 10);
    ASSERT_UINT_EQ(s->payload_len, 5);
    ASSERT_TRUE(memcmp(s->payload, "alpha", 5) == 0);

    /* Peek again returns the same item. */
    const edit_cmd_slot_t *s2 = edit_cmd_ring_peek(&ring);
    ASSERT_TRUE(s2 == s);

    /* Advance consumes it. */
    edit_cmd_ring_advance(&ring);
    ASSERT_UINT_EQ(edit_cmd_ring_count(&ring), 1);

    /* Next peek returns second item. */
    s = edit_cmd_ring_peek(&ring);
    ASSERT_TRUE(s != NULL);
    ASSERT_UINT_EQ(s->id, 20);
    ASSERT_TRUE(memcmp(s->payload, "beta", 4) == 0);
    edit_cmd_ring_advance(&ring);

    ASSERT_TRUE(edit_cmd_ring_empty(&ring));
    edit_cmd_ring_destroy(&ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Concurrent test (two pthreads)                                            */
/* ----------------------------------------------------------------------- */

#define CONCURRENT_COUNT 100000

typedef struct thread_args {
    edit_cmd_ring_t *ring;
    int result; /* 0 = success, 1 = failure */
} thread_args_t;

static void *producer_thread_(void *arg) {
    thread_args_t *ta = (thread_args_t *)arg;
    for (uint32_t i = 0; i < CONCURRENT_COUNT; ++i) {
        /* Spin until push succeeds (ring not full). */
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%u", i);
        while (!edit_cmd_ring_push(ta->ring, i, buf, (uint32_t)len)) {
            /* Busy wait — acceptable for test. */
        }
    }
    ta->result = 0;
    return NULL;
}

static void *consumer_thread_(void *arg) {
    thread_args_t *ta = (thread_args_t *)arg;
    uint32_t expected = 0;
    while (expected < CONCURRENT_COUNT) {
        const edit_cmd_slot_t *slot = edit_cmd_ring_peek(ta->ring);
        if (slot) {
            if (slot->id != expected) {
                fprintf(stderr, "  concurrent: expected id %u, got %u\n",
                        expected, slot->id);
                ta->result = 1;
                return NULL;
            }
            /* Verify payload matches id (safe: slot is valid until advance). */
            char exp_buf[32];
            int elen = snprintf(exp_buf, sizeof(exp_buf), "%u", expected);
            if (slot->payload_len != (uint32_t)elen ||
                memcmp(slot->payload, exp_buf, (size_t)elen) != 0) {
                fprintf(stderr, "  concurrent: payload mismatch at id %u\n",
                        expected);
                ta->result = 1;
                return NULL;
            }
            edit_cmd_ring_advance(ta->ring);
            expected++;
        }
    }
    ta->result = 0;
    return NULL;
}

static int test_concurrent_push_pop(void) {
    edit_cmd_ring_t ring;
    ASSERT_TRUE(edit_cmd_ring_init(&ring, 256, 64));

    thread_args_t prod_args = {.ring = &ring, .result = -1};
    thread_args_t cons_args = {.ring = &ring, .result = -1};

    pthread_t prod_tid, cons_tid;
    pthread_create(&prod_tid, NULL, producer_thread_, &prod_args);
    pthread_create(&cons_tid, NULL, consumer_thread_, &cons_args);

    pthread_join(prod_tid, NULL);
    pthread_join(cons_tid, NULL);

    ASSERT_INT_EQ(prod_args.result, 0);
    ASSERT_INT_EQ(cons_args.result, 0);
    ASSERT_TRUE(edit_cmd_ring_empty(&ring));

    edit_cmd_ring_destroy(&ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test runner                                                              */
/* ----------------------------------------------------------------------- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"init_destroy",               test_init_destroy},
    {"push_pop_single",            test_push_pop_single},
    {"pop_empty",                  test_pop_empty},
    {"push_multiple",              test_push_multiple},
    {"full_ring_rejects_push",     test_full_ring_rejects_push},
    {"wrap_around",                test_wrap_around},
    {"payload_too_large",          test_payload_too_large},
    {"zero_length_payload",        test_zero_length_payload},
    {"capacity_rounds_to_pow2",    test_capacity_rounds_to_power_of_two},
    {"peek_advance",               test_peek_advance},
    {"concurrent_push_pop",        test_concurrent_push_pop},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}

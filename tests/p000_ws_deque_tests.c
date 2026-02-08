#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>

#include "ferrum/job/ws_deque.h"

#define TEST_FAIL(msg, ...)                                                                         \
    do {                                                                                            \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);               \
        return 1;                                                                                   \
    } while (0)

#define ASSERT_TRUE(cond)                                                                           \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            TEST_FAIL("%s", #cond);                                                                 \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_PTR(expected, actual)                                                             \
    do {                                                                                            \
        const void *_exp = (expected);                                                              \
        const void *_act = (actual);                                                                \
        if (_exp != _act) {                                                                         \
            TEST_FAIL("expected %p got %p", _exp, _act);                                           \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_U32(expected, actual)                                                             \
    do {                                                                                            \
        uint32_t _exp = (uint32_t)(expected);                                                       \
        uint32_t _act = (uint32_t)(actual);                                                         \
        if (_exp != _act) {                                                                         \
            TEST_FAIL("expected %u got %u", _exp, _act);                                           \
        }                                                                                           \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int test_owner_push_pop_is_lifo(void) {
    fr_ws_deque_t dq;
    ASSERT_TRUE(fr_ws_deque_init(&dq, 64) == 0);

    int values[32];
    for (int i = 0; i < (int)ARRAY_SIZE(values); ++i) {
        values[i] = i;
        ASSERT_TRUE(fr_ws_deque_push(&dq, &values[i]) == 0);
    }

    for (int i = (int)ARRAY_SIZE(values) - 1; i >= 0; --i) {
        void *p = fr_ws_deque_pop(&dq);
        ASSERT_EQ_PTR(&values[i], p);
    }

    ASSERT_TRUE(fr_ws_deque_pop(&dq) == NULL);
    fr_ws_deque_destroy(&dq);
    return 0;
}

static int test_steal_is_fifo_from_top(void) {
    fr_ws_deque_t dq;
    ASSERT_TRUE(fr_ws_deque_init(&dq, 8) == 0);

    int a = 1, b = 2, c = 3, d = 4;
    ASSERT_TRUE(fr_ws_deque_push(&dq, &a) == 0);
    ASSERT_TRUE(fr_ws_deque_push(&dq, &b) == 0);
    ASSERT_TRUE(fr_ws_deque_push(&dq, &c) == 0);
    ASSERT_TRUE(fr_ws_deque_push(&dq, &d) == 0);

    ASSERT_EQ_PTR(&a, fr_ws_deque_steal(&dq));
    ASSERT_EQ_PTR(&b, fr_ws_deque_steal(&dq));

    /* Remaining should be popped LIFO by owner. */
    ASSERT_EQ_PTR(&d, fr_ws_deque_pop(&dq));
    ASSERT_EQ_PTR(&c, fr_ws_deque_pop(&dq));
    ASSERT_TRUE(fr_ws_deque_pop(&dq) == NULL);

    fr_ws_deque_destroy(&dq);
    return 0;
}

static int test_push_fails_when_full(void) {
    fr_ws_deque_t dq;
    ASSERT_TRUE(fr_ws_deque_init(&dq, 2) == 0);

    int a = 1, b = 2, c = 3;
    ASSERT_TRUE(fr_ws_deque_push(&dq, &a) == 0);
    ASSERT_TRUE(fr_ws_deque_push(&dq, &b) == 0);
    ASSERT_TRUE(fr_ws_deque_push(&dq, &c) != 0);

    (void)fr_ws_deque_pop(&dq);
    ASSERT_TRUE(fr_ws_deque_push(&dq, &c) == 0);

    fr_ws_deque_destroy(&dq);
    return 0;
}

struct steal_ctx {
    fr_ws_deque_t *dq;
    int *items;
    atomic_uchar *seen;
    uint32_t n;
    atomic_uint *stolen;
};

static void *thief_main(void *arg) {
    struct steal_ctx *ctx = (struct steal_ctx *)arg;
    while (atomic_load_explicit(ctx->stolen, memory_order_relaxed) < ctx->n) {
        void *p = fr_ws_deque_steal(ctx->dq);
        if (!p) {
            sched_yield();
            continue;
        }

        int *ip = (int *)p;
        ptrdiff_t idx = ip - ctx->items;
        if (idx < 0 || (uint32_t)idx >= ctx->n) {
            return (void *)(intptr_t)1;
        }

        unsigned char prev = atomic_exchange_explicit(&ctx->seen[idx], 1, memory_order_relaxed);
        if (prev != 0) {
            return (void *)(intptr_t)2; /* duplicate */
        }

        atomic_fetch_add_explicit(ctx->stolen, 1, memory_order_relaxed);
    }

    return NULL;
}

static int test_concurrent_steal_removes_all_exactly_once(void) {
    fr_ws_deque_t dq;
    const uint32_t n = 20000;
    ASSERT_TRUE(fr_ws_deque_init(&dq, 1u << 15) == 0);

    int *items = (int *)malloc(sizeof(int) * (size_t)n);
    atomic_uchar *seen = (atomic_uchar *)calloc((size_t)n, sizeof(atomic_uchar));
    ASSERT_TRUE(items != NULL);
    ASSERT_TRUE(seen != NULL);

    for (uint32_t i = 0; i < n; ++i) {
        items[i] = (int)i;
        ASSERT_TRUE(fr_ws_deque_push(&dq, &items[i]) == 0);
    }

    atomic_uint stolen;
    atomic_init(&stolen, 0);

    struct steal_ctx ctx = {
        .dq = &dq,
        .items = items,
        .seen = seen,
        .n = n,
        .stolen = &stolen,
    };

    pthread_t thieves[4];
    for (size_t i = 0; i < ARRAY_SIZE(thieves); ++i) {
        ASSERT_TRUE(pthread_create(&thieves[i], NULL, thief_main, &ctx) == 0);
    }

    for (size_t i = 0; i < ARRAY_SIZE(thieves); ++i) {
        int rc = 0;
        ASSERT_TRUE(pthread_join(thieves[i], NULL) == 0);
        ASSERT_EQ_U32(0, (uint32_t)rc);
    }

    ASSERT_EQ_U32(n, atomic_load_explicit(&stolen, memory_order_relaxed));
    for (uint32_t i = 0; i < n; ++i) {
        ASSERT_TRUE(atomic_load_explicit(&seen[i], memory_order_relaxed) == 1);
    }

    free(seen);
    free(items);
    fr_ws_deque_destroy(&dq);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

int main(void) {
    struct test_case tests[] = {
        {"owner_push_pop_is_lifo", test_owner_push_pop_is_lifo},
        {"steal_is_fifo_from_top", test_steal_is_fifo_from_top},
        {"push_fails_when_full", test_push_fails_when_full},
        {"concurrent_steal_removes_all_exactly_once", test_concurrent_steal_removes_all_exactly_once},
    };

    for (size_t i = 0; i < ARRAY_SIZE(tests); ++i) {
        int rc = tests[i].fn();
        if (rc != 0) {
            fprintf(stderr, "Test '%s' failed\n", tests[i].name);
            return 1;
        }
        printf("PASS %s\n", tests[i].name);
    }

    printf("ALL TESTS PASSED (%zu tests)\n", ARRAY_SIZE(tests));
    return 0;
}

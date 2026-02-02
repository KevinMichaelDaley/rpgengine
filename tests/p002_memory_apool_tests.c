#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/memory/apool.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                                                         \
    do {                                                                                                 \
        if ((uint32_t)(exp) != (uint32_t)(act)) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", __FILE__, __LINE__,    \
                    (uint32_t)(exp), (uint32_t)(act));                                                    \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int test_apool_basic_alloc_free(void) {
    apool_t pool;
    ASSERT_INT_EQ(APOOL_OK, apool_init(&pool, 4, sizeof(int)));
    apool_handle_t a = apool_alloc(&pool);
    apool_handle_t b = apool_alloc(&pool);
    ASSERT_TRUE(a.index != APOOL_INDEX_INVALID);
    ASSERT_TRUE(b.index != APOOL_INDEX_INVALID);
    ASSERT_INT_EQ(APOOL_OK, apool_free(&pool, a));
    apool_handle_t c = apool_alloc(&pool);
    ASSERT_UINT_EQ(a.index, c.index);
    ASSERT_UINT_EQ((uint16_t)(a.generation + 1u), c.generation);
    apool_destroy(&pool);
    return 0;
}

struct alloc_thread_ctx {
    apool_t *pool;
    atomic_int *seen;
    uint32_t capacity;
    uint32_t iterations;
};

static void *alloc_thread_fn(void *arg) {
    struct alloc_thread_ctx *ctx = (struct alloc_thread_ctx *)arg;
    for (uint32_t i = 0; i < ctx->iterations; ++i) {
        apool_handle_t h = apool_alloc(ctx->pool);
        if (h.index == APOOL_INDEX_INVALID) {
            continue;
        }
        int prev = atomic_fetch_add(&ctx->seen[h.index], 1);
        if (prev != 0) {
            // Duplicate allocation observed; signal failure via a special index.
            atomic_fetch_add(&ctx->seen[ctx->capacity - 1u], 1);
        }
    }
    return NULL;
}

static int test_apool_concurrent_unique_allocations(void) {
    const uint32_t capacity = 64;
    apool_t pool;
    ASSERT_INT_EQ(APOOL_OK, apool_init(&pool, capacity, sizeof(uint64_t)));

    atomic_int seen[64];
    for (uint32_t i = 0; i < capacity; ++i) {
        atomic_init(&seen[i], 0);
    }

    struct alloc_thread_ctx ctx = {&pool, seen, capacity, 256};
    pthread_t threads[4];
    for (int i = 0; i < 4; ++i) {
        pthread_create(&threads[i], NULL, alloc_thread_fn, &ctx);
    }
    for (int i = 0; i < 4; ++i) {
        pthread_join(threads[i], NULL);
    }

    // Sum of allocations should be <= capacity and each index allocated at most once.
    uint32_t total = 0;
    for (uint32_t i = 0; i < capacity; ++i) {
        total += (uint32_t)atomic_load(&seen[i]);
        ASSERT_TRUE(atomic_load(&seen[i]) <= 1);
    }
    ASSERT_TRUE(total <= capacity);

    apool_destroy(&pool);
    return 0;
}

struct free_thread_ctx {
    apool_t *pool;
    apool_handle_t *handles;
    uint32_t count;
};

static void *free_thread_fn(void *arg) {
    struct free_thread_ctx *ctx = (struct free_thread_ctx *)arg;
    for (uint32_t i = 0; i < ctx->count; ++i) {
        (void)apool_free(ctx->pool, ctx->handles[i]);
    }
    return NULL;
}

static int test_apool_concurrent_free_and_generation_increment(void) {
    const uint32_t capacity = 32;
    apool_t pool;
    ASSERT_INT_EQ(APOOL_OK, apool_init(&pool, capacity, sizeof(uint32_t)));

    apool_handle_t handles[32];
    for (uint32_t i = 0; i < capacity; ++i) {
        handles[i] = apool_alloc(&pool);
        ASSERT_TRUE(handles[i].index != APOOL_INDEX_INVALID);
    }

    struct free_thread_ctx ctx = {&pool, handles, capacity};
    pthread_t threads[2];
    pthread_create(&threads[0], NULL, free_thread_fn, &ctx);
    pthread_create(&threads[1], NULL, free_thread_fn, &ctx);
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    // Re-allocate and verify generations have advanced by at least 1.
    for (uint32_t i = 0; i < capacity; ++i) {
        apool_handle_t h = apool_alloc(&pool);
        ASSERT_TRUE(h.index != APOOL_INDEX_INVALID);
        ASSERT_TRUE(h.generation != handles[h.index].generation);
    }

    apool_destroy(&pool);
    return 0;
}

static int test_apool_invalid_free(void) {
    apool_t pool;
    ASSERT_INT_EQ(APOOL_OK, apool_init(&pool, 2, sizeof(int)));
    apool_handle_t bad = {5u, 1u, 0u};
    ASSERT_INT_EQ(APOOL_ERR_INVALID, apool_free(&pool, bad));
    apool_destroy(&pool);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"apool_basic_alloc_free", test_apool_basic_alloc_free},
    {"apool_concurrent_unique_allocations", test_apool_concurrent_unique_allocations},
    {"apool_concurrent_free_and_generation_increment", test_apool_concurrent_free_and_generation_increment},
    {"apool_invalid_free", test_apool_invalid_free},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}

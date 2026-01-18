#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/memory/arena.h"
#include "ferrum/memory/pool.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec4.h"
#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"

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

static int test_arena_reuse_address_stability(void) {
    uint8_t buffer[4096];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    void *first = arena_alloc(&arena, alignof(int), sizeof(int));
    ASSERT_TRUE(first != NULL);
    for (int i = 1; i < 1000; ++i) {
        void *ptr = arena_alloc(&arena, alignof(int), sizeof(int));
        ASSERT_TRUE(ptr != NULL);
    }

    arena_reset(&arena);
    void *second = arena_alloc(&arena, alignof(int), sizeof(int));
    ASSERT_TRUE(second == first);
    return 0;
}

static int test_arena_alignment_common(void) {
    uint8_t buffer[64];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    ASSERT_TRUE(arena_alloc(&arena, 1, 1) != NULL);
    void *aligned = arena_alloc(&arena, alignof(uint64_t), sizeof(uint64_t));
    ASSERT_TRUE(aligned != NULL);
    ASSERT_TRUE(((uintptr_t)aligned % alignof(uint64_t)) == 0);
    return 0;
}

static int test_arena_sequential_allocations_do_not_overlap(void) {
    uint8_t buffer[256];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    const size_t block_size = 16;
    void *prev = NULL;
    for (int i = 0; i < 8; ++i) {
        void *ptr = arena_alloc(&arena, alignof(uint32_t), block_size);
        ASSERT_TRUE(ptr != NULL);
        if (prev != NULL) {
            ASSERT_TRUE((uintptr_t)ptr >= (uintptr_t)prev + block_size);
        }
        prev = ptr;
    }
    ASSERT_TRUE((uintptr_t)prev + block_size <= (uintptr_t)buffer + sizeof(buffer));
    return 0;
}

static int test_pool_alloc_free_reuse(void) {
    pool_t pool;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, 4, sizeof(int)));
    pool_handle_t a = pool_alloc(&pool);
    pool_handle_t b = pool_alloc(&pool);
    ASSERT_TRUE(a.index != POOL_INDEX_INVALID);
    ASSERT_TRUE(b.index != POOL_INDEX_INVALID);

    ASSERT_INT_EQ(POOL_OK, pool_free(&pool, a));
    pool_handle_t c = pool_alloc(&pool);
    ASSERT_UINT_EQ(a.index, c.index);
    ASSERT_UINT_EQ((uint16_t)(a.generation + 1u), c.generation);

    pool_destroy(&pool);
    return 0;
}

static int test_pool_get_stable_pointer(void) {
    pool_t pool;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, 2, sizeof(int)));
    pool_handle_t handle = pool_alloc(&pool);
    ASSERT_TRUE(handle.index != POOL_INDEX_INVALID);

    int *ptr = (int *)pool_get(&pool, handle);
    ASSERT_TRUE(ptr != NULL);
    *ptr = 42;

    pool_handle_t other = pool_alloc(&pool);
    ASSERT_TRUE(other.index != POOL_INDEX_INVALID);

    int *ptr2 = (int *)pool_get(&pool, handle);
    ASSERT_TRUE(ptr2 == ptr);
    ASSERT_INT_EQ(42, *ptr2);

    pool_destroy(&pool);
    return 0;
}

static int test_arena_out_of_memory_returns_null(void) {
    uint8_t buffer[32];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    void *first = arena_alloc(&arena, 1, 16);
    ASSERT_TRUE(first != NULL);
    size_t mark = arena_mark(&arena);

    void *fail = arena_alloc(&arena, 1, 32);
    ASSERT_TRUE(fail == NULL);
    ASSERT_UINT_EQ(mark, arena_mark(&arena));

    void *second = arena_alloc(&arena, 1, 8);
    ASSERT_TRUE(second == (void *)(buffer + mark));
    return 0;
}

static int test_arena_large_alignment(void) {
    alignas(64) uint8_t buffer[256];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    void *ptr = arena_alloc(&arena, 64, 8);
    ASSERT_TRUE(ptr != NULL);
    ASSERT_TRUE(((uintptr_t)ptr % 64u) == 0);
    return 0;
}

static int test_arena_zero_size_allocation(void) {
    uint8_t buffer[64];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    size_t mark = arena_mark(&arena);
    void *p0 = arena_alloc(&arena, 8, 0);
    void *p1 = arena_alloc(&arena, 8, 0);
    ASSERT_TRUE(p0 != NULL);
    ASSERT_TRUE(p0 == p1);
    ASSERT_UINT_EQ(mark, arena_mark(&arena));

    void *p2 = arena_alloc(&arena, 8, 4);
    ASSERT_TRUE(p2 == p0);
    return 0;
}

static int test_arena_mark_pop_nested(void) {
    uint8_t buffer[128];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    size_t mark0 = arena_mark(&arena);
    void *first = arena_alloc(&arena, 4, 16);
    ASSERT_TRUE(first != NULL);

    size_t mark1 = arena_mark(&arena);
    void *second = arena_alloc(&arena, 4, 16);
    ASSERT_TRUE(second != NULL);

    ASSERT_INT_EQ(0, arena_pop_to_mark(&arena, mark1));
    void *second_again = arena_alloc(&arena, 4, 16);
    ASSERT_TRUE(second_again == second);

    ASSERT_INT_EQ(0, arena_pop_to_mark(&arena, mark0));
    void *first_again = arena_alloc(&arena, 4, 16);
    ASSERT_TRUE(first_again == first);
    return 0;
}

static int test_pool_capacity_boundary(void) {
    pool_t pool;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, 2, sizeof(int)));
    pool_handle_t a = pool_alloc(&pool);
    pool_handle_t b = pool_alloc(&pool);
    pool_handle_t c = pool_alloc(&pool);
    ASSERT_TRUE(a.index != POOL_INDEX_INVALID);
    ASSERT_TRUE(b.index != POOL_INDEX_INVALID);
    ASSERT_TRUE(c.index == POOL_INDEX_INVALID);
    pool_destroy(&pool);
    return 0;
}

static int test_pool_generation_mismatch(void) {
    pool_t pool;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, 1, sizeof(int)));
    pool_handle_t a = pool_alloc(&pool);
    ASSERT_INT_EQ(POOL_OK, pool_free(&pool, a));
    pool_handle_t b = pool_alloc(&pool);
    ASSERT_TRUE(b.index == a.index);
    ASSERT_TRUE(pool_get(&pool, a) == NULL);
    ASSERT_INT_EQ(POOL_ERR_INVALID, pool_free(&pool, a));
    pool_destroy(&pool);
    return 0;
}

static int test_pool_invalid_free(void) {
    pool_t pool;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, 2, sizeof(int)));
    pool_handle_t bad = {5u, 1u, 0u};
    ASSERT_INT_EQ(POOL_ERR_INVALID, pool_free(&pool, bad));

    pool_handle_t a = pool_alloc(&pool);
    pool_handle_t b = pool_alloc(&pool);
    ASSERT_TRUE(a.index != POOL_INDEX_INVALID);
    ASSERT_TRUE(b.index != POOL_INDEX_INVALID);
    pool_destroy(&pool);
    return 0;
}

static int test_pool_double_free(void) {
    pool_t pool;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, 1, sizeof(int)));
    pool_handle_t a = pool_alloc(&pool);
    ASSERT_INT_EQ(POOL_OK, pool_free(&pool, a));
    ASSERT_INT_EQ(POOL_ERR_INVALID, pool_free(&pool, a));

    pool_handle_t b = pool_alloc(&pool);
    ASSERT_UINT_EQ(a.index, b.index);
    ASSERT_UINT_EQ((uint16_t)(a.generation + 1u), b.generation);
    pool_destroy(&pool);
    return 0;
}

static int test_arena_alignment_padding_regression(void) {
    uint8_t buffer[512];
    arena_t arena;
    arena_init(&arena, buffer, sizeof(buffer));

    for (size_t i = 1; i <= 16; ++i) {
        size_t alignment = (size_t)1u << (i % 6u);
        void *ptr = arena_alloc(&arena, alignment, i);
        ASSERT_TRUE(ptr != NULL);
        ASSERT_TRUE(((uintptr_t)ptr % alignment) == 0);
    }
    return 0;
}

static int test_pool_freelist_randomized(void) {
    pool_t pool;
    const uint32_t capacity = 32;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, capacity, sizeof(int)));
    pool_handle_t handles[32] = {0};
    uint8_t in_use[32] = {0};
    uint32_t allocated = 0;
    uint32_t seed = 0x1234567u;

    for (int i = 0; i < 1000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        if ((seed & 1u) == 0u) {
            pool_handle_t h = pool_alloc(&pool);
            if (h.index == POOL_INDEX_INVALID) {
                ASSERT_UINT_EQ(capacity, allocated);
                continue;
            }
            ASSERT_TRUE(h.index < capacity);
            ASSERT_TRUE(in_use[h.index] == 0u);
            in_use[h.index] = 1u;
            handles[h.index] = h;
            allocated++;
        } else {
            if (allocated == 0u) {
                continue;
            }
            uint32_t index = seed % capacity;
            if (in_use[index] == 0u) {
                for (uint32_t j = 0; j < capacity; ++j) {
                    if (in_use[j]) {
                        index = j;
                        break;
                    }
                }
            }
            ASSERT_TRUE(in_use[index] != 0u);
            ASSERT_INT_EQ(POOL_OK, pool_free(&pool, handles[index]));
            in_use[index] = 0u;
            allocated--;
        }
    }

    pool_destroy(&pool);
    return 0;
}

static int test_pool_generation_wrap_behavior(void) {
    pool_t pool;
    ASSERT_INT_EQ(POOL_OK, pool_init(&pool, 1, sizeof(int)));
    pool_handle_t handle = pool_alloc(&pool);
    ASSERT_TRUE(handle.index != POOL_INDEX_INVALID);

    for (uint32_t i = 1; i < UINT16_MAX; ++i) {
        ASSERT_INT_EQ(POOL_OK, pool_free(&pool, handle));
        handle = pool_alloc(&pool);
        ASSERT_TRUE(handle.index != POOL_INDEX_INVALID);
    }

    ASSERT_UINT_EQ(UINT16_MAX, handle.generation);
    ASSERT_INT_EQ(POOL_OK, pool_free(&pool, handle));
    pool_handle_t wrapped = pool_alloc(&pool);
    ASSERT_UINT_EQ(1u, wrapped.generation);
    ASSERT_TRUE(pool_get(&pool, handle) == NULL);

    pool_destroy(&pool);
    return 0;
}

static atomic_int g_job_failure = 0;

struct job_ctx {
    size_t index;
};

static arena_t g_job_arenas[2];
static uint8_t g_job_buffers[2][512];
static vec4_t g_job_results[16];
static void *g_job_ptrs[16];
static uint32_t g_job_workers[16];

static void arena_job_fn(void *user) {
    struct job_ctx *ctx = (struct job_ctx *)user;
    uint32_t worker = job_current_worker_id();
    if (worker >= ARRAY_SIZE(g_job_arenas)) {
        atomic_store(&g_job_failure, 1);
        return;
    }
    arena_t *arena = &g_job_arenas[worker];
    void *ptr = arena_alloc(arena, 16, sizeof(vec4_t));
    if (ptr == NULL) {
        atomic_store(&g_job_failure, 1);
        return;
    }
    g_job_ptrs[ctx->index] = ptr;
    g_job_workers[ctx->index] = worker;

    mat4_t m = mat4_mul(mat4_rotation_x(0.05f * (float)ctx->index),
                        mat4_translation((float)ctx->index, 0.25f, -0.5f));
    vec4_t v = {1.0f, 2.0f, 3.0f, 1.0f};
    g_job_results[ctx->index] = mat4_mul_vec4(m, v);
}

static int test_arena_usage_inside_jobs(void) {
    job_system_t *sys = job_system_create(2, 64, 64 * 1024, 0);
    ASSERT_TRUE(sys != NULL);
    ASSERT_INT_EQ(0, job_system_start(sys));

    for (size_t i = 0; i < ARRAY_SIZE(g_job_arenas); ++i) {
        arena_init(&g_job_arenas[i], g_job_buffers[i], sizeof(g_job_buffers[i]));
    }

    struct job_ctx contexts[16];
    job_counter_t counter;
    job_counter_init(&counter, 0);

    for (size_t i = 0; i < ARRAY_SIZE(contexts); ++i) {
        contexts[i].index = i;
        ASSERT_TRUE(job_dispatch(sys, arena_job_fn, &contexts[i], 0, &counter) != JOB_ID_INVALID);
    }
    ASSERT_INT_EQ(JOB_WAIT_OK, job_wait_counter(&counter, 0));
    ASSERT_INT_EQ(0, atomic_load(&g_job_failure));

    for (size_t i = 0; i < ARRAY_SIZE(contexts); ++i) {
        mat4_t m = mat4_mul(mat4_rotation_x(0.05f * (float)i),
                            mat4_translation((float)i, 0.25f, -0.5f));
        vec4_t v = {1.0f, 2.0f, 3.0f, 1.0f};
        vec4_t expected = mat4_mul_vec4(m, v);
        vec4_t actual = g_job_results[i];
        ASSERT_TRUE(actual.x == expected.x && actual.y == expected.y &&
                    actual.z == expected.z && actual.w == expected.w);

        uint32_t worker = g_job_workers[i];
        uint8_t *base = g_job_buffers[worker];
        uint8_t *end = base + sizeof(g_job_buffers[worker]);
        ASSERT_TRUE((uint8_t *)g_job_ptrs[i] >= base && (uint8_t *)g_job_ptrs[i] < end);
        ASSERT_TRUE(((uintptr_t)g_job_ptrs[i] % 16u) == 0u);
    }

    for (size_t i = 0; i < ARRAY_SIZE(contexts); ++i) {
        for (size_t j = i + 1; j < ARRAY_SIZE(contexts); ++j) {
            if (g_job_workers[i] == g_job_workers[j]) {
                ASSERT_TRUE(g_job_ptrs[i] != g_job_ptrs[j]);
            }
        }
    }

    job_system_shutdown(sys);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"arena_reuse_address_stability", test_arena_reuse_address_stability},
    {"arena_alignment_common", test_arena_alignment_common},
    {"arena_sequential_allocations_do_not_overlap", test_arena_sequential_allocations_do_not_overlap},
    {"pool_alloc_free_reuse", test_pool_alloc_free_reuse},
    {"pool_get_stable_pointer", test_pool_get_stable_pointer},
    {"arena_out_of_memory_returns_null", test_arena_out_of_memory_returns_null},
    {"arena_large_alignment", test_arena_large_alignment},
    {"arena_zero_size_allocation", test_arena_zero_size_allocation},
    {"arena_mark_pop_nested", test_arena_mark_pop_nested},
    {"pool_capacity_boundary", test_pool_capacity_boundary},
    {"pool_generation_mismatch", test_pool_generation_mismatch},
    {"pool_invalid_free", test_pool_invalid_free},
    {"pool_double_free", test_pool_double_free},
    {"arena_alignment_padding_regression", test_arena_alignment_padding_regression},
    {"pool_freelist_randomized", test_pool_freelist_randomized},
    {"pool_generation_wrap_behavior", test_pool_generation_wrap_behavior},
    {"arena_usage_inside_jobs", test_arena_usage_inside_jobs},
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

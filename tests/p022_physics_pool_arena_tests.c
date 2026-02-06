#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"

/* ── Test macros ────────────────────────────────────────────────── */

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
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _e = (float)(exp);                                                                         \
        float _a = (float)(act);                                                                         \
        if (fabsf(_e - _a) > (eps)) {                                                                    \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f (eps=%f)\n", __FILE__,  \
                    __LINE__, (double)_e, (double)_a, (double)(eps));                                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_PTR_NOT_NULL(ptr)                                                                         \
    do {                                                                                                 \
        if ((ptr) == NULL) {                                                                             \
            fprintf(stderr, "ASSERT_PTR_NOT_NULL failed: %s:%d: %s\n", __FILE__, __LINE__, #ptr);        \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_PTR_NULL(ptr)                                                                             \
    do {                                                                                                 \
        if ((ptr) != NULL) {                                                                             \
            fprintf(stderr, "ASSERT_PTR_NULL failed: %s:%d: %s\n", __FILE__, __LINE__, #ptr);            \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

/* ── Body pool tests ────────────────────────────────────────────── */

static int test_body_pool_init(void) {
    phys_body_pool_t pool;
    int rc = phys_body_pool_init(&pool, 64);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(64, (int)pool.capacity);
    ASSERT_INT_EQ(0, (int)pool.count);
    ASSERT_PTR_NOT_NULL(pool.bodies_curr);
    ASSERT_PTR_NOT_NULL(pool.bodies_next);
    ASSERT_PTR_NOT_NULL(pool.active);
    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_add_single(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 16);

    phys_body_t b;
    phys_body_init(&b);
    phys_body_set_mass(&b, 5.0f);
    b.position = (phys_vec3_t){1.0f, 2.0f, 3.0f};

    uint32_t idx = 0;
    int rc = phys_body_pool_add(&pool, &b, &idx);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, (int)phys_body_pool_active_count(&pool));
    ASSERT_TRUE(phys_body_pool_is_active(&pool, idx));

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_add_returns_body(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 16);

    phys_body_t b;
    phys_body_init(&b);
    b.position = (phys_vec3_t){10.0f, 20.0f, 30.0f};
    b.inv_mass = 0.25f;

    uint32_t idx = 0;
    phys_body_pool_add(&pool, &b, &idx);

    phys_body_t *curr = phys_body_pool_get_curr(&pool, idx);
    ASSERT_PTR_NOT_NULL(curr);
    ASSERT_FLOAT_NEAR(10.0f, curr->position.x, 1e-6f);
    ASSERT_FLOAT_NEAR(20.0f, curr->position.y, 1e-6f);
    ASSERT_FLOAT_NEAR(30.0f, curr->position.z, 1e-6f);
    ASSERT_FLOAT_NEAR(0.25f, curr->inv_mass, 1e-6f);

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_remove(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 16);

    phys_body_t b;
    phys_body_init(&b);

    uint32_t idx = 0;
    phys_body_pool_add(&pool, &b, &idx);
    ASSERT_INT_EQ(1, (int)phys_body_pool_active_count(&pool));

    phys_body_pool_remove(&pool, idx);
    ASSERT_INT_EQ(0, (int)phys_body_pool_active_count(&pool));
    ASSERT_TRUE(!phys_body_pool_is_active(&pool, idx));

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_double_buffer(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 16);

    phys_body_t b;
    phys_body_init(&b);

    uint32_t idx = 0;
    phys_body_pool_add(&pool, &b, &idx);

    phys_body_t *curr = phys_body_pool_get_curr(&pool, idx);
    phys_body_t *next = phys_body_pool_get_next(&pool, idx);
    ASSERT_PTR_NOT_NULL(curr);
    ASSERT_PTR_NOT_NULL(next);
    ASSERT_TRUE(curr != next);  /* curr and next point to different buffers */

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_swap_buffers(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 16);

    phys_body_t b;
    phys_body_init(&b);
    b.position = (phys_vec3_t){1.0f, 0.0f, 0.0f};

    uint32_t idx = 0;
    phys_body_pool_add(&pool, &b, &idx);

    /* Write an updated position to the next buffer. */
    phys_body_t *next = phys_body_pool_get_next(&pool, idx);
    next->position = (phys_vec3_t){2.0f, 0.0f, 0.0f};

    phys_body_pool_swap_buffers(&pool);

    /* After swap, what was 'next' is now 'curr'. */
    phys_body_t *curr = phys_body_pool_get_curr(&pool, idx);
    ASSERT_FLOAT_NEAR(2.0f, curr->position.x, 1e-6f);

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_add_multiple(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 64);

    phys_body_t b;
    phys_body_init(&b);

    for (int i = 0; i < 10; i++) {
        uint32_t idx = 0;
        b.position.x = (float)i;
        int rc = phys_body_pool_add(&pool, &b, &idx);
        ASSERT_INT_EQ(0, rc);
    }
    ASSERT_INT_EQ(10, (int)phys_body_pool_active_count(&pool));

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_capacity_full(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 4);

    phys_body_t b;
    phys_body_init(&b);

    for (int i = 0; i < 4; i++) {
        uint32_t idx = 0;
        int rc = phys_body_pool_add(&pool, &b, &idx);
        ASSERT_INT_EQ(0, rc);
    }

    /* Pool is full — next add should fail. */
    uint32_t idx = 0;
    int rc = phys_body_pool_add(&pool, &b, &idx);
    ASSERT_TRUE(rc != 0);

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_reuse_slot(void) {
    phys_body_pool_t pool;
    phys_body_pool_init(&pool, 4);

    phys_body_t b;
    phys_body_init(&b);
    b.position = (phys_vec3_t){1.0f, 2.0f, 3.0f};

    uint32_t idx1 = 0;
    phys_body_pool_add(&pool, &b, &idx1);
    phys_body_pool_remove(&pool, idx1);

    /* Re-add; should reuse the freed slot. */
    b.position = (phys_vec3_t){4.0f, 5.0f, 6.0f};
    uint32_t idx2 = 0;
    phys_body_pool_add(&pool, &b, &idx2);
    ASSERT_INT_EQ((int)idx1, (int)idx2);

    phys_body_t *curr = phys_body_pool_get_curr(&pool, idx2);
    ASSERT_FLOAT_NEAR(4.0f, curr->position.x, 1e-6f);

    phys_body_pool_destroy(&pool);
    return 0;
}

static int test_body_pool_null_safe(void) {
    /* All functions must handle NULL pool gracefully. */
    phys_body_pool_destroy(NULL);
    phys_body_pool_remove(NULL, 0);
    phys_body_pool_swap_buffers(NULL);

    ASSERT_PTR_NULL(phys_body_pool_get_curr(NULL, 0));
    ASSERT_PTR_NULL(phys_body_pool_get_next(NULL, 0));
    ASSERT_INT_EQ(0, (int)phys_body_pool_active_count(NULL));
    ASSERT_TRUE(!phys_body_pool_is_active(NULL, 0));

    phys_body_t b;
    phys_body_init(&b);
    uint32_t idx = 0;
    int rc = phys_body_pool_add(NULL, &b, &idx);
    ASSERT_TRUE(rc != 0);

    rc = phys_body_pool_init(NULL, 16);
    ASSERT_TRUE(rc != 0);

    return 0;
}

/* ── Frame arena tests ──────────────────────────────────────────── */

static int test_frame_arena_init(void) {
    phys_frame_arena_t arena;
    int rc = phys_frame_arena_init(&arena, 4096);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(4096, (int)arena.capacity);
    ASSERT_INT_EQ(0, (int)phys_frame_arena_used(&arena));
    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_frame_arena_alloc_aligned(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4096);

    void *p = phys_frame_arena_alloc(&arena, 100, 16);
    ASSERT_PTR_NOT_NULL(p);
    ASSERT_TRUE(((uintptr_t)p & 15) == 0);  /* 16-byte aligned */

    /* Allocate again with different alignment. */
    void *p2 = phys_frame_arena_alloc(&arena, 7, 32);
    ASSERT_PTR_NOT_NULL(p2);
    ASSERT_TRUE(((uintptr_t)p2 & 31) == 0);  /* 32-byte aligned */

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_frame_arena_alloc_tracks_usage(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4096);

    size_t before = phys_frame_arena_used(&arena);
    phys_frame_arena_alloc(&arena, 100, 16);
    size_t after = phys_frame_arena_used(&arena);
    ASSERT_TRUE(after >= before + 100);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_frame_arena_reset(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4096);

    phys_frame_arena_alloc(&arena, 500, 16);
    ASSERT_TRUE(phys_frame_arena_used(&arena) > 0);

    phys_frame_arena_reset(&arena);
    ASSERT_INT_EQ(0, (int)phys_frame_arena_used(&arena));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_frame_arena_remaining(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4096);

    size_t rem_before = phys_frame_arena_remaining(&arena);
    ASSERT_INT_EQ(4096, (int)rem_before);

    phys_frame_arena_alloc(&arena, 100, 16);
    size_t used = phys_frame_arena_used(&arena);
    size_t rem_after = phys_frame_arena_remaining(&arena);
    ASSERT_INT_EQ((int)(4096 - used), (int)rem_after);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_frame_arena_exhaustion(void) {
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024);

    /* Request more than capacity — should return NULL. */
    void *p = phys_frame_arena_alloc(&arena, 2048, 16);
    ASSERT_PTR_NULL(p);

    /* Fill it up with small allocations, then one more should fail. */
    phys_frame_arena_reset(&arena);
    void *ok = phys_frame_arena_alloc(&arena, 1000, 16);
    ASSERT_PTR_NOT_NULL(ok);

    /* Remaining < 100 after alignment; ask for 100 more — might still fit
       or not depending on alignment.  Ask for the full remaining + 1 to be
       certain it fails. */
    size_t rem = phys_frame_arena_remaining(&arena);
    void *fail = phys_frame_arena_alloc(&arena, rem + 1, 1);
    ASSERT_PTR_NULL(fail);

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_frame_arena_null_safe(void) {
    phys_frame_arena_destroy(NULL);
    phys_frame_arena_reset(NULL);

    ASSERT_PTR_NULL(phys_frame_arena_alloc(NULL, 100, 16));
    ASSERT_INT_EQ(0, (int)phys_frame_arena_used(NULL));
    ASSERT_INT_EQ(0, (int)phys_frame_arena_remaining(NULL));

    int rc = phys_frame_arena_init(NULL, 4096);
    ASSERT_TRUE(rc != 0);

    return 0;
}

/* ── Test runner ─────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    /* Body pool */
    {"body_pool_init",           test_body_pool_init},
    {"body_pool_add_single",     test_body_pool_add_single},
    {"body_pool_add_returns_body", test_body_pool_add_returns_body},
    {"body_pool_remove",         test_body_pool_remove},
    {"body_pool_double_buffer",  test_body_pool_double_buffer},
    {"body_pool_swap_buffers",   test_body_pool_swap_buffers},
    {"body_pool_add_multiple",   test_body_pool_add_multiple},
    {"body_pool_capacity_full",  test_body_pool_capacity_full},
    {"body_pool_reuse_slot",     test_body_pool_reuse_slot},
    {"body_pool_null_safe",      test_body_pool_null_safe},
    /* Frame arena */
    {"frame_arena_init",           test_frame_arena_init},
    {"frame_arena_alloc_aligned",  test_frame_arena_alloc_aligned},
    {"frame_arena_alloc_tracks_usage", test_frame_arena_alloc_tracks_usage},
    {"frame_arena_reset",          test_frame_arena_reset},
    {"frame_arena_remaining",      test_frame_arena_remaining},
    {"frame_arena_exhaustion",     test_frame_arena_exhaustion},
    {"frame_arena_null_safe",      test_frame_arena_null_safe},
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

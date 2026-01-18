#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/ecs/common.h"
#include "ferrum/ecs/entity.h"
#include "ferrum/ecs/sparse_set.h"
#include "ferrum/ecs/world.h"
#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec4.h"

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

typedef struct position {
    vec4_t value;
} position_t;

ECS_DEFINE_SPARSE_SET(position, position_t)

static int test_entity_create_destroy_lifecycle(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 8));

    entity_t entities[8];
    for (size_t i = 0; i < ARRAY_SIZE(entities); ++i) {
        ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &entities[i]));
    }
    ASSERT_UINT_EQ(8u, ecs_entity_live_count(&pool));

    entity_t freed[4];
    size_t freed_count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(entities); i += 2) {
        freed[freed_count++] = entities[i];
        ASSERT_INT_EQ(ECS_OK, ecs_entity_destroy(&pool, entities[i]));
    }
    ASSERT_UINT_EQ(4u, ecs_entity_live_count(&pool));

    for (size_t i = freed_count; i > 0; --i) {
        size_t idx = i - 1;
        entity_t reused;
        ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &reused));
        ASSERT_UINT_EQ(freed[idx].index, reused.index);
        ASSERT_UINT_EQ(freed[idx].generation + 1u, reused.generation);
        ASSERT_TRUE(ecs_entity_is_alive(&pool, reused));
        ASSERT_TRUE(!ecs_entity_is_alive(&pool, freed[idx]));
    }

    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_sparse_set_insert_get_remove_basic(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 4));
    entity_t entity;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &entity));

    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 4));

    position_t value = {{1.0f, 2.0f, 3.0f, 1.0f}};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, entity, &value));
    position_t *got = ecs_sparse_set_position_get(&set, entity);
    ASSERT_TRUE(got != NULL);
    ASSERT_TRUE(got->value.x == value.value.x);

    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_remove(&set, entity));
    ASSERT_TRUE(ecs_sparse_set_position_get(&set, entity) == NULL);

    ecs_sparse_set_position_destroy(&set);
    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_swap_removal_integrity(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 4));
    entity_t a;
    entity_t b;
    entity_t c;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &a));
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &b));
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &c));

    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 4));
    position_t va = {{1.0f, 0.0f, 0.0f, 1.0f}};
    position_t vb = {{2.0f, 0.0f, 0.0f, 1.0f}};
    position_t vc = {{3.0f, 0.0f, 0.0f, 1.0f}};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, a, &va));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, b, &vb));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, c, &vc));

    position_t *c_ptr_before = ecs_sparse_set_position_get(&set, c);
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_remove(&set, b));
    ASSERT_UINT_EQ(2u, ecs_sparse_set_position_size(&set));

    entity_t *dense_entities = ecs_sparse_set_position_entities(&set);
    position_t *dense = ecs_sparse_set_position_dense(&set);
    ASSERT_TRUE(dense_entities[0].index == a.index);
    ASSERT_TRUE(dense_entities[1].index == c.index);
    ASSERT_TRUE(ecs_sparse_set_position_get(&set, c) == &dense[1]);
    ASSERT_TRUE(ecs_sparse_set_position_get(&set, b) == NULL);
    ASSERT_TRUE(c_ptr_before != ecs_sparse_set_position_get(&set, c));

    ecs_sparse_set_position_destroy(&set);
    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_remove_last_element(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 4));
    entity_t a;
    entity_t b;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &a));
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &b));

    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 4));
    position_t va = {{1.0f, 0.0f, 0.0f, 1.0f}};
    position_t vb = {{2.0f, 0.0f, 0.0f, 1.0f}};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, a, &va));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, b, &vb));

    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_remove(&set, b));
    ASSERT_UINT_EQ(1u, ecs_sparse_set_position_size(&set));
    ASSERT_TRUE(ecs_sparse_set_position_get(&set, a) != NULL);
    ASSERT_TRUE(ecs_sparse_set_position_get(&set, b) == NULL);

    ecs_sparse_set_position_destroy(&set);
    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_iterate_dense_arrays(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 8));
    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 8));

    float sum = 0.0f;
    for (int i = 0; i < 5; ++i) {
        entity_t e;
        ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &e));
        position_t value = {{(float)i, 1.0f, 0.0f, 1.0f}};
        ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, e, &value));
        sum += value.value.x;
    }

    float dense_sum = 0.0f;
    position_t *dense = ecs_sparse_set_position_dense(&set);
    for (uint32_t i = 0; i < ecs_sparse_set_position_size(&set); ++i) {
        dense_sum += dense[i].value.x;
    }
    ASSERT_TRUE(dense_sum == sum);

    ecs_sparse_set_position_destroy(&set);
    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_duplicate_insert(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 4));
    entity_t e;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &e));

    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 4));
    position_t value = {{1.0f, 0.0f, 0.0f, 1.0f}};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, e, &value));
    ASSERT_INT_EQ(ECS_ERR_EXISTS, ecs_sparse_set_position_insert(&set, e, &value));

    ecs_sparse_set_position_destroy(&set);
    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_remove_nonexistent(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 4));
    entity_t e;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &e));

    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 4));
    ASSERT_INT_EQ(ECS_ERR_NOT_FOUND, ecs_sparse_set_position_remove(&set, e));

    ecs_sparse_set_position_destroy(&set);
    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_sparse_sentinel_correctness(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 4));
    entity_t e;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &e));

    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 4));
    const uint32_t *sparse = ecs_sparse_set_position_sparse(&set);
    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_UINT_EQ(UINT32_MAX, sparse[i]);
    }

    position_t value = {{1.0f, 0.0f, 0.0f, 1.0f}};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, e, &value));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_remove(&set, e));
    ASSERT_UINT_EQ(UINT32_MAX, sparse[e.index]);

    ecs_sparse_set_position_destroy(&set);
    ecs_entity_pool_destroy(&pool);
    return 0;
}

static int test_freelist_reuse_generation_bump(void) {
    ecs_entity_pool_t pool;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_pool_init(&pool, 4));
    entity_t a;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &a));
    ASSERT_INT_EQ(ECS_OK, ecs_entity_destroy(&pool, a));
    entity_t b;
    ASSERT_INT_EQ(ECS_OK, ecs_entity_create(&pool, &b));
    ASSERT_UINT_EQ(a.index, b.index);
    ASSERT_UINT_EQ(a.generation + 1u, b.generation);
    ecs_entity_pool_destroy(&pool);
    return 0;
}
static int test_generation_mismatch_access(void) {
    ecs_world_t world;
    ASSERT_INT_EQ(ECS_OK, ecs_world_init(&world, 2));
    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 2));
    ASSERT_INT_EQ(ECS_OK, ecs_world_register_set(&world, &set.base));

    entity_t a;
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &a));
    position_t value = {{1.0f, 0.0f, 0.0f, 1.0f}};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, a, &value));
    ASSERT_INT_EQ(ECS_OK, ecs_world_destroy_entity(&world, a));

    entity_t reused;
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &reused));
    ASSERT_UINT_EQ(a.index, reused.index);
    ASSERT_TRUE(ecs_sparse_set_position_get(&set, a) == NULL);
    ASSERT_INT_EQ(ECS_ERR_NOT_FOUND, ecs_sparse_set_position_remove(&set, a));

    ecs_sparse_set_position_destroy(&set);
    ecs_world_destroy(&world);
    return 0;
}

static int test_invalid_entity_index(void) {
    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 4));
    entity_t bad = {999u, 1u};
    ASSERT_TRUE(ecs_sparse_set_position_get(&set, bad) == NULL);
    ASSERT_INT_EQ(ECS_ERR_INVALID, ecs_sparse_set_position_remove(&set, bad));
    ecs_sparse_set_position_destroy(&set);
    return 0;
}

static int test_world_entity_exhaustion(void) {
    ecs_world_t world;
    ASSERT_INT_EQ(ECS_OK, ecs_world_init(&world, 2));
    entity_t a;
    entity_t b;
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &a));
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &b));
    entity_t c;
    ASSERT_INT_EQ(ECS_ERR_FULL, ecs_world_create_entity(&world, &c));
    ecs_world_destroy(&world);
    return 0;
}

struct system_ctx {
    ecs_sparse_set_position_t *set;
    size_t start;
    size_t end;
    vec4_t *output;
};

static void system_job_fn(void *user) {
    struct system_ctx *ctx = (struct system_ctx *)user;
    position_t *dense = ecs_sparse_set_position_dense(ctx->set);
    entity_t *entities = ecs_sparse_set_position_entities(ctx->set);
    for (size_t i = ctx->start; i < ctx->end; ++i) {
        mat4_t m = mat4_translation((float)entities[i].index, 0.25f, -0.5f);
        ctx->output[i] = mat4_mul_vec4(m, dense[i].value);
    }
}

static int test_ecs_system_loop_determinism(void) {
    ecs_world_t world;
    ASSERT_INT_EQ(ECS_OK, ecs_world_init(&world, 16));

    ecs_sparse_set_position_t set;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_init(&set, 16));
    ASSERT_INT_EQ(ECS_OK, ecs_world_register_set(&world, &set.base));

    for (uint32_t i = 0; i < 8; ++i) {
        entity_t entity;
        ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &entity));
        position_t value = {{(float)i, 1.0f, 2.0f, 1.0f}};
        ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_position_insert(&set, entity, &value));
    }

    vec4_t output_a[8];
    vec4_t output_b[8];

    for (int pass = 0; pass < 2; ++pass) {
        vec4_t *output = (pass == 0) ? output_a : output_b;
        job_system_t *sys = job_system_create(1, 32, 64 * 1024, 1);
        ASSERT_TRUE(sys != NULL);
        ASSERT_INT_EQ(0, job_system_start(sys));

        struct system_ctx ctxs[2];
        size_t mid = ecs_sparse_set_position_size(&set) / 2u;
        ctxs[0] = (struct system_ctx){&set, 0u, mid, output};
        ctxs[1] = (struct system_ctx){&set, mid, ecs_sparse_set_position_size(&set), output};
        ASSERT_TRUE(job_dispatch(sys, system_job_fn, &ctxs[0], 0, NULL) != JOB_ID_INVALID);
        ASSERT_TRUE(job_dispatch(sys, system_job_fn, &ctxs[1], 0, NULL) != JOB_ID_INVALID);
        ASSERT_INT_EQ(0, job_system_wait_idle(sys));
        job_system_shutdown(sys);
    }

    for (size_t i = 0; i < ecs_sparse_set_position_size(&set); ++i) {
        ASSERT_TRUE(output_a[i].x == output_b[i].x);
        ASSERT_TRUE(output_a[i].y == output_b[i].y);
        ASSERT_TRUE(output_a[i].z == output_b[i].z);
        ASSERT_TRUE(output_a[i].w == output_b[i].w);
    }

    ecs_sparse_set_position_destroy(&set);
    ecs_world_destroy(&world);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"entity_create_destroy_lifecycle", test_entity_create_destroy_lifecycle},
    {"sparse_set_insert_get_remove_basic", test_sparse_set_insert_get_remove_basic},
    {"swap_removal_integrity", test_swap_removal_integrity},
    {"remove_last_element", test_remove_last_element},
    {"iterate_dense_arrays", test_iterate_dense_arrays},
    {"duplicate_insert", test_duplicate_insert},
    {"remove_nonexistent", test_remove_nonexistent},
    {"sparse_sentinel_correctness", test_sparse_sentinel_correctness},
    {"freelist_reuse_generation_bump", test_freelist_reuse_generation_bump},
    {"generation_mismatch_access", test_generation_mismatch_access},
    {"invalid_entity_index", test_invalid_entity_index},
    {"world_entity_exhaustion", test_world_entity_exhaustion},
    {"ecs_system_loop_determinism", test_ecs_system_loop_determinism},
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

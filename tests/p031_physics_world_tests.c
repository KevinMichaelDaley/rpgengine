#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/world.h"

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

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_config_default(void) {
    phys_world_config_t cfg = phys_world_config_default();
    ASSERT_INT_EQ(10000, (int)cfg.max_bodies);
    ASSERT_INT_EQ(10000, (int)cfg.max_colliders);
    ASSERT_INT_EQ(4096, (int)cfg.manifold_cache_size);
    ASSERT_TRUE(cfg.frame_arena_size == 32u * 1024u * 1024u);
    ASSERT_FLOAT_NEAR(1.0f / 60.0f, cfg.fixed_dt, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, cfg.gravity.x, 1e-6f);
    ASSERT_FLOAT_NEAR(-9.81f, cfg.gravity.y, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, cfg.gravity.z, 1e-6f);
    ASSERT_INT_EQ(1, (int)cfg.default_substeps);
    ASSERT_INT_EQ(10, (int)cfg.default_solver_iterations);
    ASSERT_FLOAT_NEAR(0.0f, cfg.baumgarte, 1e-6f);
    ASSERT_FLOAT_NEAR(0.005f, cfg.slop, 1e-6f);
    ASSERT_FLOAT_NEAR(0.08f, cfg.sleep_threshold_linear, 1e-6f);
    ASSERT_FLOAT_NEAR(0.08f, cfg.sleep_threshold_angular, 1e-6f);
    ASSERT_INT_EQ(60, (int)cfg.sleep_delay_frames);
    return 0;
}

static int test_world_init_destroy(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));
    ASSERT_INT_EQ(0, (int)phys_world_body_count(&world));
    ASSERT_TRUE(phys_world_tick_count(&world) == 0);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_create_body(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t idx = phys_world_create_body(&world);
    ASSERT_TRUE(idx != UINT32_MAX);
    ASSERT_INT_EQ(1, (int)phys_world_body_count(&world));

    phys_body_t *body = phys_world_get_body(&world, idx);
    ASSERT_PTR_NOT_NULL(body);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_create_multiple_bodies(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    for (int i = 0; i < 10; i++) {
        uint32_t idx = phys_world_create_body(&world);
        ASSERT_TRUE(idx != UINT32_MAX);
    }
    ASSERT_INT_EQ(10, (int)phys_world_body_count(&world));

    phys_world_destroy(&world);
    return 0;
}

static int test_world_destroy_body(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t idx = phys_world_create_body(&world);
    ASSERT_INT_EQ(1, (int)phys_world_body_count(&world));

    phys_world_destroy_body(&world, idx);
    ASSERT_INT_EQ(0, (int)phys_world_body_count(&world));

    phys_world_destroy(&world);
    return 0;
}

static int test_world_get_body_data(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t idx = phys_world_create_body(&world);
    phys_body_t *body = phys_world_get_body(&world, idx);
    ASSERT_PTR_NOT_NULL(body);

    body->position = (phys_vec3_t){1.0f, 2.0f, 3.0f};
    phys_body_set_mass(body, 5.0f);

    /* Re-fetch and verify data persists. */
    phys_body_t *body2 = phys_world_get_body(&world, idx);
    ASSERT_FLOAT_NEAR(1.0f, body2->position.x, 1e-6f);
    ASSERT_FLOAT_NEAR(2.0f, body2->position.y, 1e-6f);
    ASSERT_FLOAT_NEAR(3.0f, body2->position.z, 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f / 5.0f, body2->inv_mass, 1e-6f);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_set_sphere_collider(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t idx = phys_world_create_body(&world);
    phys_world_set_sphere_collider(&world, idx, 0.5f, (phys_vec3_t){0, 0, 0});

    const phys_collider_t *c = phys_world_get_collider(&world, idx);
    ASSERT_PTR_NOT_NULL(c);
    ASSERT_INT_EQ(PHYS_SHAPE_SPHERE, (int)c->type);

    /* Verify shape was stored. */
    ASSERT_TRUE(c->shape_index < world.sphere_count);
    ASSERT_FLOAT_NEAR(0.5f, world.spheres[c->shape_index].radius, 1e-6f);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_set_box_collider(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t idx = phys_world_create_body(&world);
    phys_vec3_t half = {1.0f, 2.0f, 3.0f};
    phys_quat_t rot = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_world_set_box_collider(&world, idx, half, (phys_vec3_t){0, 0, 0}, rot);

    const phys_collider_t *c = phys_world_get_collider(&world, idx);
    ASSERT_PTR_NOT_NULL(c);
    ASSERT_INT_EQ(PHYS_SHAPE_BOX, (int)c->type);

    ASSERT_TRUE(c->shape_index < world.box_count);
    ASSERT_FLOAT_NEAR(1.0f, world.boxes[c->shape_index].half_extents.x, 1e-6f);
    ASSERT_FLOAT_NEAR(2.0f, world.boxes[c->shape_index].half_extents.y, 1e-6f);
    ASSERT_FLOAT_NEAR(3.0f, world.boxes[c->shape_index].half_extents.z, 1e-6f);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_set_capsule_collider(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t idx = phys_world_create_body(&world);
    phys_quat_t rot = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_world_set_capsule_collider(&world, idx, 0.5f, 1.0f,
                                    (phys_vec3_t){0, 0, 0}, rot);

    const phys_collider_t *c = phys_world_get_collider(&world, idx);
    ASSERT_PTR_NOT_NULL(c);
    ASSERT_INT_EQ(PHYS_SHAPE_CAPSULE, (int)c->type);

    ASSERT_TRUE(c->shape_index < world.capsule_count);
    ASSERT_FLOAT_NEAR(0.5f, world.capsules[c->shape_index].radius, 1e-6f);
    ASSERT_FLOAT_NEAR(1.0f, world.capsules[c->shape_index].half_height, 1e-6f);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_mixed_colliders(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t sphere_idx = phys_world_create_body(&world);
    uint32_t box_idx = phys_world_create_body(&world);
    uint32_t capsule_idx = phys_world_create_body(&world);

    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_world_set_sphere_collider(&world, sphere_idx, 1.0f, (phys_vec3_t){0, 0, 0});
    phys_world_set_box_collider(&world, box_idx, (phys_vec3_t){1, 2, 3},
                                (phys_vec3_t){0, 0, 0}, identity);
    phys_world_set_capsule_collider(&world, capsule_idx, 0.5f, 1.0f,
                                    (phys_vec3_t){0, 0, 0}, identity);

    ASSERT_INT_EQ(PHYS_SHAPE_SPHERE,
                  (int)phys_world_get_collider(&world, sphere_idx)->type);
    ASSERT_INT_EQ(PHYS_SHAPE_BOX,
                  (int)phys_world_get_collider(&world, box_idx)->type);
    ASSERT_INT_EQ(PHYS_SHAPE_CAPSULE,
                  (int)phys_world_get_collider(&world, capsule_idx)->type);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_body_count(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    uint32_t a = phys_world_create_body(&world);
    phys_world_create_body(&world);
    phys_world_create_body(&world);
    ASSERT_INT_EQ(3, (int)phys_world_body_count(&world));

    phys_world_destroy_body(&world, a);
    ASSERT_INT_EQ(2, (int)phys_world_body_count(&world));

    phys_world_destroy(&world);
    return 0;
}

static int test_world_tick_count(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 100;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    ASSERT_TRUE(phys_world_tick_count(&world) == 0);

    phys_world_destroy(&world);
    return 0;
}

static int test_world_null_safe(void) {
    /* phys_world_destroy with NULL should not crash. */
    phys_world_destroy(NULL);

    /* phys_world_init with NULL args should return -1. */
    phys_world_config_t cfg = phys_world_config_default();
    ASSERT_INT_EQ(-1, phys_world_init(NULL, &cfg));

    phys_world_t world;
    ASSERT_INT_EQ(-1, phys_world_init(&world, NULL));

    /* Body operations with NULL world. */
    ASSERT_TRUE(phys_world_create_body(NULL) == UINT32_MAX);
    phys_world_destroy_body(NULL, 0);
    ASSERT_PTR_NULL(phys_world_get_body(NULL, 0));
    ASSERT_INT_EQ(0, (int)phys_world_body_count(NULL));
    ASSERT_TRUE(phys_world_tick_count(NULL) == 0);

    /* Collider operations with NULL world. */
    phys_world_set_sphere_collider(NULL, 0, 1.0f, (phys_vec3_t){0, 0, 0});
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_world_set_box_collider(NULL, 0, (phys_vec3_t){1, 1, 1},
                                (phys_vec3_t){0, 0, 0}, identity);
    phys_world_set_capsule_collider(NULL, 0, 1.0f, 1.0f,
                                    (phys_vec3_t){0, 0, 0}, identity);
    ASSERT_PTR_NULL(phys_world_get_collider(NULL, 0));
    ASSERT_PTR_NULL(phys_world_get_aabb(NULL, 0));

    return 0;
}

/* ── Test runner ─────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"config_default",            test_config_default},
    {"world_init_destroy",        test_world_init_destroy},
    {"world_create_body",         test_world_create_body},
    {"world_create_multiple_bodies", test_world_create_multiple_bodies},
    {"world_destroy_body",        test_world_destroy_body},
    {"world_get_body_data",       test_world_get_body_data},
    {"world_set_sphere_collider", test_world_set_sphere_collider},
    {"world_set_box_collider",    test_world_set_box_collider},
    {"world_set_capsule_collider", test_world_set_capsule_collider},
    {"world_mixed_colliders",     test_world_mixed_colliders},
    {"world_body_count",          test_world_body_count},
    {"world_tick_count",          test_world_tick_count},
    {"world_null_safe",           test_world_null_safe},
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

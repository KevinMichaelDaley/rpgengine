/**
 * @file p090_physics_static_bvh_rebuild_tests.c
 * @brief Regression tests for static BVH rebuild-on-edit semantics.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/job/system.h"

#define ASSERT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                                                     \
    do {                                                                                             \
        if ((uint32_t)(exp) != (uint32_t)(act)) {                                                    \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", __FILE__,         \
                    __LINE__, (unsigned)(exp), (unsigned)(act));                                     \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

static phys_world_config_t test_config_(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    cfg.max_colliders = 16;
    cfg.manifold_cache_size = 64;
    cfg.frame_arena_size = 4u * 1024u * 1024u;
    return cfg;
}

static void setup_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    job_system_create(sys, 1, 256, 65536, 64, 0);
    job_system_start(sys);
    phys_job_context_init(ctx, sys);
}
static void teardown_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    phys_job_context_destroy(ctx);
    job_system_shutdown(sys);
}

static int attach_static_box_(phys_world_t *world, uint32_t idx, phys_vec3_t half_extents) {
    phys_body_t *b = phys_world_get_body(world, idx);
    ASSERT_TRUE(b != NULL);

    b->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;

    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_world_set_box_collider(world, idx, half_extents, (phys_vec3_t){0, 0, 0}, identity);
    return 0;
}

static int attach_dynamic_sphere_(phys_world_t *world, uint32_t idx, float radius, phys_vec3_t pos) {
    phys_body_t *b = phys_world_get_body(world, idx);
    ASSERT_TRUE(b != NULL);

    b->position = pos;
    phys_body_set_mass(b, 1.0f);

    phys_world_set_sphere_collider(world, idx, radius, (phys_vec3_t){0, 0, 0});
    return 0;
}

static int test_static_bvh_invalidates_on_static_collider_edit(void) {
    phys_world_config_t cfg = test_config_();
    phys_world_t world;
    ASSERT_UINT_EQ(0u, (uint32_t)phys_world_init(&world, &cfg));
    world.prediction_mode = 1;

    uint32_t s = phys_world_create_body(&world);
    uint32_t d = phys_world_create_body(&world);
    ASSERT_TRUE(s != UINT32_MAX);
    ASSERT_TRUE(d != UINT32_MAX);

    ASSERT_UINT_EQ(0u, (uint32_t)attach_static_box_(&world, s, (phys_vec3_t){1.0f, 1.0f, 1.0f}));
    ASSERT_UINT_EQ(0u, (uint32_t)attach_dynamic_sphere_(&world, d, 0.5f, (phys_vec3_t){0.75f, 0.0f, 0.0f}));

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    phys_world_tick_parallel(&world, NULL, &ctx);
    ASSERT_TRUE(world.static_bvh_valid != 0);

    /* Editing a static collider should mark the static BVH dirty. */
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_world_set_box_collider(&world, s, (phys_vec3_t){2.0f, 2.0f, 2.0f},
                               (phys_vec3_t){0, 0, 0}, identity);
    ASSERT_TRUE(world.static_bvh_valid == 0);

    /* Next tick should rebuild and re-enable BVH-backed broadphase. */
    phys_world_tick_parallel(&world, NULL, &ctx);
    ASSERT_TRUE(world.static_bvh_valid != 0);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

static int test_static_bvh_invalidates_on_static_body_destroy(void) {
    phys_world_config_t cfg = test_config_();
    phys_world_t world;
    ASSERT_UINT_EQ(0u, (uint32_t)phys_world_init(&world, &cfg));
    world.prediction_mode = 1;

    uint32_t s = phys_world_create_body(&world);
    uint32_t d = phys_world_create_body(&world);
    ASSERT_TRUE(s != UINT32_MAX);
    ASSERT_TRUE(d != UINT32_MAX);

    ASSERT_UINT_EQ(0u, (uint32_t)attach_static_box_(&world, s, (phys_vec3_t){1.0f, 1.0f, 1.0f}));
    ASSERT_UINT_EQ(0u, (uint32_t)attach_dynamic_sphere_(&world, d, 0.5f, (phys_vec3_t){0.75f, 0.0f, 0.0f}));

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    phys_world_tick_parallel(&world, NULL, &ctx);
    ASSERT_TRUE(world.static_bvh_valid != 0);

    /* Removing a static body should mark the BVH dirty (it can now be rebuilt or cleared). */
    phys_world_destroy_body(&world, s);
    ASSERT_TRUE(world.static_bvh_valid == 0);

    /* With no static bodies remaining, the next tick should not rebuild. */
    phys_world_tick_parallel(&world, NULL, &ctx);
    ASSERT_TRUE(world.static_bvh_valid == 0);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

#define RUN_TEST(fn)                                                                                   \
    do {                                                                                               \
        printf("  %-60s", #fn);                                                                       \
        int rc = (fn)();                                                                               \
        printf("%s\n", rc ? "FAIL" : "PASS");                                                      \
        if (rc)                                                                                        \
            return rc;                                                                                 \
    } while (0)

int main(void) {
    printf("RUN p090_physics_static_bvh_rebuild_tests\n");
    RUN_TEST(test_static_bvh_invalidates_on_static_collider_edit);
    RUN_TEST(test_static_bvh_invalidates_on_static_body_destroy);
    return 0;
}

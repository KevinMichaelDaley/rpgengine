#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/job/system.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((int)(exp) != (int)(act)) {                                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,  \
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

static int make_world_(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16u;
    cfg.max_colliders = 16u;
    cfg.manifold_cache_size = 64u;
    cfg.frame_arena_size = 8u * 1024u * 1024u;
    return phys_world_init(world, &cfg);
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

static bool grid_contains_body_(const phys_spatial_grid_t *grid, uint32_t body_id) {
    if (!grid || !grid->cells || grid->cell_count == 0) {
        return false;
    }

    for (uint32_t i = 0; i < grid->cell_count; ++i) {
        const phys_grid_cell_t *c = &grid->cells[i];
        for (uint32_t j = 0; j < c->count; ++j) {
            if (c->body_indices[j] == body_id) {
                return true;
            }
        }
    }

    return false;
}

static int test_static_collision_generates_manifold_and_excludes_grid(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    const uint32_t s = phys_world_create_body(&world);
    const uint32_t d = phys_world_create_body(&world);
    ASSERT_TRUE(s != UINT32_MAX);
    ASSERT_TRUE(d != UINT32_MAX);

    /* Static box centered at origin. */
    phys_body_t *bs = phys_world_get_body(&world, s);
    ASSERT_TRUE(bs);
    bs->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    bs->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;

    phys_quat_t identity = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    phys_world_set_box_collider(&world, s, (phys_vec3_t){1.0f, 1.0f, 1.0f},
                               (phys_vec3_t){0, 0, 0}, identity);

    /* Dynamic sphere overlapping the static box. */
    phys_body_t *bd = phys_world_get_body(&world, d);
    ASSERT_TRUE(bd);
    bd->position = (phys_vec3_t){1.2f, 0.0f, 0.0f};
    phys_body_set_mass(bd, 1.0f);

    phys_world_set_sphere_collider(&world, d, 0.75f, (phys_vec3_t){0, 0, 0});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    phys_world_tick_parallel(&world, NULL, &ctx);

    ASSERT_TRUE(world.static_bvh_valid != 0);
    ASSERT_TRUE(world.query_grid_valid != 0);

    /* The query grid should exclude static geometry once the BVH is active. */
    ASSERT_TRUE(!grid_contains_body_(&world.query_grid, s));

    /* Collision response pipeline should still detect dynamic-vs-static contact. */
    phys_manifold_t *m = phys_manifold_cache_find(&world.manifold_cache, s, d);
    ASSERT_TRUE(m != NULL);
    ASSERT_TRUE(m->point_count > 0);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

#define RUN_TEST(fn)                                                                                     \
    do {                                                                                                 \
        printf("  %-60s", #fn);                                                                          \
        int _r = fn();                                                                                   \
        printf("%s\n", _r ? "FAIL" : "PASS");                                                        \
        if (_r) fail_count++;                                                                            \
        test_count++;                                                                                    \
    } while (0)

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p091_physics_phase6_integration_tests:\n");

    RUN_TEST(test_static_collision_generates_manifold_and_excludes_grid);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}

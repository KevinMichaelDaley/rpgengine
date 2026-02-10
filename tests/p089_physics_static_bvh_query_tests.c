/**
 * @file p089_physics_static_bvh_query_tests.c
 * @brief Unit tests for static BVH AABB query + broadphase integration.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/spatial_update.h"
#include "ferrum/physics/static_bvh.h"
#include "ferrum/physics/tier_list.h"

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",               \
                    __FILE__, __LINE__, #cond);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                               \
    do {                                                                       \
        if ((uint32_t)(exp) != (uint32_t)(act)) {                              \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", \
                    __FILE__, __LINE__, (unsigned)(exp), (unsigned)(act));     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static bool results_contain(const uint32_t *results, uint32_t count,
                            uint32_t value) {
    for (uint32_t i = 0; i < count; ++i) {
        if (results[i] == value) {
            return true;
        }
    }
    return false;
}

static int test_static_bvh_query_null_safe(void) {
    uint32_t out[4];
    ASSERT_UINT_EQ(0, phys_static_bvh_query_aabb(NULL, NULL, out, 4));
    ASSERT_UINT_EQ(0, phys_static_bvh_query_aabb(NULL, NULL, NULL, 0));
    return 0;
}

static int test_static_bvh_query_finds_overlaps(void) {
    phys_aabb_t items[3] = {
        {{0,0,0},{1,1,1}},
        {{2,2,2},{3,3,3}},
        {{0.5f,0.5f,0.5f},{1.5f,1.5f,1.5f}},
    };
    uint32_t ids[3] = {10u, 11u, 12u};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024u * 1024u);

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, items, ids, 3, &arena);

    phys_aabb_t q = {{0.75f,0.75f,0.75f},{1.25f,1.25f,1.25f}};
    uint32_t out[8];
    uint32_t n = phys_static_bvh_query_aabb(&bvh, &q, out, 8);

    ASSERT_UINT_EQ(2, n);
    ASSERT_TRUE(results_contain(out, n, 10u));
    ASSERT_TRUE(results_contain(out, n, 12u));
    ASSERT_TRUE(!results_contain(out, n, 11u));

    phys_frame_arena_destroy(&arena);
    return 0;
}

static int test_broadphase_emits_static_pairs_via_bvh(void) {
    /* 2 static bodies (0,1) and 1 dynamic (2). Static bodies excluded from grid;
     * dynamic should still produce (static,dynamic) pair via BVH query. */

    phys_body_t bodies[3];
    phys_collider_t colliders[3];
    phys_sphere_t spheres[3] = {
        {.radius = 1.0f},
        {.radius = 1.0f},
        {.radius = 1.0f},
    };
    phys_aabb_t aabbs[3];

    for (int i = 0; i < 3; ++i) {
        phys_body_init(&bodies[i]);
        phys_collider_init_sphere(&colliders[i], (uint32_t)i, (phys_vec3_t){0,0,0});
    }

    bodies[0].position = (phys_vec3_t){0,0,0};
    bodies[1].position = (phys_vec3_t){100,0,0};

    /* Dynamic body near static body 0. */
    phys_body_set_mass(&bodies[2], 1.0f);
    bodies[2].position = (phys_vec3_t){1.5f,0,0};
    bodies[2].tier = PHYS_TIER_0_DIRECT;

    uint8_t active[3] = {1,1,1};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024u * 1024u);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 64u, 4.0f, &arena);

    phys_spatial_update_args_t su = {
        .bodies     = bodies,
        .colliders  = colliders,
        .spheres    = spheres,
        .boxes      = NULL,
        .capsules   = NULL,
        .aabbs_out  = aabbs,
        .grid_out   = &grid,
        .active     = active,
        .body_count = 3,
        .exclude_static_from_grid = 1,
    };
    phys_stage_spatial_update(&su);

    /* Build BVH over static bodies (0,1). */
    phys_aabb_t static_items[2] = {aabbs[0], aabbs[1]};
    uint32_t static_ids[2] = {0u, 1u};

    phys_static_bvh_t bvh;
    phys_static_bvh_build(&bvh, static_items, static_ids, 2, &arena);

    uint8_t static_flags[64];
    memset(static_flags, 0, sizeof(static_flags));
    phys_static_bvh_build_bucket_flags(&bvh, 64u, 4.0f, static_flags);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 3);
    phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 2);

    phys_collision_pair_t pairs[16];
    uint32_t pair_count = 0;

    phys_stage_broadphase(&(phys_broadphase_args_t){
        .bodies         = bodies,
        .aabbs          = aabbs,
        .grid           = &grid,
        .tier_lists     = &lists,
        .pairs_out      = pairs,
        .max_pairs      = 16,
        .pair_count_out = &pair_count,
        .static_bvh     = &bvh,
        .static_bucket_flags = static_flags,
        .static_bucket_flag_count = 64u,
    });

    ASSERT_UINT_EQ(1u, pair_count);
    ASSERT_UINT_EQ(0u, pairs[0].body_a);
    ASSERT_UINT_EQ(2u, pairs[0].body_b);

    phys_frame_arena_destroy(&arena);
    return 0;
}

#define RUN_TEST(fn) do { \
    printf("  %-55s", #fn); \
    int rc = (fn)(); \
    printf("%s\n", rc ? "FAIL" : "PASS"); \
    if (rc) return rc; \
} while (0)

int main(void) {
    printf("RUN p089_physics_static_bvh_query_tests\n");
    RUN_TEST(test_static_bvh_query_null_safe);
    RUN_TEST(test_static_bvh_query_finds_overlaps);
    RUN_TEST(test_broadphase_emits_static_pairs_via_bvh);
    return 0;
}

/**
 * @file p032_physics_phase0_integration_tests.c
 * @brief Integration tests verifying all Phase 0 physics data structures
 *        work together: allocation, initialization, cross-subsystem wiring,
 *        and cleanup.  No simulation — just structure plumbing.
 */

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/aabb.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                       \
        if ((exp) != (act)) {                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                       \
        float _e = (float)(exp);                                               \
        float _a = (float)(act);                                               \
        if (fabsf(_e - _a) > (eps)) {                                          \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %f got %f (eps=%f)\n",                            \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)(eps)); \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_PTR_NOT_NULL(ptr)                                               \
    do {                                                                       \
        if ((ptr) == NULL) {                                                   \
            fprintf(stderr, "ASSERT_PTR_NOT_NULL failed: %s:%d: %s\n",        \
                    __FILE__, __LINE__, #ptr);                                  \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Return a world config suitable for integration tests. */
static phys_world_config_t test_config(uint32_t max_bodies) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies     = max_bodies;
    cfg.max_colliders  = max_bodies;
    cfg.frame_arena_size = 4u * 1024u * 1024u; /* 4 MiB */
    return cfg;
}

static const phys_quat_t QUAT_IDENTITY = {0.0f, 0.0f, 0.0f, 1.0f};

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Create 100 bodies with alternating collider types (sphere/box/capsule),
 * set mass and inertia, verify collider types.  Destroy world cleanly.
 */
static int test_create_world_100_bodies(void) {
    phys_world_config_t cfg = test_config(1000);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    uint32_t indices[100];
    for (int i = 0; i < 100; ++i) {
        indices[i] = phys_world_create_body(&world);
        ASSERT_TRUE(indices[i] != UINT32_MAX);

        phys_body_t *b = phys_world_get_body(&world, indices[i]);
        ASSERT_PTR_NOT_NULL(b);

        b->position = (phys_vec3_t){(float)i * 2.0f, 0.0f, 0.0f};
        float mass = 1.0f + (float)i * 0.1f;
        phys_body_set_mass(b, mass);

        switch (i % 3) {
        case 0:
            phys_world_set_sphere_collider(&world, indices[i], 0.5f,
                                           (phys_vec3_t){0, 0, 0});
            phys_body_set_sphere_inertia(b, mass, 0.5f);
            break;
        case 1:
            phys_world_set_box_collider(&world, indices[i],
                (phys_vec3_t){0.5f, 0.5f, 0.5f},
                (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
            phys_body_set_box_inertia(b, mass,
                (phys_vec3_t){0.5f, 0.5f, 0.5f});
            break;
        case 2:
            phys_world_set_capsule_collider(&world, indices[i],
                0.3f, 0.5f, (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
            phys_body_set_capsule_inertia(b, mass, 0.3f, 0.5f);
            break;
        }
    }

    ASSERT_INT_EQ(100, (int)phys_world_body_count(&world));

    /* Verify collider types match expectations. */
    for (int i = 0; i < 100; ++i) {
        const phys_collider_t *c =
            phys_world_get_collider(&world, indices[i]);
        ASSERT_PTR_NOT_NULL(c);

        phys_shape_type_t expected;
        switch (i % 3) {
        case 0:  expected = PHYS_SHAPE_SPHERE;  break;
        case 1:  expected = PHYS_SHAPE_BOX;     break;
        default: expected = PHYS_SHAPE_CAPSULE;  break;
        }
        ASSERT_INT_EQ((int)expected, (int)c->type);
    }

    phys_world_destroy(&world);
    return 0;
}

/**
 * Verify tier lists allocate from the world's frame arena and that
 * arena reset reclaims all memory.
 */
static int test_tier_lists_from_arena(void) {
    phys_world_config_t cfg = test_config(1000);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    /* Create some bodies so we have indices to add. */
    uint32_t body_indices[5];
    for (int i = 0; i < 5; ++i) {
        body_indices[i] = phys_world_create_body(&world);
    }

    size_t arena_before = phys_frame_arena_used(&world.frame_arena);

    phys_tier_lists_t tier_lists;
    phys_tier_lists_init(&tier_lists, &world.frame_arena, 1000);

    size_t arena_after = phys_frame_arena_used(&world.frame_arena);
    ASSERT_TRUE(arena_after > arena_before);

    /* Add bodies to different tiers. */
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_0_DIRECT], body_indices[0]);
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_1_NEAR],   body_indices[1]);
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_2_VISIBLE], body_indices[2]);
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_3_WORLD],   body_indices[3]);
    /* body 4 goes to sleeping — should NOT count as active. */
    phys_tier_list_add(&tier_lists.tiers[PHYS_TIER_5_SLEEPING], body_indices[4]);

    ASSERT_INT_EQ(4, (int)phys_tier_lists_total_active(&tier_lists));

    /* Arena reset should reclaim everything. */
    phys_frame_arena_reset(&world.frame_arena);
    ASSERT_INT_EQ(0, (int)phys_frame_arena_used(&world.frame_arena));

    phys_world_destroy(&world);
    return 0;
}

/**
 * Create two bodies, insert a manifold into the world's cache, set
 * warmstart data, find it again, and verify persistence.
 */
static int test_manifold_cache_with_world(void) {
    phys_world_config_t cfg = test_config(100);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    uint32_t h1 = phys_world_create_body(&world);
    uint32_t h2 = phys_world_create_body(&world);

    /* Create a manifold in the persistent cache. */
    phys_manifold_t *m =
        phys_manifold_cache_get_or_create(&world.manifold_cache, h1, h2, 1);
    ASSERT_PTR_NOT_NULL(m);

    /* Set some warmstart data. */
    m->friction = 0.5f;
    m->normal_impulse[0] = 42.0f;

    /* Find should return the same manifold. */
    phys_manifold_t *found =
        phys_manifold_cache_find(&world.manifold_cache, h1, h2);
    ASSERT_TRUE(found == m);
    ASSERT_FLOAT_NEAR(0.5f, found->friction, 1e-6f);
    ASSERT_FLOAT_NEAR(42.0f, found->normal_impulse[0], 1e-6f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Create 10 bodies with different positions, compute AABBs from
 * colliders, insert into a spatial grid, and query a region.
 */
static int test_spatial_grid_with_bodies(void) {
    phys_world_config_t cfg = test_config(100);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    uint32_t body_ids[10];
    for (int i = 0; i < 10; ++i) {
        body_ids[i] = phys_world_create_body(&world);
        phys_body_t *b = phys_world_get_body(&world, body_ids[i]);
        b->position = (phys_vec3_t){(float)i * 5.0f, 0.0f, 0.0f};
        phys_body_set_mass(b, 1.0f);
        phys_world_set_sphere_collider(&world, body_ids[i], 1.0f,
                                       (phys_vec3_t){0, 0, 0});

        /* Compute AABB and store into the world's aabbs array. */
        phys_aabb_from_sphere(&world.aabbs[body_ids[i]],
                              b->position, 1.0f);
    }

    /* Init spatial grid from frame arena. */
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, 10.0f, &world.frame_arena);

    /* Insert all bodies into grid. */
    for (int i = 0; i < 10; ++i) {
        phys_spatial_grid_insert(&grid, body_ids[i],
                                 &world.aabbs[body_ids[i]]);
    }

    /* Query a region around body 0 (position 0,0,0 ± 2). */
    phys_aabb_t query_region = {
        .min = {-2.0f, -2.0f, -2.0f},
        .max = { 2.0f,  2.0f,  2.0f}
    };
    uint32_t results[10];
    uint32_t count = phys_spatial_grid_query(&grid, &query_region,
                                             results, 10);

    /* Body 0 (at x=0) should be found; body 1 (at x=5) should not
     * since its AABB is [4,6] and query goes to x=2. */
    ASSERT_TRUE(count >= 1);
    bool found_body_0 = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (results[i] == body_ids[0]) {
            found_body_0 = true;
        }
    }
    ASSERT_TRUE(found_body_0);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Create constraints linking bodies into two chains and build islands.
 * Chain 1: 0-1-2  Chain 2: 3-4  → expect 2 islands.
 */
static int test_islands_from_constraints(void) {
    phys_world_config_t cfg = test_config(100);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    /* Create 5 bodies. */
    uint32_t bids[5];
    for (int i = 0; i < 5; ++i) {
        bids[i] = phys_world_create_body(&world);
    }

    /* Allocate constraints on frame arena.
     * Chain 1: 0–1, 1–2   Chain 2: 3–4. */
    phys_constraint_t *constraints = (phys_constraint_t *)
        phys_frame_arena_alloc(&world.frame_arena,
                               3 * sizeof(phys_constraint_t),
                               _Alignof(phys_constraint_t));
    ASSERT_PTR_NOT_NULL(constraints);
    memset(constraints, 0, 3 * sizeof(phys_constraint_t));

    constraints[0].body_a = bids[0];
    constraints[0].body_b = bids[1];
    constraints[1].body_a = bids[1];
    constraints[1].body_b = bids[2];
    constraints[2].body_a = bids[3];
    constraints[2].body_b = bids[4];

    /* Build island list. */
    phys_island_list_t islands;
    phys_island_list_init(&islands, &world.frame_arena, cfg.max_bodies, 100);
    phys_island_list_build(&islands, constraints, 3, cfg.max_bodies,
                           &world.frame_arena);

    /* Should have exactly 2 islands. */
    ASSERT_INT_EQ(2, (int)islands.count);

    /* Verify total bodies across islands = 5. */
    uint32_t total_bodies = 0;
    for (uint32_t i = 0; i < islands.count; ++i) {
        total_bodies += islands.islands[i].body_count;
    }
    ASSERT_INT_EQ(5, (int)total_bodies);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Create a world with bodies, set up a game state with a player,
 * and query distance_to_nearest_player.
 */
static int test_game_state_with_world(void) {
    phys_world_config_t cfg = test_config(100);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    /* Create a body at (10, 0, 0). */
    uint32_t bid = phys_world_create_body(&world);
    phys_body_t *b = phys_world_get_body(&world, bid);
    b->position = (phys_vec3_t){10.0f, 0.0f, 0.0f};

    /* Set up game state with one player at origin. */
    phys_game_state_t gs;
    phys_game_state_init(&gs);
    phys_player_state_t player;
    memset(&player, 0, sizeof(player));
    player.position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    player.interaction_radius = 5.0f;
    phys_game_state_set_player(&gs, 0, &player);

    /* Distance from body to nearest player should be 10. */
    float dist = phys_game_state_distance_to_nearest_player(
        &gs, b->position);
    ASSERT_FLOAT_NEAR(10.0f, dist, 1e-3f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Full pipeline data setup: world, bodies, colliders, AABBs, spatial
 * grid, manifolds, constraints, and islands — all wired together.
 */
static int test_full_pipeline_data_setup(void) {
    phys_world_config_t cfg = test_config(1000);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    /* Create 10 bodies with colliders and AABBs. */
    uint32_t bids[10];
    for (int i = 0; i < 10; ++i) {
        bids[i] = phys_world_create_body(&world);
        phys_body_t *b = phys_world_get_body(&world, bids[i]);
        b->position = (phys_vec3_t){(float)i * 3.0f, 0.0f, 0.0f};
        phys_body_set_mass(b, 1.0f);

        /* Alternate collider types. */
        switch (i % 3) {
        case 0:
            phys_world_set_sphere_collider(&world, bids[i], 0.5f,
                                           (phys_vec3_t){0, 0, 0});
            phys_body_set_sphere_inertia(b, 1.0f, 0.5f);
            phys_aabb_from_sphere(&world.aabbs[bids[i]],
                                  b->position, 0.5f);
            break;
        case 1:
            phys_world_set_box_collider(&world, bids[i],
                (phys_vec3_t){0.5f, 0.5f, 0.5f},
                (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
            phys_body_set_box_inertia(b, 1.0f,
                (phys_vec3_t){0.5f, 0.5f, 0.5f});
            phys_aabb_from_box(&world.aabbs[bids[i]], b->position,
                               QUAT_IDENTITY,
                               (phys_vec3_t){0.5f, 0.5f, 0.5f});
            break;
        case 2:
            phys_world_set_capsule_collider(&world, bids[i],
                0.3f, 0.5f, (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
            phys_body_set_capsule_inertia(b, 1.0f, 0.3f, 0.5f);
            phys_aabb_from_capsule(&world.aabbs[bids[i]], b->position,
                                   QUAT_IDENTITY, 0.3f, 0.5f);
            break;
        }
    }
    ASSERT_INT_EQ(10, (int)phys_world_body_count(&world));

    /* Spatial grid. */
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, 10.0f, &world.frame_arena);
    for (int i = 0; i < 10; ++i) {
        phys_spatial_grid_insert(&grid, bids[i], &world.aabbs[bids[i]]);
    }

    /* Manifold cache: create a fake manifold between bodies 0 and 1. */
    phys_manifold_t *m =
        phys_manifold_cache_get_or_create(&world.manifold_cache,
                                          bids[0], bids[1], 1);
    ASSERT_PTR_NOT_NULL(m);
    m->friction = 0.4f;

    /* Constraints: chain bodies 0-1-2 and 3-4. */
    phys_constraint_t *constraints = (phys_constraint_t *)
        phys_frame_arena_alloc(&world.frame_arena,
                               3 * sizeof(phys_constraint_t),
                               _Alignof(phys_constraint_t));
    ASSERT_PTR_NOT_NULL(constraints);
    memset(constraints, 0, 3 * sizeof(phys_constraint_t));

    constraints[0].body_a = bids[0];
    constraints[0].body_b = bids[1];
    constraints[1].body_a = bids[1];
    constraints[1].body_b = bids[2];
    constraints[2].body_a = bids[3];
    constraints[2].body_b = bids[4];

    /* Islands. */
    phys_island_list_t islands;
    phys_island_list_init(&islands, &world.frame_arena, cfg.max_bodies, 100);
    phys_island_list_build(&islands, constraints, 3, cfg.max_bodies,
                           &world.frame_arena);
    ASSERT_INT_EQ(2, (int)islands.count);

    /* Verify the manifold is still findable. */
    phys_manifold_t *found =
        phys_manifold_cache_find(&world.manifold_cache, bids[0], bids[1]);
    ASSERT_TRUE(found == m);
    ASSERT_FLOAT_NEAR(0.4f, found->friction, 1e-6f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Stress test: create 1000 bodies with colliders, verify count,
 * destroy all, verify count = 0, destroy world.
 */
static int test_world_stress_1000_bodies(void) {
    phys_world_config_t cfg = test_config(1000);
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    uint32_t indices[1000];
    for (int i = 0; i < 1000; ++i) {
        indices[i] = phys_world_create_body(&world);
        ASSERT_TRUE(indices[i] != UINT32_MAX);

        phys_body_t *b = phys_world_get_body(&world, indices[i]);
        phys_body_set_mass(b, 1.0f);

        switch (i % 3) {
        case 0:
            phys_world_set_sphere_collider(&world, indices[i], 0.5f,
                                           (phys_vec3_t){0, 0, 0});
            break;
        case 1:
            phys_world_set_box_collider(&world, indices[i],
                (phys_vec3_t){0.5f, 0.5f, 0.5f},
                (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
            break;
        case 2:
            phys_world_set_capsule_collider(&world, indices[i],
                0.3f, 0.5f, (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
            break;
        }
    }
    ASSERT_INT_EQ(1000, (int)phys_world_body_count(&world));

    /* Destroy all bodies. */
    for (int i = 0; i < 1000; ++i) {
        phys_world_destroy_body(&world, indices[i]);
    }
    ASSERT_INT_EQ(0, (int)phys_world_body_count(&world));

    phys_world_destroy(&world);
    return 0;
}

/* ── Test runner ─────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"create_world_100_bodies",     test_create_world_100_bodies},
    {"tier_lists_from_arena",       test_tier_lists_from_arena},
    {"manifold_cache_with_world",   test_manifold_cache_with_world},
    {"spatial_grid_with_bodies",    test_spatial_grid_with_bodies},
    {"islands_from_constraints",    test_islands_from_constraints},
    {"game_state_with_world",       test_game_state_with_world},
    {"full_pipeline_data_setup",    test_full_pipeline_data_setup},
    {"world_stress_1000_bodies",    test_world_stress_1000_bodies},
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/spatial_update.h"
#include "ferrum/physics/static_bvh.h"
#include "ferrum/physics/tier_list.h"

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void setup_scene_(phys_body_t *bodies,
                         phys_collider_t *colliders,
                         phys_sphere_t *spheres,
                         uint8_t *active,
                         uint32_t static_count,
                         uint32_t dynamic_count) {
    const uint32_t total = static_count + dynamic_count;

    for (uint32_t i = 0; i < total; ++i) {
        phys_body_init(&bodies[i]);
        active[i] = 1;

        spheres[i].radius = 0.5f;
        phys_collider_init_sphere(&colliders[i], i, (phys_vec3_t){0, 0, 0});
    }

    /* Place a set of colliding static/dynamic pairs spread out so there are
     * no dynamic-dynamic overlaps. */
    for (uint32_t i = 0; i < dynamic_count; ++i) {
        uint32_t s = i;
        uint32_t d = static_count + i;

        float x = (float)i * 10.0f;
        bodies[s].position = (phys_vec3_t){x, 0.0f, 0.0f};

        bodies[d].position = (phys_vec3_t){x + 0.75f, 0.0f, 0.0f};
        phys_body_set_mass(&bodies[d], 1.0f);
        bodies[d].tier = PHYS_TIER_0_DIRECT;
    }

    /* Remaining statics are far away in a grid-like distribution. */
    for (uint32_t i = dynamic_count; i < static_count; ++i) {
        float x = (float)(i % 100u) * 4.0f;
        float z = (float)(i / 100u) * 4.0f + 2000.0f;
        bodies[i].position = (phys_vec3_t){x, 0.0f, z};
    }
}

static double bench_mode_ms_(bool use_bvh,
                            const phys_static_bvh_t *static_bvh,
                            const uint8_t *bucket_flags,
                            uint32_t bucket_flag_count,
                            phys_frame_arena_t *frame_arena,
                            phys_body_t *bodies,
                            phys_collider_t *colliders,
                            phys_sphere_t *spheres,
                            phys_aabb_t *aabbs,
                            const uint8_t *active,
                            uint32_t static_count,
                            uint32_t dynamic_count,
                            phys_collision_pair_t *pairs,
                            uint32_t max_pairs,
                            uint32_t iters) {

    const uint32_t total = static_count + dynamic_count;

    uint64_t t0 = now_ns();

    for (uint32_t iter = 0; iter < iters; ++iter) {
        phys_frame_arena_reset(frame_arena);

        phys_spatial_grid_t grid;
        phys_spatial_grid_init(&grid, 256u, 2.0f, frame_arena);

        phys_stage_spatial_update(&(phys_spatial_update_args_t){
            .bodies = bodies,
            .colliders = colliders,
            .spheres = spheres,
            .boxes = NULL,
            .capsules = NULL,
            .aabbs_out = aabbs,
            .grid_out = &grid,
            .active = active,
            .body_count = total,
            .exclude_static_from_grid = use_bvh ? 1 : 0,
        });

        phys_tier_lists_t lists;
        phys_tier_lists_init(&lists, frame_arena, dynamic_count);
        for (uint32_t i = 0; i < dynamic_count; ++i) {
            phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], static_count + i);
        }

        uint32_t pair_count = 0;
        phys_stage_broadphase(&(phys_broadphase_args_t){
            .bodies = bodies,
            .aabbs = aabbs,
            .grid = &grid,
            .tier_lists = &lists,
            .pairs_out = pairs,
            .max_pairs = max_pairs,
            .pair_count_out = &pair_count,
            .static_bvh = use_bvh ? static_bvh : NULL,
            .static_bucket_flags = use_bvh ? bucket_flags : NULL,
            .static_bucket_flag_count = use_bvh ? bucket_flag_count : 0,
        });

        /* Prevent optimizing away the work. */
        if (pair_count != dynamic_count) {
            fprintf(stderr, "WARN unexpected pair_count=%u (expected %u)\n",
                    pair_count, dynamic_count);
        }
    }

    uint64_t t1 = now_ns();
    return (double)(t1 - t0) / 1e6;
}

static double bench_best_of_3_ms_(bool use_bvh,
                                 const phys_static_bvh_t *static_bvh,
                                 const uint8_t *bucket_flags,
                                 uint32_t bucket_flag_count,
                                 phys_frame_arena_t *frame_arena,
                                 phys_body_t *bodies,
                                 phys_collider_t *colliders,
                                 phys_sphere_t *spheres,
                                 phys_aabb_t *aabbs,
                                 const uint8_t *active,
                                 uint32_t static_count,
                                 uint32_t dynamic_count,
                                 phys_collision_pair_t *pairs,
                                 uint32_t max_pairs,
                                 uint32_t iters) {

    double best = 1e30;
    for (int i = 0; i < 3; ++i) {
        double ms = bench_mode_ms_(use_bvh, static_bvh, bucket_flags, bucket_flag_count,
                                   frame_arena, bodies, colliders, spheres, aabbs, active,
                                   static_count, dynamic_count, pairs, max_pairs, iters);
        if (ms < best) {
            best = ms;
        }
    }
    return best;
}

static int bench_dynamic_vs_static_10000_static_bodies(void) {
    const uint32_t static_count = 10000u;
    const uint32_t dynamic_count = 256u;
    const uint32_t total = static_count + dynamic_count;

    phys_body_t *bodies = calloc(total, sizeof(*bodies));
    phys_collider_t *colliders = calloc(total, sizeof(*colliders));
    phys_sphere_t *spheres = calloc(total, sizeof(*spheres));
    phys_aabb_t *aabbs = calloc(total, sizeof(*aabbs));
    uint8_t *active = calloc(total, sizeof(*active));
    if (!bodies || !colliders || !spheres || !aabbs || !active) {
        free(bodies);
        free(colliders);
        free(spheres);
        free(aabbs);
        free(active);
        return 1;
    }

    setup_scene_(bodies, colliders, spheres, active, static_count, dynamic_count);

    /* Build the static BVH once (startup cost, not per-frame). */
    phys_frame_arena_t static_arena;
    if (phys_frame_arena_init(&static_arena, 32u * 1024u * 1024u) != 0) {
        return 1;
    }

    phys_static_bvh_t static_bvh;
    uint32_t *static_ids = malloc((size_t)static_count * sizeof(*static_ids));
    if (!static_ids) {
        phys_frame_arena_destroy(&static_arena);
        return 1;
    }
    for (uint32_t i = 0; i < static_count; ++i) {
        static_ids[i] = i;
    }

    /* AABBs come from spatial_update (uses the same code path as the tick). */
    phys_frame_arena_t tmp_arena;
    phys_frame_arena_init(&tmp_arena, 16u * 1024u * 1024u);
    phys_spatial_grid_t tmp_grid;
    phys_spatial_grid_init(&tmp_grid, 256u, 2.0f, &tmp_arena);
    phys_stage_spatial_update(&(phys_spatial_update_args_t){
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .boxes = NULL,
        .capsules = NULL,
        .aabbs_out = aabbs,
        .grid_out = &tmp_grid,
        .active = active,
        .body_count = total,
        .exclude_static_from_grid = 0,
    });
    phys_frame_arena_destroy(&tmp_arena);

    phys_static_bvh_build(&static_bvh, aabbs, static_ids, static_count, &static_arena);

    uint8_t *bucket_flags = calloc(256u, sizeof(uint8_t));
    if (!bucket_flags) {
        free(static_ids);
        phys_frame_arena_destroy(&static_arena);
        return 1;
    }
    phys_static_bvh_build_bucket_flags(&static_bvh, 256u, 2.0f, bucket_flags);

    /* Per-frame arena (simulates tick behavior). */
    phys_frame_arena_t frame_arena;
    if (phys_frame_arena_init(&frame_arena, 64u * 1024u * 1024u) != 0) {
        free(bucket_flags);
        free(static_ids);
        phys_frame_arena_destroy(&static_arena);
        return 1;
    }

    const uint32_t iters = 20u;
    const uint32_t max_pairs = dynamic_count * 4u;
    phys_collision_pair_t *pairs = calloc(max_pairs, sizeof(*pairs));
    if (!pairs) {
        phys_frame_arena_destroy(&frame_arena);
        free(bucket_flags);
        free(static_ids);
        phys_frame_arena_destroy(&static_arena);
        return 1;
    }

    double grid_ms = bench_best_of_3_ms_(false, NULL, NULL, 0,
                                        &frame_arena, bodies, colliders, spheres,
                                        aabbs, active, static_count, dynamic_count,
                                        pairs, max_pairs, iters);

    double bvh_ms = bench_best_of_3_ms_(true, &static_bvh, bucket_flags, 256u,
                                       &frame_arena, bodies, colliders, spheres,
                                       aabbs, active, static_count, dynamic_count,
                                       pairs, max_pairs, iters);

    double ratio = (grid_ms > 0.0) ? (bvh_ms / grid_ms) : 1.0;

    fprintf(stderr,
            "OK bench_dynamic_vs_static_10000_static_bodies: grid=%.3fms bvh=%.3fms ratio=%.2fx (target<0.90x)\n",
            grid_ms, bvh_ms, ratio);

    int rc = (ratio < 0.90) ? 0 : 1;

    free(pairs);
    phys_frame_arena_destroy(&frame_arena);

    free(bucket_flags);
    free(static_ids);
    phys_frame_arena_destroy(&static_arena);

    free(bodies);
    free(colliders);
    free(spheres);
    free(aabbs);
    free(active);

    return rc;
}

int main(void) {
    fprintf(stderr, "RUN p091_physics_phase6_benchmarks\n");
    return bench_dynamic_vs_static_10000_static_bodies();
}

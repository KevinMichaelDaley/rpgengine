/**
 * @file tick.c
 * @brief Master tick function — orchestrates all 14 physics pipeline stages.
 *
 * Stages 0-13 execute in order within a substep loop.  Buffer swap
 * occurs at the end of each substep.  The frame arena is reset once
 * at the start of the tick.
 */

#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/physics/tier_classify.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/physics/spatial_update.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/halo_closure.h"
#include "ferrum/physics/aabb_update.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/manifold_build.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/constraint_stage.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island_build.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/island_tier_promote.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/xpbd_solve.h"
#include "ferrum/physics/integrate.h"
#include "ferrum/physics/ccd.h"
#include "ferrum/physics/ccd_dynamic.h"
#include "ferrum/physics/cache_commit.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Number of spatial grid hash buckets (must be power of 2). */
#define GRID_CELL_COUNT 256

/** World-space size of each spatial grid cell. */
#define GRID_CELL_SIZE 2.0f

/** Maximum broadphase pairs per substep. */
#define MAX_PAIRS_PER_SUBSTEP 10000

/* ── Adaptive solver sub-substeps ──────────────────────────────── */

/** Speed (m/s) above which solver sub-substeps kick in. */
#define SUBSUB_SPEED_LOW  5.0f
/** Speed (m/s) at which we reach maximum sub-substeps. */
#define SUBSUB_SPEED_HIGH 40.0f
/** Maximum solver sub-substeps for fast islands. */
#define SUBSUB_MAX        16

/**
 * @brief Compute the number of solver sub-substeps for an island
 *        based on the maximum body speed.
 *
 * Islands where every body is below SUBSUB_SPEED_LOW get 1 sub-substep
 * (the normal path).  Faster islands get up to SUBSUB_MAX, with a
 * sqrt ramp for early onset.
 *
 * @param island     Island to inspect.
 * @param bodies     Body array (read-only).
 * @param body_count Total body count.
 * @return Sub-substep count (1..SUBSUB_MAX).
 */
static uint32_t compute_island_sub_substeps(
    const phys_island_t *island,
    const phys_body_t *bodies,
    uint32_t body_count)
{
    (void)body_count;
    float max_speed_sq = 0.0f;
    for (uint32_t bi = 0; bi < island->body_count; bi++) {
        uint32_t idx = island->body_indices[bi];
        if (bodies[idx].inv_mass == 0.0f) continue;
        phys_vec3_t v = bodies[idx].linear_vel;
        float speed_sq = vec3_dot(v, v);
        if (speed_sq > max_speed_sq) {
            max_speed_sq = speed_sq;
        }
    }

    const float lo2 = SUBSUB_SPEED_LOW  * SUBSUB_SPEED_LOW;
    const float hi2 = SUBSUB_SPEED_HIGH * SUBSUB_SPEED_HIGH;

    if (max_speed_sq <= lo2) { return 1; }
    if (max_speed_sq >= hi2) { return SUBSUB_MAX; }

    float t = (max_speed_sq - lo2) / (hi2 - lo2);
    t = sqrtf(t);
    return 1 + (uint32_t)(t * (float)(SUBSUB_MAX - 1));
}

/**
 * @brief Rebuild joint constraint rows for joints whose bodies are
 *        in the given island, then pack into the constraint array.
 *
 * Only rebuilds joints — contact constraints are left frozen from
 * the narrowphase.
 *
 * @param island       Island whose joints to rebuild.
 * @param constraints  Full constraint array (joint entries overwritten).
 * @param constraint_count  Total constraint count.
 * @param joints       World joint array.
 * @param joint_count  Number of joints.
 * @param bodies       Current body positions.
 * @param dt           Sub-substep dt.
 */
static void rebuild_island_joint_constraints(
    const phys_island_t *island,
    phys_constraint_t *constraints,
    uint32_t constraint_count,
    phys_joint_t *joints,
    uint32_t joint_count,
    const phys_body_t *bodies,
    float dt)
{
    /* For each constraint in this island that is a joint, find
     * the corresponding joint, rebuild its rows, and update the
     * constraint in-place. */
    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t c_idx = island->constraint_indices[ci];
        if (c_idx >= constraint_count) continue;
        phys_constraint_t *c = &constraints[c_idx];
        if (!c->is_joint) continue;

        /* Find the joint by matching body_a/body_b. */
        for (uint32_t ji = 0; ji < joint_count; ji++) {
            phys_joint_t *j = &joints[ji];
            if (j->body_a == c->body_a && j->body_b == c->body_b) {
                /* Rebuild Jacobian rows from updated positions. */
                switch (j->type) {
                case PHYS_JOINT_DISTANCE:
                    phys_joint_build_distance(j, &bodies[j->body_a],
                                               &bodies[j->body_b], dt);
                    break;
                case PHYS_JOINT_BALL:
                    phys_joint_build_ball(j, &bodies[j->body_a],
                                          &bodies[j->body_b], dt);
                    break;
                case PHYS_JOINT_HINGE:
                    phys_joint_build_hinge(j, &bodies[j->body_a],
                                           &bodies[j->body_b], dt);
                    break;
                }
                /* Repack into constraint. */
                phys_constraint_t tmp[2];
                uint32_t written = phys_joint_build_constraints(
                    j, tmp, 2, c->solver_mode);
                if (written >= 1) {
                    *c = tmp[0];
                }
                /* If hinge produced a second constraint, update the
                 * next constraint entry in the island if present. */
                if (written >= 2 && ci + 1 < island->constraint_count) {
                    uint32_t next_idx = island->constraint_indices[ci + 1];
                    if (next_idx < constraint_count &&
                        constraints[next_idx].is_joint) {
                        constraints[next_idx] = tmp[1];
                        ci++; /* skip the second entry */
                    }
                }
                break;
            }
        }
    }
}

static size_t static_bvh_arena_size_bytes(uint32_t item_count) {
    if (item_count == 0) {
        return 0;
    }
    const uint32_t max_nodes = 2u * item_count - 1u;
    /* Build allocates: nodes + indices + build stack.
     * The stack element type is internal, so we over-approximate. */
    return (size_t)max_nodes * sizeof(phys_static_bvh_node_t) +
           (size_t)item_count * sizeof(uint32_t) +
           (size_t)max_nodes * 32u + 4096u;
}

static uint8_t phys_world_try_build_static_bvh(phys_world_t *world,
                                              const uint8_t *active,
                                              uint32_t body_cap) {
    if (!world || world->static_bvh_valid) {
        return 0;
    }

    uint32_t static_count = 0;
    for (uint32_t i = 0; i < body_cap; ++i) {
        if (active && !active[i]) {
            continue;
        }
        if (phys_body_is_static(&world->body_pool.bodies_curr[i])
            && world->colliders[i].type != PHYS_SHAPE_HALFSPACE) {
            static_count++;
        }
    }

    if (static_count == 0) {
        return 0;
    }

    phys_aabb_t *static_aabbs = phys_frame_arena_alloc(
        &world->frame_arena, (size_t)static_count * sizeof(phys_aabb_t),
        _Alignof(phys_aabb_t));
    uint32_t *static_ids = phys_frame_arena_alloc(
        &world->frame_arena, (size_t)static_count * sizeof(uint32_t),
        _Alignof(uint32_t));
    if (!static_aabbs || !static_ids) {
        return 0;
    }

    uint32_t k = 0;
    for (uint32_t i = 0; i < body_cap; ++i) {
        if (active && !active[i]) {
            continue;
        }
        if (!phys_body_is_static(&world->body_pool.bodies_curr[i])) {
            continue;
        }
        if (world->colliders[i].type == PHYS_SHAPE_HALFSPACE) {
            continue;
        }
        static_aabbs[k] = world->aabbs[i];
        static_ids[k] = i;
        k++;
    }

    phys_frame_arena_destroy(&world->static_bvh_arena);

    const size_t arena_bytes = static_bvh_arena_size_bytes(static_count);
    if (arena_bytes == 0) {
        return 0;
    }
    if (phys_frame_arena_init(&world->static_bvh_arena, arena_bytes) != 0) {
        return 0;
    }

    phys_static_bvh_build(&world->static_bvh, static_aabbs, static_ids,
                          static_count, &world->static_bvh_arena);

    if (!world->static_bvh.nodes || world->static_bvh.node_count == 0) {
        phys_frame_arena_destroy(&world->static_bvh_arena);
        world->static_bvh = (phys_static_bvh_t){0};
        return 0;
    }

    free(world->static_bucket_flags);
    world->static_bucket_flags = calloc(GRID_CELL_COUNT, sizeof(uint8_t));
    if (!world->static_bucket_flags) {
        phys_frame_arena_destroy(&world->static_bvh_arena);
        world->static_bvh = (phys_static_bvh_t){0};
        return 0;
    }

    world->static_bucket_flag_count = GRID_CELL_COUNT;
    phys_static_bvh_build_bucket_flags(
        &world->static_bvh, world->static_bucket_flag_count, GRID_CELL_SIZE,
        world->static_bucket_flags);

    world->static_bvh_valid = 1;
    return 1;
}

void phys_world_tick(phys_world_t *world, const phys_game_state_t *game) {
    if (!world) {
        return;
    }

    /* Clear impact events from last frame. */
    world->impact_event_count = 0;

    world->query_grid_valid = 0;

    /* Reset the per-frame arena so all arena pointers are fresh. */
    phys_frame_arena_reset(&world->frame_arena);

    const uint32_t body_cap = world->body_pool.capacity;

    /* ── Stage 0: Step Plan ────────────────────────────────────── */
    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, world, game);

    /* Build active flags array (mirrors body pool activity). */
    uint8_t *active = phys_frame_arena_alloc(&world->frame_arena,
                                             body_cap * sizeof(uint8_t),
                                             _Alignof(uint8_t));
    if (!active && body_cap > 0) {
        return; /* Arena exhausted — skip this tick. */
    }
    for (uint32_t i = 0; i < body_cap; i++) {
        active[i] = world->body_pool.active[i];
    }

    /* ── Stage 1: Tier Classification ──────────────────────────── */
    phys_tier_lists_t tier_lists;
    phys_stage_tier_classify(&(phys_tier_classify_args_t){
        .bodies         = world->body_pool.bodies_curr,
        .active         = active,
        .body_count     = body_cap,
        .game           = game,
        .tier_lists_out = &tier_lists,
        .arena          = &world->frame_arena,
    });

    /* ── Stage 2: Spatial Index Update ─────────────────────────── */
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, GRID_CELL_COUNT, GRID_CELL_SIZE,
                           &world->frame_arena);

    const uint8_t exclude_static_from_grid = world->static_bvh_valid;

    phys_stage_spatial_update(&(phys_spatial_update_args_t){
        .bodies    = world->body_pool.bodies_curr,
        .colliders = world->colliders,
        .spheres   = world->spheres,
        .boxes     = world->boxes,
        .capsules  = world->capsules,
        .meshes    = world->meshes,
        .convex_hulls = world->convex_hulls,
        .halfspaces = world->halfspaces,
        .compounds  = world->compounds,
        .aabbs_out = world->aabbs,
        .grid_out  = &grid,
        .active    = active,
        .body_count = body_cap,
        .exclude_static_from_grid = exclude_static_from_grid,
    });

    /* If the static BVH isn't built yet, try to build it now (after AABBs are
     * computed). If build succeeds, rebuild the grid excluding static bodies so
     * halo closure doesn't see static geometry. */
    if (!exclude_static_from_grid) {
        if (phys_world_try_build_static_bvh(world, active, body_cap)) {
            phys_stage_spatial_update(&(phys_spatial_update_args_t){
                .bodies    = world->body_pool.bodies_curr,
                .colliders = world->colliders,
                .spheres   = world->spheres,
                .boxes     = world->boxes,
                .capsules  = world->capsules,
                .meshes    = world->meshes,
                .convex_hulls = world->convex_hulls,
                .halfspaces = world->halfspaces,
                .compounds  = world->compounds,
                .aabbs_out = world->aabbs,
                .grid_out  = &grid,
                .active    = active,
                .body_count = body_cap,
                .exclude_static_from_grid = 1,
            });
        }
    }

    world->query_grid = grid;
    world->query_grid_valid = 1;

    /* ── Stage 3: Halo Closure (once per tick) ────────────────── */
    phys_stage_halo_closure(&(phys_halo_closure_args_t){
        .bodies          = world->body_pool.bodies_curr,
        .aabbs           = world->aabbs,
        .grid            = &grid,
        .tier_lists      = &tier_lists,
        .velocity_margin = 0.1f,
        .dt              = plan.dt,
        .body_count      = body_cap,
    });

    /* ── Stage 5: Broadphase (once per tick) ───────────────────── */
    /* Collect halfspace body indices — they need a separate broadphase
     * pass since infinite planes have no bounding volume. */
    uint32_t *hs_bodies = NULL;
    uint32_t hs_count = 0;
    if (world->halfspace_count > 0) {
        hs_bodies = phys_frame_arena_alloc(
            &world->frame_arena,
            (size_t)world->halfspace_count * sizeof(uint32_t),
            _Alignof(uint32_t));
        if (hs_bodies) {
            for (uint32_t i = 0; i < body_cap; ++i) {
                if (active && !active[i]) continue;
                if (world->colliders[i].type == PHYS_SHAPE_HALFSPACE) {
                    hs_bodies[hs_count++] = i;
                }
            }
        }
    }

    uint32_t max_pairs = MAX_PAIRS_PER_SUBSTEP;
    if (max_pairs > body_cap * 4) {
        max_pairs = body_cap * 4 > 0 ? body_cap * 4 : 1;
    }
    phys_collision_pair_t *pairs = phys_frame_arena_alloc(
        &world->frame_arena,
        max_pairs * sizeof(phys_collision_pair_t),
        _Alignof(phys_collision_pair_t));
    uint32_t pair_count = 0;

    if (pairs) {
        phys_stage_broadphase(&(phys_broadphase_args_t){
            .bodies         = world->body_pool.bodies_curr,
            .aabbs          = world->aabbs,
            .grid           = &grid,
            .tier_lists     = &tier_lists,
            .static_bvh     = world->static_bvh_valid ? &world->static_bvh : NULL,
            .static_bucket_flags = world->static_bucket_flags,
            .static_bucket_flag_count = world->static_bucket_flag_count,
            .halfspace_bodies = hs_bodies,
            .halfspace_body_count = hs_count,
            .pairs_out      = pairs,
            .max_pairs      = max_pairs,
            .pair_count_out = &pair_count,
        });
    }

    /* ── Per-tier substep loop ─────────────────────────────────── */
    /* Compute max substeps across all tiers.  Only the narrowphase,
     * solver, and integrate stages run multiple times; broadphase and
     * halo stay outside this loop.  Islands whose tier needs fewer
     * substeps are marked skip on later iterations. */
    uint32_t max_substeps = 1;
    uint32_t tier_substep_counts[PHYS_TIER_COUNT];
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        if (!plan.tier_params[t].active) {
            tier_substep_counts[t] = 0; /* Inactive tiers are skipped. */
            continue;
        }
        uint32_t ts = plan.tier_params[t].substeps;
        if (ts == 0) { ts = 1; }
        tier_substep_counts[t] = ts;
        if (ts > max_substeps) { max_substeps = ts; }
    }
    const float substep_dt = plan.dt / (float)max_substeps;

    /* Initialize CCD prev buffer on first tick (no prior snapshot). */
    if (world->tick_count == 0) {
        memcpy(world->body_pool.bodies_ccd_prev, world->body_pool.bodies_curr,
               body_cap * sizeof(phys_body_t));
    }

    /* Clear CONTACT_RESTING flag on all active bodies before substep loop.
     * The flag will be re-set each substep based on contact normals. */
    for (uint32_t i = 0; i < body_cap; i++) {
        if (active[i]) {
            world->body_pool.bodies_curr[i].flags &=
                ~(uint32_t)PHYS_BODY_FLAG_CONTACT_RESTING;
        }
    }

    for (uint32_t sub = 0; sub < max_substeps; sub++) {

        /* ── Stage 4: AABB Update (skip on first substep) ──────── */
        if (sub > 0) {
            phys_stage_aabb_update(&(phys_aabb_update_args_t){
                .bodies     = world->body_pool.bodies_curr,
                .colliders  = world->colliders,
                .spheres    = world->spheres,
                .boxes      = world->boxes,
                .capsules   = world->capsules,
                .meshes     = world->meshes,
                .convex_hulls = world->convex_hulls,
                .halfspaces = world->halfspaces,
                .compounds  = world->compounds,
                .aabbs_out  = world->aabbs,
                .tier_lists = &tier_lists,
            });
        }

        /* ── Stages 6–11: collision response pipeline ──────────── */
        /* In prediction mode (client-side), skip narrowphase through
         * TGS solve.  Bodies integrate under gravity + current velocity
         * only.  The server sends authoritative corrections for any
         * colliding bodies. */
        phys_constraint_t *constraints = NULL;
        uint32_t constraint_count = 0;
        phys_manifold_t *manifolds = NULL;
        uint32_t manifold_count = 0;
        phys_island_list_t islands;
        phys_island_list_init(&islands, &world->frame_arena,
                              body_cap, body_cap);

        phys_velocity_t *velocities = NULL;
        phys_velocity_t *pseudo_velocities = NULL;
        float *body_max_pen = NULL;

        /* Per-body flag: set to 1 for bodies integrated by solver
         * sub-substeps so the main integrate stage skips them. */
        uint8_t *body_sub_substepped = NULL;

        if (!world->prediction_mode) {

        /* ── Stage 5b: Dynamic-dynamic CCD sweep ──────────────── */
        /* Run CCD BEFORE narrowphase so handled pairs can be skipped.
         * Sweep CCD-marked dynamic pairs from prev→curr poses,
         * bisect for TOI, GJK+EPA at TOI, emit manifolds. */
        uint32_t max_ccd_manifolds = pair_count > 0 ? pair_count : 0;
        uint32_t max_manifolds_total = (pair_count > 0 ? pair_count : 1) + max_ccd_manifolds;
        manifolds = phys_frame_arena_alloc(
            &world->frame_arena,
            max_manifolds_total * sizeof(phys_manifold_t),
            _Alignof(phys_manifold_t));
        manifold_count = 0;

        uint8_t *ccd_skip_pair = NULL;
        if (pair_count > 0) {
            ccd_skip_pair = phys_frame_arena_alloc(
                &world->frame_arena,
                pair_count * sizeof(uint8_t),
                _Alignof(uint8_t));
            if (ccd_skip_pair) memset(ccd_skip_pair, 0, pair_count * sizeof(uint8_t));
        }

        if (manifolds && pair_count > 0) {
            phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
                .bodies             = world->body_pool.bodies_curr,
                .colliders          = world->colliders,
                .spheres            = world->spheres,
                .capsules           = world->capsules,
                .boxes              = world->boxes,
                .pairs              = pairs,
                .pair_count         = pair_count,
                .body_count         = body_cap,
                .manifolds_out      = manifolds,
                .manifold_count_out = &manifold_count,
                .max_manifolds      = max_manifolds_total,
                .skip_pair_out      = ccd_skip_pair,
                .joints             = world->joints,
                .joint_count        = world->joint_count,
                .arena              = &world->frame_arena,
                .dt                 = substep_dt,
            });
        }

        /* ── Stage 6: Narrowphase ──────────────────────────────── */
        uint32_t max_candidates = pair_count > 0 ? pair_count : 1;
        phys_contact_candidate_t *candidates = phys_frame_arena_alloc(
            &world->frame_arena,
            max_candidates * sizeof(phys_contact_candidate_t),
            _Alignof(phys_contact_candidate_t));
        uint32_t candidate_count = 0;

        if (candidates && pair_count > 0) {
            phys_stage_narrowphase(&(phys_narrowphase_args_t){
                .bodies              = world->body_pool.bodies_curr,
                .colliders           = world->colliders,
                .spheres             = world->spheres,
                .boxes               = world->boxes,
                .capsules            = world->capsules,
                .meshes              = world->meshes,
                .convex_hulls        = world->convex_hulls,
                .halfspaces          = world->halfspaces,
                .compounds           = world->compounds,
                .pairs               = pairs,
                .pair_count          = pair_count,
                .candidates_out      = candidates,
                .candidate_count_out = &candidate_count,
                .max_candidates      = max_candidates,
                .speculative_margin  = world->config.speculative_margin,
                .skip_pair           = ccd_skip_pair,
            });
        }

        /* ── Stage 7: Manifold Build ───────────────────────────── */
        /* Narrowphase candidates become manifolds.  CCD manifolds
         * are already in manifolds[0..manifold_count-1] from Stage 5b,
         * so manifold_build appends after them. */
        uint32_t narrow_manifold_count = 0;
        if (manifolds && candidate_count > 0) {
            phys_stage_manifold_build(&(phys_manifold_build_args_t){
                .candidates         = candidates,
                .candidate_count    = candidate_count,
                .cache              = &world->manifold_cache,
                .manifolds_out      = manifolds + manifold_count,
                .manifold_count_out = &narrow_manifold_count,
                .max_manifolds      = max_manifolds_total - manifold_count,
                .tick               = world->tick_count,
                .bodies             = world->body_pool.bodies_curr,
            });
            manifold_count += narrow_manifold_count;
        }

        /* ── Stage 8: Stabilization ────────────────────────────── */
        uint32_t hint_count = manifold_count > 0 ? manifold_count : 1;
        phys_stab_hint_t *hints = phys_frame_arena_alloc(
            &world->frame_arena,
            hint_count * sizeof(phys_stab_hint_t),
            _Alignof(phys_stab_hint_t));

        if (hints && manifold_count > 0) {
            phys_stage_stabilization(&(phys_stabilization_args_t){
                .manifolds                  = manifolds,
                .manifold_count             = manifold_count,
                .bodies                     = world->body_pool.bodies_curr,
                .colliders                  = world->colliders,
                .boxes                      = world->boxes,
                .hints_out                  = hints,
                .resting_velocity_threshold = 0.1f,
            });
        }

        /* ── Stage 9: Constraint Build ─────────────────────────── */
        /* Reserve space for contact constraints plus joint constraints.
         * Each joint can produce up to 2 phys_constraint_t entries. */
        uint32_t max_contact_constraints = manifold_count * PHYS_MAX_MANIFOLD_POINTS;
        uint32_t max_joint_constraints = world->joint_count * 2;
        uint32_t max_constraints = max_contact_constraints + max_joint_constraints;
        if (max_constraints == 0) {
            max_constraints = 1;
        }
        constraints = phys_frame_arena_alloc(
            &world->frame_arena,
            max_constraints * sizeof(phys_constraint_t),
            _Alignof(phys_constraint_t));

        if (constraints && manifold_count > 0) {
            phys_stage_constraint_build(&(phys_constraint_build_args_t){
                .manifolds            = manifolds,
                .hints                = hints,
                .manifold_count       = manifold_count,
                .bodies               = world->body_pool.bodies_curr,
                .constraints_out      = constraints,
                .constraint_count_out = &constraint_count,
                .max_constraints      = max_constraints,
                .dt                   = substep_dt,
                .baumgarte            = world->config.baumgarte,
                .slop                 = world->config.slop,
            });
        }

        /* Build joint constraints and append after contact constraints. */
        uint32_t joint_constraint_start = constraint_count;
        if (constraints && world->joint_count > 0) {
            const phys_body_t *bodies = world->body_pool.bodies_curr;
            for (uint32_t ji = 0; ji < world->joint_count; ++ji) {
                phys_joint_t *j = &world->joints[ji];

                /* Build Jacobian rows for this joint type. */
                switch (j->type) {
                case PHYS_JOINT_DISTANCE:
                    phys_joint_build_distance(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                case PHYS_JOINT_BALL:
                    phys_joint_build_ball(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                case PHYS_JOINT_HINGE:
                    phys_joint_build_hinge(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                }

                /* Convert to solver constraints and append. */
                if (constraint_count < max_constraints) {
                    uint32_t remaining = max_constraints - constraint_count;
                    uint32_t written = phys_joint_build_constraints(
                        j, &constraints[constraint_count], remaining, 0);
                    constraint_count += written;
                }
            }
        }

        /* Compute per-body max penetration from constraints for sleep
         * blocking.  Bodies with penetration > slop must stay awake.
         * Also detect contact-resting bodies: any non-joint constraint
         * whose normal row has J_vb.y > 0.5 (upward-facing contact on
         * body B) marks body B as CONTACT_RESTING, and similarly J_va.y
         * < -0.5 marks body A.  This tells the client that gravity is
         * countered by collision support. */
        if (constraints && constraint_count > 0) {
            body_max_pen = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(float),
                _Alignof(float));
            if (body_max_pen) {
                memset(body_max_pen, 0,
                       (body_cap > 0 ? body_cap : 1) * sizeof(float));
                for (uint32_t ci = 0; ci < constraint_count; ci++) {
                    const phys_constraint_t *c = &constraints[ci];
                    float pen = c->penetration;
                    uint32_t a = c->body_a;
                    uint32_t b = c->body_b;
                    if (a < body_cap && pen > body_max_pen[a]) {
                        body_max_pen[a] = pen;
                    }
                    if (b < body_cap && pen > body_max_pen[b]) {
                        body_max_pen[b] = pen;
                    }

                    /* Contact-resting detection: normal row (row 0)
                     * has J_vb = contact_normal (A→B).  If Y component
                     * is strongly upward, the contact opposes gravity. */
                    if (!c->is_joint && c->row_count > 0) {
                        float ny = c->rows[0].J_vb.y;
                        if (ny > 0.5f && b < body_cap) {
                            world->body_pool.bodies_curr[b].flags |=
                                PHYS_BODY_FLAG_CONTACT_RESTING;
                        }
                        if (ny < -0.5f && a < body_cap) {
                            world->body_pool.bodies_curr[a].flags |=
                                PHYS_BODY_FLAG_CONTACT_RESTING;
                        }
                    }
                }
            }
        }

        /* ── Stage 10: Island Build ────────────────────────────── */

        phys_stage_island_build(&(phys_island_build_args_t){
            .constraints      = constraints,
            .constraint_count = constraint_count,
            .bodies           = world->body_pool.bodies_curr,
            .body_count       = body_cap,
            .islands_out      = &islands,
            .arena            = &world->frame_arena,
        });

        /* ── Stage 10b: Island Tier Promotion ──────────────────── */
        /* Promote all dynamic bodies in each island to the
         * highest-fidelity (lowest-numbered) tier in that island.
         * This ensures per-island solver-mode uniformity and
         * correct substep-skip decisions below. */
        phys_stage_island_tier_promote(&(phys_island_tier_promote_args_t){
            .islands          = &islands,
            .bodies           = world->body_pool.bodies_curr,
            .body_count       = body_cap,
            .constraints      = constraints,
            .constraint_count = constraint_count,
        });

        /* Mark islands whose tier needs fewer substeps than the
         * current iteration.  Tier promotion guarantees all bodies
         * in an island share the same tier, so we read the first
         * body's tier to look up the per-tier substep count. */
        if (sub > 0) {
            for (uint32_t i = 0; i < islands.count; i++) {
                phys_island_t *isle = &islands.islands[i];
                if (isle->body_count == 0) { continue; }
                uint8_t tier = world->body_pool.bodies_curr[
                                   isle->body_indices[0]].tier;
                uint32_t tier_subs = plan.tier_params[tier].substeps;
                if (tier_subs == 0) { tier_subs = 1; }
                isle->skip = (sub >= tier_subs);
            }
        }

        /* ── Stage 10c: Solver sub-substeps for fast islands ───── */
        /* Islands with high body velocities get multiple solver+integrate
         * sub-substeps with a finer dt.  Contact constraints stay frozen
         * from the narrowphase; only joint rows are rebuilt each sub-substep
         * to track updated body positions.
         *
         * Integration is done inline per-body (not via phys_stage_integrate)
         * so only island bodies are touched.  The main integrate stage
         * skips these bodies via body_sub_substepped[]. */
        body_sub_substepped = phys_frame_arena_alloc(
            &world->frame_arena,
            (body_cap > 0 ? body_cap : 1) * sizeof(uint8_t),
            _Alignof(uint8_t));
        if (body_sub_substepped) {
            memset(body_sub_substepped, 0,
                   (body_cap > 0 ? body_cap : 1) * sizeof(uint8_t));
        }

        {
            phys_velocity_t *ss_vel = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
                _Alignof(phys_velocity_t));
            phys_velocity_t *ss_pseudo = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
                _Alignof(phys_velocity_t));

            if (ss_vel && ss_pseudo && body_sub_substepped) {
                for (uint32_t ii = 0; ii < islands.count; ii++) {
                    phys_island_t *isle = &islands.islands[ii];
                    if (isle->sleeping || isle->skip) continue;
                    if (isle->body_count == 0) continue;

                    uint32_t nsub = compute_island_sub_substeps(
                        isle, world->body_pool.bodies_curr, body_cap);
                    if (nsub <= 1) continue;

                    float ss_dt = substep_dt / (float)nsub;
                    float vel_damp = world->config.velocity_damping;

                    for (uint32_t ss = 0; ss < nsub; ss++) {

                        /* Rebuild joint rows from current positions
                         * (skip first pass — rows are fresh from build). */
                        if (ss > 0) {
                            rebuild_island_joint_constraints(
                                isle, constraints, constraint_count,
                                world->joints, world->joint_count,
                                world->body_pool.bodies_curr, ss_dt);
                        }

                        /* Init velocity workspace from body state. */
                        for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                            uint32_t idx = isle->body_indices[bi];
                            ss_vel[idx].linear  =
                                world->body_pool.bodies_curr[idx].linear_vel;
                            ss_vel[idx].angular =
                                world->body_pool.bodies_curr[idx].angular_vel;
                            ss_pseudo[idx] = (phys_velocity_t){{0,0,0},{0,0,0}};
                            if (world->body_pool.bodies_curr[idx].inv_mass > 0.0f &&
                                !phys_body_is_sleeping(
                                    &world->body_pool.bodies_curr[idx])) {
                                ss_vel[idx].linear = vec3_add(
                                    ss_vel[idx].linear,
                                    vec3_scale(world->config.gravity, ss_dt));
                            }
                        }

                        /* Wrap island in a single-island list for solver. */
                        phys_island_list_t ss_islands = {
                            .islands  = isle,
                            .count    = 1,
                            .capacity = 1,
                            .parent   = NULL,
                            .rank     = NULL,
                            .uf_size  = body_cap,
                        };

                        phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
                            .islands    = &ss_islands,
                            .constraints = constraints,
                            .bodies     = world->body_pool.bodies_curr,
                            .velocities = ss_vel,
                            .pseudo_velocities = ss_pseudo,
                            .body_count = body_cap,
                            .iterations = plan.solver_iterations,
                            .gravity    = world->config.gravity,
                            .dt         = ss_dt,
                            .tick_dt    = plan.dt,
                            .slop       = world->config.slop,
                            .tier_substep_counts = tier_substep_counts,
                            .frame_arena = &world->frame_arena,
                            .island_color_threshold =
                                world->config.island_color_threshold,
                            .joints      = world->joints,
                            .joint_count = world->joint_count,
                        });

                        /* Integrate island bodies inline (in-place on
                         * bodies_curr so next sub-substep sees updates). */
                        for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                            uint32_t idx = isle->body_indices[bi];
                            phys_body_t *b =
                                &world->body_pool.bodies_curr[idx];

                            if (phys_body_is_static(b) ||
                                phys_body_is_kinematic(b)) {
                                continue;
                            }

                            /* Write solved velocity back. */
                            b->linear_vel  = ss_vel[idx].linear;
                            b->angular_vel = ss_vel[idx].angular;

                            /* Velocity damping. */
                            if (vel_damp < 1.0f && vel_damp > 0.0f) {
                                float d = powf(vel_damp, ss_dt);
                                b->linear_vel  = vec3_scale(b->linear_vel, d);
                                b->angular_vel = vec3_scale(b->angular_vel, d);
                            }

                            /* Velocity clamping. */
                            {
                                float ls = vec3_magnitude(b->linear_vel);
                                if (ls > 100.0f) {
                                    b->linear_vel = vec3_scale(
                                        b->linear_vel, 100.0f / ls);
                                }
                                float as = vec3_magnitude(b->angular_vel);
                                if (as > 50.0f) {
                                    b->angular_vel = vec3_scale(
                                        b->angular_vel, 50.0f / as);
                                }
                            }

                            /* Position integration with pseudo-velocity. */
                            phys_vec3_t int_vel = b->linear_vel;
                            int_vel = vec3_add(int_vel,
                                               ss_pseudo[idx].linear);
                            b->position = vec3_add(
                                b->position,
                                vec3_scale(int_vel, ss_dt));

                            /* Orientation integration via quat derivative. */
                            phys_vec3_t int_ang = b->angular_vel;
                            int_ang = vec3_add(int_ang,
                                               ss_pseudo[idx].angular);
                            phys_quat_t omega_q = {
                                int_ang.x, int_ang.y, int_ang.z, 0.0f
                            };
                            phys_quat_t dq = quat_mul(omega_q,
                                                       b->orientation);
                            float half_dt = 0.5f * ss_dt;
                            b->orientation.x += dq.x * half_dt;
                            b->orientation.y += dq.y * half_dt;
                            b->orientation.z += dq.z * half_dt;
                            b->orientation.w += dq.w * half_dt;
                            b->orientation = quat_normalize_safe(
                                b->orientation, 1e-8f);
                        }

                    }

                    /* Mark bodies so the main integrate stage skips them. */
                    for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                        body_sub_substepped[isle->body_indices[bi]] = 1;
                    }

                    /* Mark skip so main TGS solve doesn't repeat. */
                    isle->skip = true;
                }
            }
        }

        /* ── Stage 11: TGS Solve ───────────────────────────────── */
        velocities = phys_frame_arena_alloc(
            &world->frame_arena,
            (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
            _Alignof(phys_velocity_t));

        /* Allocate pseudo-velocities for split impulse position correction. */
        pseudo_velocities = phys_frame_arena_alloc(
            &world->frame_arena,
            (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
            _Alignof(phys_velocity_t));

        if (velocities) {
            /* Zero-initialize velocities. */
            memset(velocities, 0,
                   (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t));

            phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
                .islands    = &islands,
                .constraints = constraints,
                .bodies     = world->body_pool.bodies_curr,
                .velocities = velocities,
                .pseudo_velocities = pseudo_velocities,
                .body_count = body_cap,
                .iterations = plan.solver_iterations,
                .gravity    = world->config.gravity,
                .dt         = substep_dt,
                .tick_dt    = plan.dt,
                .slop       = world->config.slop,
                .tier_substep_counts = tier_substep_counts,
                .frame_arena = &world->frame_arena,
                .island_color_threshold = world->config.island_color_threshold,
                .joints      = world->joints,
                .joint_count = world->joint_count,
            });

            /* Write back solved lambdas to joint cache for warmstarting. */
            if (world->joint_count > 0 && constraints) {
                uint32_t jci = joint_constraint_start;
                for (uint32_t ji = 0; ji < world->joint_count; ++ji) {
                    phys_joint_t *j = &world->joints[ji];
                    uint32_t num_c = (j->row_count <= PHYS_MAX_CONSTRAINT_ROWS)
                                   ? 1u : 2u;
                    uint8_t row_idx = 0;
                    for (uint32_t ci = 0; ci < num_c && jci < constraint_count; ci++, jci++) {
                        const phys_constraint_t *c = &constraints[jci];
                        for (uint8_t r = 0; r < c->row_count && row_idx < j->row_count; r++, row_idx++) {
                            j->cached_lambda[row_idx] = c->rows[r].lambda;
                        }
                    }
                }
            }
        }

        /* ── Stage 11b: XPBD Solve (T2-T4 islands) ────────────── */
        /* Count XPBD constraints across all non-sleeping islands. */
        uint32_t xpbd_count = 0;
        for (uint32_t i = 0; i < islands.count; ++i) {
            const phys_island_t *isle = &islands.islands[i];
            if (isle->sleeping || isle->skip) { continue; }
            if (isle->constraint_count == 0) { continue; }
            uint32_t first_ci = isle->constraint_indices[0];
            if (constraints[first_ci].solver_mode == PHYS_SOLVER_XPBD) {
                xpbd_count += isle->constraint_count;
            }
        }

        if (xpbd_count > 0 && velocities) {
            /* Gather XPBD constraints into a contiguous arena array. */
            phys_constraint_t *xpbd_constraints = phys_frame_arena_alloc(
                &world->frame_arena,
                xpbd_count * sizeof(phys_constraint_t),
                _Alignof(phys_constraint_t));

            /* Scratch body array for XPBD position solving. */
            phys_body_t *xpbd_bodies = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_body_t),
                _Alignof(phys_body_t));

            /* Scratch velocity array — XPBD derives velocities from
             * position deltas; we merge into the shared array after. */
            phys_velocity_t *xpbd_velocities = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
                _Alignof(phys_velocity_t));

            if (xpbd_constraints && xpbd_bodies && xpbd_velocities) {
                /* Copy XPBD constraints. */
                uint32_t xc = 0;
                for (uint32_t i = 0; i < islands.count; ++i) {
                    const phys_island_t *isle = &islands.islands[i];
                    if (isle->sleeping || isle->skip) { continue; }
                    if (isle->constraint_count == 0) { continue; }
                    uint32_t first_ci = isle->constraint_indices[0];
                    if (constraints[first_ci].solver_mode != PHYS_SOLVER_XPBD) {
                        continue;
                    }
                    for (uint32_t c = 0; c < isle->constraint_count; ++c) {
                        xpbd_constraints[xc++] = constraints[isle->constraint_indices[c]];
                    }
                }

                /* Determine XPBD iterations and compliance from the
                 * highest-fidelity XPBD tier (lowest tier number). */
                uint32_t xpbd_iters = 2;
                float xpbd_compliance = 1e-4f;
                for (int t = PHYS_TIER_2_VISIBLE; t <= PHYS_TIER_4_BACKGROUND; ++t) {
                    if (plan.tier_params[t].solver_mode == PHYS_SOLVER_XPBD &&
                        plan.tier_params[t].iterations > xpbd_iters) {
                        xpbd_iters = plan.tier_params[t].iterations;
                        xpbd_compliance = plan.tier_params[t].compliance;
                    }
                }

                phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
                    .constraints      = xpbd_constraints,
                    .constraint_count = xpbd_count,
                    .bodies_in        = world->body_pool.bodies_curr,
                    .bodies_out       = xpbd_bodies,
                    .velocities_out   = xpbd_velocities,
                    .body_count       = body_cap,
                    .iterations       = xpbd_iters,
                    .omega            = 0.7f,
                    .dt               = substep_dt,
                    .compliance       = xpbd_compliance,
                });

                /* Merge XPBD velocities into shared array for bodies
                 * that belong to XPBD islands.  TGS init_velocities
                 * already seeded gravity for these bodies, so we
                 * add the XPBD-derived delta on top. */
                for (uint32_t i = 0; i < islands.count; ++i) {
                    const phys_island_t *isle = &islands.islands[i];
                    if (isle->sleeping || isle->skip) { continue; }
                    if (isle->constraint_count == 0) { continue; }
                    uint32_t first_ci = isle->constraint_indices[0];
                    if (constraints[first_ci].solver_mode != PHYS_SOLVER_XPBD) {
                        continue;
                    }
                    for (uint32_t b = 0; b < isle->body_count; ++b) {
                        uint32_t idx = isle->body_indices[b];
                        if (idx < body_cap) {
                            velocities[idx] = xpbd_velocities[idx];
                        }
                    }
                }
            }
        }

        } /* end if (!world->prediction_mode) — stages 6–11 */

        /* In prediction mode the solver didn't run, so seed the
         * velocity array from the bodies' current velocities.
         * Apply gravity here since it's normally done in TGS init. */
        phys_velocity_t *pred_velocities = velocities;
        if (!pred_velocities) {
            pred_velocities = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
                _Alignof(phys_velocity_t));
            if (pred_velocities) {
                for (uint32_t i = 0; i < body_cap; ++i) {
                    pred_velocities[i].linear  = world->body_pool.bodies_curr[i].linear_vel;
                    pred_velocities[i].angular = world->body_pool.bodies_curr[i].angular_vel;
                    if (world->body_pool.bodies_curr[i].inv_mass > 0.0f &&
                        !phys_body_is_sleeping(&world->body_pool.bodies_curr[i])) {
                        uint8_t tier = world->body_pool.bodies_curr[i].tier;
                        uint32_t ts = tier_substep_counts[tier];
                        if (ts == 0) { ts = 1; }
                        float body_dt = plan.dt / (float)ts;
                        pred_velocities[i].linear = vec3_add(
                            pred_velocities[i].linear,
                            vec3_scale(world->config.gravity, body_dt));
                    }
                }
            }
        }

        /* ── Stage 12: Integrate ───────────────────────────────── */
        if (pred_velocities) {
            phys_stage_integrate(&(phys_integrate_args_t){
                .bodies_in              = world->body_pool.bodies_curr,
                .velocities             = pred_velocities,
                .pseudo_velocities      = pseudo_velocities,
                .bodies_out             = world->body_pool.bodies_next,
                .body_count             = body_cap,
                .dt                     = substep_dt,
                .tick_dt                = plan.dt,
                .gravity                = world->config.gravity,
                .sleep_threshold_linear = world->config.sleep_threshold_linear,
                .sleep_threshold_angular = world->config.sleep_threshold_angular,
                .sleep_delay_frames     = world->config.sleep_delay_frames,
                .current_substep        = sub,
                .tier_substep_counts    = tier_substep_counts,
                .velocity_damping       = world->config.velocity_damping,
                .max_penetration        = body_max_pen,
                .slop                   = world->config.slop,
                .skip_body              = body_sub_substepped,
            });
        }

        /* ── Stage 12c: CCD (swept sphere vs static mesh) ─────── */
        {
            phys_stage_ccd(&(phys_ccd_args_t){
                .bodies_prev      = world->body_pool.bodies_ccd_prev,
                .bodies_read      = world->body_pool.bodies_curr,
                .bodies_curr      = world->body_pool.bodies_next,
                .colliders        = world->colliders,
                .meshes           = world->meshes,
                .constraints      = constraints,
                .arena            = &world->frame_arena,
                .constraint_count = constraint_count,
                .mesh_count       = world->mesh_count,
                .body_count       = body_cap,
                .dt               = substep_dt,
                .spheres          = world->spheres,
                .capsules         = world->capsules,
                .boxes            = world->boxes,
                .convex_hulls     = world->convex_hulls,
                .compounds        = world->compounds,
                .compound_count   = world->compound_count,
                .auto_ccd_speed   = world->config.auto_ccd_speed,
            });
        }

        /* ── Stage 12b + 13: skipped in prediction mode ───────── */
        if (!world->prediction_mode) {

        /* Position projection and velocity sync are no longer separate
         * stages — they are fused into the TGS solver via split impulse. */

        /* ── Stage 13: Cache Commit ────────────────────────────── */
        if (constraints && constraint_count > 0) {
            phys_stage_cache_commit(&(phys_cache_commit_args_t){
                .manifolds        = manifolds,
                .constraints      = constraints,
                .constraint_count = constraint_count,
                .cache            = &world->manifold_cache,
                .events_out       = world->impact_events,
                .event_count_out  = &world->impact_event_count,
                .max_events       = world->impact_event_capacity,
                .impact_threshold = world->impact_threshold,
                .warmstart_decay  = world->config.warmstart_decay,
            });
        }

        } /* end if (!world->prediction_mode) — stage 13 */

        /* ── Buffer swap for next substep ──────────────────────── */
        phys_body_pool_swap_buffers(&world->body_pool);
    }

    /* CCD prev snapshot is now handled by the 3-way buffer rotation
     * inside swap_buffers — no memcpy needed. */

    /* Increment tick counter. */
    world->tick_count++;

    /* Expire old manifold cache entries (keep for 30 ticks). */
    phys_manifold_cache_expire(&world->manifold_cache,
                               (uint32_t)world->tick_count, 30);
}

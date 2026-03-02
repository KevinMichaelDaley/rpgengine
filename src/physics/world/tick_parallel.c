/**
 * @file tick_parallel.c
 * @brief Parallel tick — dispatches pipeline stages as jobs.
 *
 * Mirrors the exact structure and logic of tick.c but calls _par()
 * variants for parallelizable stages.  Sync stages (step plan, halo
 * closure, AABB update, island build, cache commit) run on the
 * calling thread.
 */

#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/phys_jobs.h"
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
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/integrate.h"
#include "ferrum/physics/ccd.h"
#include "ferrum/physics/ccd_dynamic.h"
#include "ferrum/physics/cache_commit.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* Parallel stage headers. */
#include "ferrum/physics/par/tier_classify_par.h"
#include "ferrum/physics/par/spatial_update_par.h"
#include "ferrum/physics/par/broadphase_par.h"
#include "ferrum/physics/par/collision_fused_par.h"
#include "ferrum/physics/par/tgs_solve_par.h"
#include "ferrum/physics/par/integrate_par.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

/** Number of spatial grid hash buckets (must be power of 2). */
#define GRID_CELL_COUNT 256

/** World-space size of each spatial grid cell. */
#define GRID_CELL_SIZE 2.0f

/** Maximum broadphase pairs per substep. */
#define MAX_PAIRS_PER_SUBSTEP 10000

static size_t static_bvh_arena_size_bytes(uint32_t item_count) {
    if (item_count == 0) {
        return 0;
    }
    const uint32_t max_nodes = 2u * item_count - 1u;
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

void phys_world_tick_parallel(phys_world_t *world,
                              const phys_game_state_t *game,
                              phys_job_context_t *jobs) {
    if (!world) {
        return;
    }

#ifdef TRACY_ENABLE
    TracyCZoneN(z_tick, "Phys.Tick.Total", true);
#endif

    /* With <= 1 worker, the job system provides no parallelism and
     * its scheduling order may differ from the sequential pipeline.
     * Fall back to the sequential tick so results match exactly. */
    if (!jobs || !jobs->job_sys || jobs->job_sys->worker_count <= 1) {
        phys_world_tick(world, game);
        return;
    }

    /* Clear impact events from last frame. */
    world->impact_event_count = 0;

    /* Reset the per-frame arena so all arena pointers are fresh. */
    phys_frame_arena_reset(&world->frame_arena);

    const uint32_t body_cap = world->body_pool.capacity;

    /* ── Stage 0: Step Plan [SYNC] ─────────────────────────────── */
#ifdef TRACY_ENABLE
    TracyCZoneN(z_plan, "Phys.Plan.Computing", true);
#endif
    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, world, game);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_plan);
#endif

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

    /* ── Stage 1: Tier Classification [PARALLEL] ───────────────── */
#ifdef TRACY_ENABLE
    TracyCZoneN(z_tier, "Phys.Tier.Classifying", true);
#endif
    phys_tier_lists_t tier_lists;
    phys_stage_tier_classify_par(&(phys_tier_classify_args_t){
        .bodies         = world->body_pool.bodies_curr,
        .active         = active,
        .body_count     = body_cap,
        .game           = game,
        .tier_lists_out = &tier_lists,
        .arena          = &world->frame_arena,
    }, jobs);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_tier);
#endif

    /* ── Stage 2: Spatial Index Update [PARALLEL] ──────────────── */
#ifdef TRACY_ENABLE
    TracyCZoneN(z_spatial, "Phys.Spatial.Updating", true);
#endif
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, GRID_CELL_COUNT, GRID_CELL_SIZE,
                           &world->frame_arena);

    const uint8_t exclude_static_from_grid = world->static_bvh_valid;

    phys_stage_spatial_update_par(&(phys_spatial_update_args_t){
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
    }, jobs, &world->frame_arena);

    if (!exclude_static_from_grid) {
        if (phys_world_try_build_static_bvh(world, active, body_cap)) {
            phys_stage_spatial_update_par(&(phys_spatial_update_args_t){
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
            }, jobs, &world->frame_arena);
        }
    }
#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_spatial);
#endif

    /* ── Stage 3: Halo Closure [SYNC] (once per tick) ──────────── */
#ifdef TRACY_ENABLE
    TracyCZoneN(z_halo, "Phys.Halo.Closing", true);
#endif
    phys_stage_halo_closure(&(phys_halo_closure_args_t){
        .bodies          = world->body_pool.bodies_curr,
        .aabbs           = world->aabbs,
        .grid            = &grid,
        .tier_lists      = &tier_lists,
        .velocity_margin = 0.1f,
        .dt              = plan.dt,
        .body_count      = body_cap,
    });
#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_halo);
#endif

    /* ── Stage 5: Broadphase [PARALLEL] (once per tick) ────────── */
#ifdef TRACY_ENABLE
    TracyCZoneN(z_broad, "Phys.Broad.FindingPairs", true);
#endif
    /* Collect halfspace body indices for separate broadphase pass. */
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
        phys_stage_broadphase_par(&(phys_broadphase_args_t){
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
        }, jobs, &world->frame_arena);
    }
#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_broad);
#endif

    /* ── Per-tier substep loop ─────────────────────────────────── */
    /* Compute max substeps across all tiers.  Only narrowphase,
     * solver, and integrate run multiple times; broadphase and halo
     * stay outside this loop.  Islands whose tier needs fewer
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

        /* ── Stage 4: AABB Update [SYNC, skip first substep] ──── */
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
        /* Stages 6-11 skipped in prediction mode. */
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

        if (!world->prediction_mode) {

        /* ── Stage 5b: Dynamic-dynamic CCD sweep ──────────────── */
        /* Run CCD BEFORE the fused pipeline so handled pairs can be
         * skipped.  CCD manifolds go into the front of the manifold
         * buffer; the fused pipeline appends after them. */
        uint32_t max_manifolds_base = pair_count > 0 ? pair_count : 1;
        uint32_t max_ccd_manifolds  = pair_count > 0 ? pair_count : 0;
        uint32_t max_manifolds = max_manifolds_base + max_ccd_manifolds;
        manifolds = phys_frame_arena_alloc(
            &world->frame_arena,
            max_manifolds * sizeof(phys_manifold_t),
            _Alignof(phys_manifold_t));
        manifold_count = 0;

        uint32_t max_contact_constraints = max_manifolds * PHYS_MAX_MANIFOLD_POINTS;
        uint32_t max_joint_constraints = world->joint_count * 2;
        uint32_t max_constraints = max_contact_constraints + max_joint_constraints;
        if (max_constraints == 0) max_constraints = 1;
        constraints = phys_frame_arena_alloc(
            &world->frame_arena,
            max_constraints * sizeof(phys_constraint_t),
            _Alignof(phys_constraint_t));
        constraint_count = 0;

        uint8_t *ccd_skip_pair = NULL;
        if (pair_count > 0) {
            ccd_skip_pair = phys_frame_arena_alloc(
                &world->frame_arena,
                pair_count * sizeof(uint8_t),
                _Alignof(uint8_t));
            if (ccd_skip_pair) memset(ccd_skip_pair, 0, pair_count * sizeof(uint8_t));
        }

        uint32_t ccd_manifold_start = 0;
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
                .max_manifolds      = max_manifolds,
                .skip_pair_out      = ccd_skip_pair,
                .joints             = world->joints,
                .joint_count        = world->joint_count,
                .arena              = &world->frame_arena,
                .dt                 = substep_dt,
            });
        }
        uint32_t ccd_manifold_count = manifold_count - ccd_manifold_start;

        /* Build constraints from CCD manifolds (if any). */
        if (constraints && ccd_manifold_count > 0) {
            phys_stab_hint_t *ccd_hints = phys_frame_arena_alloc(
                &world->frame_arena,
                ccd_manifold_count * sizeof(phys_stab_hint_t),
                _Alignof(phys_stab_hint_t));
            if (ccd_hints) {
                memset(ccd_hints, 0,
                       ccd_manifold_count * sizeof(phys_stab_hint_t));
                uint32_t ccd_constraint_count = 0;
                phys_stage_constraint_build(&(phys_constraint_build_args_t){
                    .manifolds            = manifolds + ccd_manifold_start,
                    .hints                = ccd_hints,
                    .manifold_count       = ccd_manifold_count,
                    .bodies               = world->body_pool.bodies_curr,
                    .constraints_out      = constraints + constraint_count,
                    .constraint_count_out = &ccd_constraint_count,
                    .max_constraints      = max_constraints - constraint_count,
                    .dt                   = substep_dt,
                    .baumgarte            = world->config.baumgarte,
                    .slop                 = world->config.slop,
                });
                constraint_count += ccd_constraint_count;
            }
        }

        /* ── Fused collision pipeline: narrow→manifold→stab→constraint ── */
        /* Skip pairs already handled by CCD.  Offset output buffers past
         * CCD entries so the fused pipeline appends rather than overwrites. */
        uint32_t fused_manifold_count = 0;
        uint32_t fused_constraint_count = 0;
        if (manifolds && constraints && pair_count > 0) {
#ifdef TRACY_ENABLE
            TracyCZoneN(z_fused, "Phys.Collision.Fused", true);
#endif
            phys_stage_collision_fused_par(&(phys_collision_fused_args_t){
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
                .speculative_margin  = 0.0f,
                .skip_pair           = ccd_skip_pair,
                .cache               = &world->manifold_cache,
                .tick                = world->tick_count,
                .resting_velocity_threshold = 0.1f,
                .dt                  = substep_dt,
                .baumgarte           = world->config.baumgarte,
                .slop                = world->config.slop,
                .manifolds_out       = manifolds + manifold_count,
                .manifold_count_out  = &fused_manifold_count,
                .max_manifolds       = max_manifolds - manifold_count,
                .constraints_out     = constraints + constraint_count,
                .constraint_count_out = &fused_constraint_count,
                .max_constraints     = max_constraints - constraint_count,
            }, jobs, &world->frame_arena);
            manifold_count += fused_manifold_count;
            constraint_count += fused_constraint_count;
#ifdef TRACY_ENABLE
            TracyCZoneEnd(z_fused);
#endif
        }

        /* Build joint constraints and append after contact constraints. */
        uint32_t joint_constraint_start = constraint_count;
        if (constraints && world->joint_count > 0) {
            const phys_body_t *bodies = world->body_pool.bodies_curr;
            for (uint32_t ji = 0; ji < world->joint_count; ++ji) {
                phys_joint_t *j = &world->joints[ji];

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

                if (constraint_count < max_constraints) {
                    uint32_t remaining = max_constraints - constraint_count;
                    uint32_t written = phys_joint_build_constraints(
                        j, &constraints[constraint_count], remaining, 0);
                    constraint_count += written;
                }
            }
        }

        /* Compute per-body max penetration from constraints for sleep
         * blocking.  Bodies with penetration > slop must stay awake so
         * Baumgarte + position projection can correct the overlap.
         * Also detect contact-resting bodies via upward contact normals. */
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

#ifdef TRACY_ENABLE
        TracyCZoneN(z_island, "Phys.Island.Building", true);
#endif
        phys_stage_island_build(&(phys_island_build_args_t){
            .constraints      = constraints,
            .constraint_count = constraint_count,
            .bodies           = world->body_pool.bodies_curr,
            .body_count       = body_cap,
            .islands_out      = &islands,
            .arena            = &world->frame_arena,
            .max_island_bodies = world->config.max_island_bodies,
        });
#ifdef TRACY_ENABLE
        TracyCZoneEnd(z_island);
#endif

        /* Mark islands whose tier needs fewer substeps than the
         * current iteration.  Tier promotion guarantees all bodies
         * in an island share the same tier. */
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

#ifdef TRACY_ENABLE
            TracyCZoneN(z_tgs, "Phys.Solve.IteratingTGS", true);
#endif
            phys_stage_tgs_solve_par(&(phys_tgs_solve_args_t){
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
            }, jobs, &world->frame_arena);
#ifdef TRACY_ENABLE
            TracyCZoneEnd(z_tgs);
#endif

            /* Write back solved lambdas to joint cache for warmstarting
             * the next substep.  Joint constraints start at
             * joint_constraint_start in the constraint array. */
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
        } /* end if (!world->prediction_mode) */

        /* In prediction mode, seed velocities from bodies' current state
         * so integration preserves server-corrected velocities.
         * Apply gravity here since it's normally done in TGS init. */
        if (!velocities) {
            velocities = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
                _Alignof(phys_velocity_t));
            if (velocities) {
                for (uint32_t i = 0; i < body_cap; ++i) {
                    velocities[i].linear  = world->body_pool.bodies_curr[i].linear_vel;
                    velocities[i].angular = world->body_pool.bodies_curr[i].angular_vel;
                    if (world->body_pool.bodies_curr[i].inv_mass > 0.0f &&
                        !phys_body_is_sleeping(&world->body_pool.bodies_curr[i])) {
                        uint8_t tier = world->body_pool.bodies_curr[i].tier;
                        uint32_t ts = tier_substep_counts[tier];
                        if (ts == 0) { ts = 1; }
                        float body_dt = plan.dt / (float)ts;
                        velocities[i].linear = vec3_add(
                            velocities[i].linear,
                            vec3_scale(world->config.gravity, body_dt));
                    }
                }
            }
        }

        if (velocities) {
#ifdef TRACY_ENABLE
            TracyCZoneN(z_integ, "Phys.Integrate.Stepping", true);
#endif
            phys_stage_integrate_par(&(phys_integrate_args_t){
                .bodies_in              = world->body_pool.bodies_curr,
                .velocities             = velocities,
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
            }, jobs, &world->frame_arena);
#ifdef TRACY_ENABLE
            TracyCZoneEnd(z_integ);
#endif
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

        if (!world->prediction_mode) {

        /* Position projection and velocity sync are no longer separate
         * stages — they are fused into the TGS solver via split impulse.
         * the integrator without polluting body velocities. */

        /* ── Stage 13: Cache Commit [SYNC] ─────────────────────── */
        if (constraints && constraint_count > 0) {
#ifdef TRACY_ENABLE
            TracyCZoneN(z_cache, "Phys.Cache.Committing", true);
#endif
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
#ifdef TRACY_ENABLE
            TracyCZoneEnd(z_cache);
#endif
        }

        } /* end if (!world->prediction_mode) — 13 */

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

#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_tick);
#endif
}

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
#include "ferrum/physics/cache_commit.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

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
        if (phys_body_is_static(&world->body_pool.bodies_curr[i])) {
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
        uint32_t ts = plan.tier_params[t].substeps;
        if (ts == 0) { ts = 1; }
        tier_substep_counts[t] = ts;
        if (ts > max_substeps) { max_substeps = ts; }
    }
    const float substep_dt = plan.dt / (float)max_substeps;

    for (uint32_t sub = 0; sub < max_substeps; sub++) {

        /* ── Stage 4: AABB Update (skip on first substep) ──────── */
        if (sub > 0) {
            phys_stage_aabb_update(&(phys_aabb_update_args_t){
                .bodies     = world->body_pool.bodies_curr,
                .colliders  = world->colliders,
                .spheres    = world->spheres,
                .boxes      = world->boxes,
                .capsules   = world->capsules,
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

        if (!world->prediction_mode) {

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
                .pairs               = pairs,
                .pair_count          = pair_count,
                .candidates_out      = candidates,
                .candidate_count_out = &candidate_count,
                .max_candidates      = max_candidates,
                .speculative_margin  = world->config.speculative_margin,
            });
        }

        /* ── Stage 7: Manifold Build ───────────────────────────── */
        uint32_t max_manifolds = candidate_count > 0 ? candidate_count : 1;
        manifolds = phys_frame_arena_alloc(
            &world->frame_arena,
            max_manifolds * sizeof(phys_manifold_t),
            _Alignof(phys_manifold_t));
        manifold_count = 0;

        if (manifolds && candidate_count > 0) {
            phys_stage_manifold_build(&(phys_manifold_build_args_t){
                .candidates         = candidates,
                .candidate_count    = candidate_count,
                .cache              = &world->manifold_cache,
                .manifolds_out      = manifolds,
                .manifold_count_out = &manifold_count,
                .max_manifolds      = max_manifolds,
                .tick               = world->tick_count,
                .bodies             = world->body_pool.bodies_curr,
            });
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
        uint32_t max_constraints = manifold_count * PHYS_MAX_MANIFOLD_POINTS;
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

        /* Compute per-body max penetration from constraints for sleep
         * blocking.  Bodies with penetration > slop must stay awake. */
        if (constraints && constraint_count > 0) {
            body_max_pen = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(float),
                _Alignof(float));
            if (body_max_pen) {
                memset(body_max_pen, 0,
                       (body_cap > 0 ? body_cap : 1) * sizeof(float));
                for (uint32_t ci = 0; ci < constraint_count; ci++) {
                    float pen = constraints[ci].penetration;
                    uint32_t a = constraints[ci].body_a;
                    uint32_t b = constraints[ci].body_b;
                    if (a < body_cap && pen > body_max_pen[a]) {
                        body_max_pen[a] = pen;
                    }
                    if (b < body_cap && pen > body_max_pen[b]) {
                        body_max_pen[b] = pen;
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
            });
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

    /* Increment tick counter. */
    world->tick_count++;

    /* Expire old manifold cache entries (keep for 30 ticks). */
    phys_manifold_cache_expire(&world->manifold_cache,
                               (uint32_t)world->tick_count, 30);
}

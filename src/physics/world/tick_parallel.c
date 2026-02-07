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
#include "ferrum/physics/position_projection.h"
#include "ferrum/physics/velocity_sync.h"
#include "ferrum/physics/cache_commit.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"

/* Parallel stage headers. */
#include "ferrum/physics/par/tier_classify_par.h"
#include "ferrum/physics/par/spatial_update_par.h"
#include "ferrum/physics/par/broadphase_par.h"
#include "ferrum/physics/par/narrowphase_par.h"
#include "ferrum/physics/par/manifold_build_par.h"
#include "ferrum/physics/par/stabilization_par.h"
#include "ferrum/physics/par/constraint_build_par.h"
#include "ferrum/physics/par/tgs_solve_par.h"
#include "ferrum/physics/par/integrate_par.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Number of spatial grid hash buckets (must be power of 2). */
#define GRID_CELL_COUNT 256

/** World-space size of each spatial grid cell. */
#define GRID_CELL_SIZE 2.0f

/** Maximum broadphase pairs per substep. */
#define MAX_PAIRS_PER_SUBSTEP 10000

void phys_world_tick_parallel(phys_world_t *world,
                              const phys_game_state_t *game,
                              phys_job_context_t *jobs) {
    if (!world || !jobs) {
        return;
    }

    /* Clear impact events from last frame. */
    world->impact_event_count = 0;

    /* Reset the per-frame arena so all arena pointers are fresh. */
    phys_frame_arena_reset(&world->frame_arena);

    const uint32_t body_cap = world->body_pool.capacity;

    /* ── Stage 0: Step Plan [SYNC] ─────────────────────────────── */
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

    /* ── Stage 1: Tier Classification [PARALLEL] ───────────────── */
    phys_tier_lists_t tier_lists;
    phys_stage_tier_classify_par(&(phys_tier_classify_args_t){
        .bodies         = world->body_pool.bodies_curr,
        .active         = active,
        .body_count     = body_cap,
        .game           = game,
        .tier_lists_out = &tier_lists,
        .arena          = &world->frame_arena,
    }, jobs);

    /* ── Stage 2: Spatial Index Update [PARALLEL] ──────────────── */
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, GRID_CELL_COUNT, GRID_CELL_SIZE,
                           &world->frame_arena);

    phys_stage_spatial_update_par(&(phys_spatial_update_args_t){
        .bodies    = world->body_pool.bodies_curr,
        .colliders = world->colliders,
        .spheres   = world->spheres,
        .boxes     = world->boxes,
        .capsules  = world->capsules,
        .aabbs_out = world->aabbs,
        .grid_out  = &grid,
        .active    = active,
        .body_count = body_cap,
    }, jobs);

    /* ── Substep loop ──────────────────────────────────────────── */
    const uint32_t substeps = plan.substeps > 0 ? plan.substeps : 1;
    const float substep_dt = plan.substep_dt > 0.0f
                                 ? plan.substep_dt
                                 : world->config.fixed_dt;

    for (uint32_t sub = 0; sub < substeps; sub++) {
        /* ── Stage 3: Halo Closure [SYNC] ──────────────────────── */
        phys_stage_halo_closure(&(phys_halo_closure_args_t){
            .bodies          = world->body_pool.bodies_curr,
            .aabbs           = world->aabbs,
            .grid            = &grid,
            .tier_lists      = &tier_lists,
            .velocity_margin = 0.1f,
            .dt              = substep_dt,
            .body_count      = body_cap,
        });

        /* ── Stage 4: AABB Update [SYNC, skip first substep] ──── */
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

        /* ── Stage 5: Broadphase [PARALLEL] ────────────────────── */
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
                .pairs_out      = pairs,
                .max_pairs      = max_pairs,
                .pair_count_out = &pair_count,
            }, jobs, &world->frame_arena);
        }

        /* ── Stage 6: Narrowphase [PARALLEL] ───────────────────── */
        uint32_t max_candidates = pair_count > 0 ? pair_count : 1;
        phys_contact_candidate_t *candidates = phys_frame_arena_alloc(
            &world->frame_arena,
            max_candidates * sizeof(phys_contact_candidate_t),
            _Alignof(phys_contact_candidate_t));
        uint32_t candidate_count = 0;

        if (candidates && pair_count > 0) {
            phys_stage_narrowphase_par(&(phys_narrowphase_args_t){
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
            }, jobs);
        }

        /* ── Stage 7: Manifold Build [PARALLEL] ────────────────── */
        uint32_t max_manifolds = candidate_count > 0 ? candidate_count : 1;
        phys_manifold_t *manifolds = phys_frame_arena_alloc(
            &world->frame_arena,
            max_manifolds * sizeof(phys_manifold_t),
            _Alignof(phys_manifold_t));
        uint32_t manifold_count = 0;

        if (manifolds && candidate_count > 0) {
            phys_stage_manifold_build_par(&(phys_manifold_build_args_t){
                .candidates         = candidates,
                .candidate_count    = candidate_count,
                .cache              = &world->manifold_cache,
                .manifolds_out      = manifolds,
                .manifold_count_out = &manifold_count,
                .max_manifolds      = max_manifolds,
                .tick               = world->tick_count,
            }, jobs);
        }

        /* ── Stage 8: Stabilization [PARALLEL] ─────────────────── */
        uint32_t hint_count = manifold_count > 0 ? manifold_count : 1;
        phys_stab_hint_t *hints = phys_frame_arena_alloc(
            &world->frame_arena,
            hint_count * sizeof(phys_stab_hint_t),
            _Alignof(phys_stab_hint_t));

        if (hints && manifold_count > 0) {
            phys_stage_stabilization_par(&(phys_stabilization_args_t){
                .manifolds                  = manifolds,
                .manifold_count             = manifold_count,
                .bodies                     = world->body_pool.bodies_curr,
                .hints_out                  = hints,
                .resting_velocity_threshold = 0.5f,
            }, jobs);
        }

        /* ── Stage 9: Constraint Build [PARALLEL] ──────────────── */
        uint32_t max_constraints = manifold_count * PHYS_MAX_MANIFOLD_POINTS;
        if (max_constraints == 0) {
            max_constraints = 1;
        }
        phys_constraint_t *constraints = phys_frame_arena_alloc(
            &world->frame_arena,
            max_constraints * sizeof(phys_constraint_t),
            _Alignof(phys_constraint_t));
        uint32_t constraint_count = 0;

        if (constraints && manifold_count > 0) {
            phys_stage_constraint_build_par(&(phys_constraint_build_args_t){
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
            }, jobs);
        }

        /* ── Stage 10: Island Build [SYNC] ─────────────────────── */
        phys_island_list_t islands;
        phys_island_list_init(&islands, &world->frame_arena,
                              body_cap, body_cap);

        phys_stage_island_build(&(phys_island_build_args_t){
            .constraints      = constraints,
            .constraint_count = constraint_count,
            .bodies           = world->body_pool.bodies_curr,
            .body_count       = body_cap,
            .islands_out      = &islands,
            .arena            = &world->frame_arena,
        });

        /* ── Stage 11: TGS Solve [PARALLEL] ────────────────────── */
        phys_velocity_t *velocities = phys_frame_arena_alloc(
            &world->frame_arena,
            (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
            _Alignof(phys_velocity_t));

        if (velocities) {
            /* Zero-initialize velocities. */
            memset(velocities, 0,
                   (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t));

            phys_stage_tgs_solve_par(&(phys_tgs_solve_args_t){
                .islands    = &islands,
                .constraints = constraints,
                .bodies     = world->body_pool.bodies_curr,
                .velocities = velocities,
                .body_count = body_cap,
                .iterations = plan.solver_iterations,
            }, jobs);
        }

        /* ── Stage 12: Integrate [PARALLEL] ────────────────────── */
        if (velocities) {
            phys_stage_integrate_par(&(phys_integrate_args_t){
                .bodies_in              = world->body_pool.bodies_curr,
                .velocities             = velocities,
                .bodies_out             = world->body_pool.bodies_next,
                .body_count             = body_cap,
                .dt                     = substep_dt,
                .gravity                = world->config.gravity,
                .sleep_threshold_linear = world->config.sleep_threshold_linear,
                .sleep_threshold_angular = world->config.sleep_threshold_angular,
                .sleep_delay_frames     = world->config.sleep_delay_frames,
            }, jobs);
        }

        /* ── Stage 12b: Position Projection [SYNC per island] ──── */
        if (constraint_count > 0) {
            /* Allocate shared output arrays once for all islands. */
            phys_vec3_t *shared_pos = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_vec3_t),
                _Alignof(phys_vec3_t));
            phys_velocity_t *shared_vel = phys_frame_arena_alloc(
                &world->frame_arena,
                (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t),
                _Alignof(phys_velocity_t));

            for (uint32_t i = 0; i < islands.count; i++) {
                const phys_island_t *isle = &islands.islands[i];

                /* Skip islands where all bodies are in the lowest-priority
                 * tier (TIER_4_BACKGROUND) — Baumgarte is sufficient. */
                bool all_background = true;
                for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                    uint32_t idx = isle->body_indices[bi];
                    if (world->body_pool.bodies_next[idx].tier
                            < PHYS_TIER_4_BACKGROUND) {
                        all_background = false;
                        break;
                    }
                }
                if (all_background) { continue; }

                phys_position_projection_result_t proj_result;
                memset(&proj_result, 0, sizeof(proj_result));

                phys_position_projection(&(phys_position_projection_args_t){
                    .island      = isle,
                    .constraints = constraints,
                    .bodies      = world->body_pool.bodies_next,
                    .body_count  = body_cap,
                    .dt          = substep_dt,
                    .slop        = world->config.slop,
                    .arena       = &world->frame_arena,
                    .result      = &proj_result,
                    .shared_pos_deltas = shared_pos,
                    .shared_vel_deltas = shared_vel,
                });

                if (proj_result.success && proj_result.position_deltas) {
                    /* Apply position corrections to integrated bodies. */
                    for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                        uint32_t idx = isle->body_indices[bi];
                        phys_body_t *body = &world->body_pool.bodies_next[idx];
                        body->position = vec3_add(body->position,
                                                  proj_result.position_deltas[idx]);
                    }

                    /* Sparse GS velocity sync: solve per-island
                     * velocity-level system to match correction velocities
                     * along constraint normals. */
                    phys_velocity_sync_normals(&(phys_velocity_sync_args_t){
                        .island          = isle,
                        .constraints     = constraints,
                        .bodies          = world->body_pool.bodies_next,
                        .position_deltas = proj_result.position_deltas,
                        .dt              = substep_dt,
                    });
                }
            }
        }

        /* ── Stage 13: Cache Commit [SYNC] ─────────────────────── */
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
            });
        }

        /* ── Buffer swap for next substep ──────────────────────── */
        phys_body_pool_swap_buffers(&world->body_pool);
    }

    /* Increment tick counter. */
    world->tick_count++;

    /* Expire old manifold cache entries (keep for 30 ticks). */
    phys_manifold_cache_expire(&world->manifold_cache,
                               (uint32_t)world->tick_count, 30);
}

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
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/xpbd_solve.h"
#include "ferrum/physics/position_projection.h"
#include "ferrum/physics/velocity_sync.h"
#include "ferrum/physics/integrate.h"
#include "ferrum/physics/cache_commit.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Number of spatial grid hash buckets (must be power of 2). */
#define GRID_CELL_COUNT 256

/** World-space size of each spatial grid cell. */
#define GRID_CELL_SIZE 2.0f

/** Maximum broadphase pairs per substep. */
#define MAX_PAIRS_PER_SUBSTEP 10000

void phys_world_tick(phys_world_t *world, const phys_game_state_t *game) {
    if (!world) {
        return;
    }

    /* Clear impact events from last frame. */
    world->impact_event_count = 0;

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
    });

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

        if (velocities) {
            /* Zero-initialize velocities. */
            memset(velocities, 0,
                   (body_cap > 0 ? body_cap : 1) * sizeof(phys_velocity_t));

            phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
                .islands    = &islands,
                .constraints = constraints,
                .bodies     = world->body_pool.bodies_curr,
                .velocities = velocities,
                .body_count = body_cap,
                .iterations = plan.solver_iterations,
                .gravity    = world->config.gravity,
                .dt         = substep_dt,
                .tick_dt    = plan.dt,
                .tier_substep_counts = tier_substep_counts,
            });
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

        /* ── Stage 12b: Position Projection (post-integrate) ───── */
        if (constraint_count > 0) {
            /* Allocate shared output array once for all islands. */
            phys_velocity_t *shared_deltas = phys_frame_arena_alloc(
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
                    .shared_deltas = shared_deltas,
                });

                if (proj_result.success && proj_result.correction_deltas) {
                    /* Apply generalized corrections to integrated bodies. */
                    for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                        uint32_t idx = isle->body_indices[bi];
                        phys_body_t *body = &world->body_pool.bodies_next[idx];
                        const phys_velocity_t *d = &proj_result.correction_deltas[idx];

                        /* Linear position correction. */
                        body->position = vec3_add(body->position, d->linear);

                        /* Angular orientation correction via quaternion
                         * derivative: q += 0.5 * (delta_ang, 0) * q. */
                        float ang_mag = vec3_magnitude(d->angular);
                        if (ang_mag > 1e-8f) {
                            phys_quat_t dq_quat = {
                                d->angular.x, d->angular.y, d->angular.z, 0.0f
                            };
                            phys_quat_t rot = quat_mul(dq_quat, body->orientation);
                            body->orientation.x += 0.5f * rot.x;
                            body->orientation.y += 0.5f * rot.y;
                            body->orientation.z += 0.5f * rot.z;
                            body->orientation.w += 0.5f * rot.w;
                            body->orientation = quat_normalize_safe(
                                body->orientation, 1e-8f);
                        }
                    }

                    /* Sparse GS velocity sync: solve per-island
                     * velocity-level system to match correction velocities
                     * along constraint normals (linear + angular). */
                    phys_velocity_sync_normals(&(phys_velocity_sync_args_t){
                        .island            = isle,
                        .constraints       = constraints,
                        .bodies            = world->body_pool.bodies_next,
                        .correction_deltas = proj_result.correction_deltas,
                        .dt                = substep_dt,
                    });
                }
            }
        }

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

        } /* end if (!world->prediction_mode) — stages 12b–13 */

        /* ── Buffer swap for next substep ──────────────────────── */
        phys_body_pool_swap_buffers(&world->body_pool);
    }

    /* Increment tick counter. */
    world->tick_count++;

    /* Expire old manifold cache entries (keep for 30 ticks). */
    phys_manifold_cache_expire(&world->manifold_cache,
                               (uint32_t)world->tick_count, 30);
}

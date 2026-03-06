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
#include "ferrum/physics/phys_cmd.h"
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
#include "ferrum/physics/phys_mat3.h"
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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

/** Number of spatial grid hash buckets (must be power of 2). */
#define GRID_CELL_COUNT 256

/** World-space size of each spatial grid cell. */
#define GRID_CELL_SIZE 2.0f

/** Maximum broadphase pairs per substep. */
#define MAX_PAIRS_PER_SUBSTEP 500000

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
 * @brief Rebuild joint constraint rows for joints whose bodies belong
 *        to the given island, using updated body positions from inline
 *        integration within sub-substeps.
 *
 * Only joint constraints (is_joint == true) are rebuilt; contact rows
 * from the narrowphase are left unchanged.
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
    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t c_idx = island->constraint_indices[ci];
        if (c_idx >= constraint_count) continue;
        phys_constraint_t *c = &constraints[c_idx];
        if (!c->is_joint) continue;

        for (uint32_t ji = 0; ji < joint_count; ji++) {
            phys_joint_t *j = &joints[ji];
            if (j->body_a == c->body_a && j->body_b == c->body_b) {
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
                case PHYS_JOINT_LOCK:
                    phys_joint_build_lock(j, &bodies[j->body_a],
                                          &bodies[j->body_b], dt);
                    break;
                case PHYS_JOINT_COPY_ROTATION:
                    phys_joint_build_copy_rotation(j, &bodies[j->body_a],
                                                    &bodies[j->body_b], dt);
                    break;
                case PHYS_JOINT_LIMIT_ROTATION:
                    phys_joint_build_limit_rotation(j, &bodies[j->body_a],
                                                     &bodies[j->body_b], dt);
                    break;
                case PHYS_JOINT_LIMIT_POSITION:
                    phys_joint_build_limit_position(j, &bodies[j->body_a],
                                                     &bodies[j->body_b], dt);
                    break;
                case PHYS_JOINT_AIM:
                    phys_joint_build_aim(j, &bodies[j->body_a],
                                         &bodies[j->body_b], dt);
                    break;
                case PHYS_JOINT_IK:
                    /* Dynamically update target pos from target body. */
                    if (j->ik_target_body != UINT32_MAX) {
                        j->ik_target_pos = bodies[j->ik_target_body].position;
                    }
                    phys_joint_build_ik(j, &bodies[j->body_a],
                                        &bodies[j->body_b],
                                        &bodies[j->ik_ee_body], dt);
                    break;
                }
                phys_constraint_t tmp[2];
                uint32_t written = phys_joint_build_constraints(
                    j, tmp, 2, c->solver_mode);
                if (written >= 1) {
                    *c = tmp[0];
                }
                if (written >= 2 && ci + 1 < island->constraint_count) {
                    uint32_t next_idx = island->constraint_indices[ci + 1];
                    if (next_idx < constraint_count &&
                        constraints[next_idx].is_joint) {
                        constraints[next_idx] = tmp[1];
                        ci++;
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

/* ── Sub-substep job context and dispatch ──────────────────────── */

/**
 * @brief Shared context for sub-substep island batch jobs.
 *
 * Islands have disjoint body sets, so parallel jobs can safely write
 * to bodies_curr, inv_inertia_world, ss_vel, ss_pseudo, and
 * body_sub_substepped without synchronization.
 */
typedef struct ss_shared {
    const phys_island_list_t *islands;
    const uint32_t           *island_indices;
    phys_constraint_t        *constraints;
    uint32_t                  constraint_count;
    phys_body_t              *bodies;
    phys_mat3_t              *inv_inertia_world;
    phys_velocity_t          *ss_vel;
    phys_velocity_t          *ss_pseudo;
    uint8_t                  *body_sub_substepped;
    uint32_t                  body_cap;
    float                     substep_dt;
    float                     tick_dt;
    phys_vec3_t               gravity;
    float                     slop;
    float                     velocity_damping;
    uint32_t                  solver_iterations;
    uint32_t                  island_color_threshold;
    const uint32_t           *tier_substep_counts;
    phys_joint_t             *joints;
    uint32_t                  joint_count;
    phys_frame_arena_t       *frame_arena;
} ss_shared_t;

/**
 * @brief Run the full sub-substep loop for a single island.
 */
static void ss_solve_island(const ss_shared_t *s, phys_island_t *isle) {
    uint32_t nsub = compute_island_sub_substeps(
        isle, s->bodies, s->body_cap);
    if (nsub <= 1) return;

    float ss_dt = s->substep_dt / (float)nsub;

    for (uint32_t ss = 0; ss < nsub; ss++) {

        if (ss > 0) {
            /* Recompute world-space inertia for island bodies. */
            for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                uint32_t idx = isle->body_indices[bi];
                const phys_body_t *b = &s->bodies[idx];
                s->inv_inertia_world[idx] =
                    phys_mat3_inv_inertia_world(
                        b->orientation, b->inv_inertia_diag);
            }

            /* Rebuild joint rows from current positions. */
            rebuild_island_joint_constraints(
                isle, s->constraints, s->constraint_count,
                s->joints, s->joint_count, s->bodies, ss_dt);

            /* Recompute effective mass for island constraints. */
            for (uint32_t ci = 0; ci < isle->constraint_count; ci++) {
                uint32_t c_idx = isle->constraint_indices[ci];
                if (c_idx >= s->constraint_count) continue;
                phys_constraint_t *c = &s->constraints[c_idx];
                const phys_mat3_t *iw_a =
                    &s->inv_inertia_world[c->body_a];
                const phys_mat3_t *iw_b =
                    &s->inv_inertia_world[c->body_b];
                float im_a = s->bodies[c->body_a].inv_mass;
                float im_b = s->bodies[c->body_b].inv_mass;
                for (uint32_t ri = 0; ri < c->row_count; ri++) {
                    c->rows[ri].effective_mass =
                        phys_compute_effective_mass(
                            &c->rows[ri], im_a, iw_a, im_b, iw_b);
                }
            }
        }

        /* Init velocity workspace from body state. */
        for (uint32_t bi = 0; bi < isle->body_count; bi++) {
            uint32_t idx = isle->body_indices[bi];
            s->ss_vel[idx].linear  = s->bodies[idx].linear_vel;
            s->ss_vel[idx].angular = s->bodies[idx].angular_vel;
            s->ss_pseudo[idx] = (phys_velocity_t){{0,0,0},{0,0,0}};
            if (s->bodies[idx].inv_mass > 0.0f &&
                !phys_body_is_sleeping(&s->bodies[idx])) {
                s->ss_vel[idx].linear = vec3_add(
                    s->ss_vel[idx].linear,
                    vec3_scale(s->gravity, ss_dt));
            }
        }

        /* Wrap island in a single-island list for solver. */
        phys_island_list_t ss_islands = {
            .islands  = isle,
            .count    = 1,
            .capacity = 1,
            .parent   = NULL,
            .rank     = NULL,
            .uf_size  = s->body_cap,
        };

        phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
            .islands    = &ss_islands,
            .constraints = s->constraints,
            .bodies     = s->bodies,
            .inv_inertia_world = s->inv_inertia_world,
            .velocities = s->ss_vel,
            .pseudo_velocities = s->ss_pseudo,
            .body_count = s->body_cap,
            .iterations = s->solver_iterations,
            .gravity    = s->gravity,
            .dt         = ss_dt,
            .tick_dt    = s->tick_dt,
            .slop       = s->slop,
            .tier_substep_counts = s->tier_substep_counts,
            .frame_arena = s->frame_arena,
            .island_color_threshold = s->island_color_threshold,
            .joints      = s->joints,
            .joint_count = s->joint_count,
        });

        /* Integrate island bodies inline. */
        for (uint32_t bi = 0; bi < isle->body_count; bi++) {
            uint32_t idx = isle->body_indices[bi];
            phys_body_t *b = &s->bodies[idx];

            if (phys_body_is_static(b) || phys_body_is_kinematic(b)) {
                continue;
            }

            b->linear_vel  = s->ss_vel[idx].linear;
            b->angular_vel = s->ss_vel[idx].angular;

            /* Velocity damping. */
            if (s->velocity_damping < 1.0f && s->velocity_damping > 0.0f) {
                float d = powf(s->velocity_damping, ss_dt);
                b->linear_vel  = vec3_scale(b->linear_vel, d);
                b->angular_vel = vec3_scale(b->angular_vel, d);
            }

            /* Velocity clamping. */
            {
                float ls = vec3_magnitude(b->linear_vel);
                if (ls > 100.0f) {
                    b->linear_vel = vec3_scale(b->linear_vel, 100.0f / ls);
                }
                float as = vec3_magnitude(b->angular_vel);
                if (as > 50.0f) {
                    b->angular_vel = vec3_scale(b->angular_vel, 50.0f / as);
                }
            }

            /* Position integration with pseudo-velocity. */
            phys_vec3_t int_vel = vec3_add(b->linear_vel,
                                           s->ss_pseudo[idx].linear);
            b->position = vec3_add(b->position,
                                   vec3_scale(int_vel, ss_dt));

            /* Orientation integration via quat derivative. */
            phys_vec3_t int_ang = vec3_add(b->angular_vel,
                                           s->ss_pseudo[idx].angular);
            phys_quat_t omega_q = {
                int_ang.x, int_ang.y, int_ang.z, 0.0f
            };
            phys_quat_t dq = quat_mul(omega_q, b->orientation);
            float half_dt = 0.5f * ss_dt;
            b->orientation.x += dq.x * half_dt;
            b->orientation.y += dq.y * half_dt;
            b->orientation.z += dq.z * half_dt;
            b->orientation.w += dq.w * half_dt;
            b->orientation = quat_normalize_safe(b->orientation, 1e-8f);
        }
    }

    /* Mark bodies so the main integrate stage skips them. */
    for (uint32_t bi = 0; bi < isle->body_count; bi++) {
        s->body_sub_substepped[isle->body_indices[bi]] = 1;
    }

    /* Mark skip so main TGS solve doesn't repeat. */
    isle->skip = true;
}

/**
 * @brief Job entry point: solve a batch of sub-substep islands.
 */
static void ss_island_batch_job(void *user_data) {
    phys_job_batch_t *batch = (phys_job_batch_t *)user_data;
    const ss_shared_t *s = (const ss_shared_t *)batch->user_args;
    for (uint32_t i = 0; i < batch->count; i++) {
        uint32_t island_idx = s->island_indices[batch->start + i];
        phys_island_t *isle = &s->islands->islands[island_idx];
        ss_solve_island(s, isle);
    }
}

/* ── XPBD parallel job context ────────────────────────────────── */

/**
 * @brief Shared context for batched XPBD constraint fiber jobs.
 *
 * XPBD operates on T2-T4 islands whose body sets are disjoint from
 * T0/T1 islands being solved by TGS, so the two solvers can run
 * concurrently.  Within the XPBD solve, constraints are split into
 * batches and dispatched to multiple fibers.  Jacobi relaxation
 * (omega < 1) ensures convergence with concurrent body writes.
 */
typedef struct xpbd_batch_shared {
    phys_constraint_t *constraints;    /**< XPBD constraint array. */
    phys_body_t       *bodies_out;     /**< Shared body workspace. */
    uint32_t           iterations;     /**< Solver iteration count. */
    float              omega;          /**< Jacobi relaxation factor. */
    float              dt;             /**< Substep timestep. */
    float              compliance;     /**< XPBD compliance. */
} xpbd_batch_shared_t;

/**
 * @brief Fiber job: solve a batch of XPBD constraints.
 *
 * Each fiber processes a contiguous slice of the XPBD constraint array
 * for all iterations.  Concurrent writes to shared body positions are
 * handled by Jacobi relaxation (omega).
 */
static void xpbd_constraint_batch_job(void *user_data) {
    phys_job_batch_t *batch = (phys_job_batch_t *)user_data;
    xpbd_batch_shared_t *s = (xpbd_batch_shared_t *)batch->user_args;

    phys_xpbd_solve_constraint_batch(
        &s->constraints[batch->start], batch->count,
        s->bodies_out, s->iterations,
        s->omega, s->dt, s->compliance);
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

    if (!jobs || !jobs->job_sys) {
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

        /* ── Animation substep callback ────────────────────────── */
        /* Invoked before broadphase/solver so animation code can
         * set kinematic bone positions+orientations.  The solver
         * then sees correct joint anchors. */
        if (world->anim_substep_cb) {
            world->anim_substep_cb(world->anim_substep_user, world,
                                   sub, substep_dt);
        }

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
        uint8_t *body_sub_substepped = NULL;

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
                .exclude_set         = world->collision_exclude.capacity > 0
                                         ? &world->collision_exclude : NULL,
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
                case PHYS_JOINT_LOCK:
                    phys_joint_build_lock(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                case PHYS_JOINT_COPY_ROTATION:
                    phys_joint_build_copy_rotation(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                case PHYS_JOINT_LIMIT_ROTATION:
                    phys_joint_build_limit_rotation(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                case PHYS_JOINT_LIMIT_POSITION:
                    phys_joint_build_limit_position(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                case PHYS_JOINT_AIM:
                    phys_joint_build_aim(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                case PHYS_JOINT_IK:
                    /* Dynamically update target pos from target body. */
                    if (j->ik_target_body != UINT32_MAX) {
                        j->ik_target_pos = bodies[j->ik_target_body].position;
                    }
                    phys_joint_build_ik(j,
                        &bodies[j->body_a], &bodies[j->body_b],
                        &bodies[j->ik_ee_body], substep_dt);
                    break;
                }

                if (constraint_count < max_constraints) {
                    uint32_t remaining = max_constraints - constraint_count;
                    uint8_t jmode = (uint8_t)phys_tier_cross_solver_mode(
                        bodies[j->body_a].tier, bodies[j->body_b].tier);
                    uint32_t written = phys_joint_build_constraints(
                        j, &constraints[constraint_count], remaining, jmode);
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

        /* Precompute world-space inverse inertia tensors. */
        phys_mat3_t *inv_inertia_world = phys_frame_arena_alloc(
            &world->frame_arena,
            (body_cap > 0 ? body_cap : 1) * sizeof(phys_mat3_t),
            _Alignof(phys_mat3_t));
        if (inv_inertia_world) {
            for (uint32_t i = 0; i < body_cap; i++) {
                if (!world->body_pool.active[i]) {
                    inv_inertia_world[i] = (phys_mat3_t){{0}};
                    continue;
                }
                const phys_body_t *b = &world->body_pool.bodies_curr[i];
                inv_inertia_world[i] = phys_mat3_inv_inertia_world(
                    b->orientation, b->inv_inertia_diag);
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
         * skips these bodies via body_sub_substepped[].
         *
         * Dispatched as batched jobs over eligible islands.  Islands have
         * disjoint body sets, so parallel writes to bodies_curr,
         * inv_inertia_world, and velocity arrays are safe. */
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
                /* Collect eligible island indices. */
                uint32_t *ss_island_indices = phys_frame_arena_alloc(
                    &world->frame_arena,
                    (islands.count > 0 ? islands.count : 1) * sizeof(uint32_t),
                    _Alignof(uint32_t));
                uint32_t ss_island_count = 0;
                if (ss_island_indices) {
                    for (uint32_t ii = 0; ii < islands.count; ii++) {
                        phys_island_t *isle = &islands.islands[ii];
                        if (isle->sleeping || isle->skip) continue;
                        if (isle->body_count == 0) continue;
                        uint32_t nsub = compute_island_sub_substeps(
                            isle, world->body_pool.bodies_curr, body_cap);
                        if (nsub <= 1) continue;
                        ss_island_indices[ss_island_count++] = ii;
                    }
                }

                if (ss_island_count > 0) {
                    /* Shared context for sub-substep jobs. */
                    ss_shared_t ss_shared = {
                        .islands            = &islands,
                        .island_indices     = ss_island_indices,
                        .constraints        = constraints,
                        .constraint_count   = constraint_count,
                        .bodies             = world->body_pool.bodies_curr,
                        .inv_inertia_world  = inv_inertia_world,
                        .ss_vel             = ss_vel,
                        .ss_pseudo          = ss_pseudo,
                        .body_sub_substepped = body_sub_substepped,
                        .body_cap           = body_cap,
                        .substep_dt         = substep_dt,
                        .tick_dt            = plan.dt,
                        .gravity            = world->config.gravity,
                        .slop               = world->config.slop,
                        .velocity_damping   = world->config.velocity_damping,
                        .solver_iterations  = plan.solver_iterations,
                        .island_color_threshold =
                            world->config.island_color_threshold,
                        .tier_substep_counts = tier_substep_counts,
                        .joints             = world->joints,
                        .joint_count        = world->joint_count,
                        .frame_arena        = &world->frame_arena,
                    };

                    uint32_t batch_size = phys_batch_size(
                        jobs, ss_island_count, 4, 0);
                    uint32_t num_batches =
                        (ss_island_count + batch_size - 1) / batch_size;
                    phys_job_batch_t *ss_batches = phys_frame_arena_alloc(
                        &world->frame_arena,
                        num_batches * sizeof(phys_job_batch_t),
                        _Alignof(phys_job_batch_t));

                    if (ss_batches) {
                        phys_dispatch_stage(jobs, PHYS_STAGE_TGS_SOLVE,
                                            ss_island_batch_job, &ss_shared,
                                            ss_island_count, batch_size,
                                            ss_batches);
                        phys_wait_stage(jobs, PHYS_STAGE_TGS_SOLVE);
                    } else {
                        /* Arena exhausted — run inline. */
                        phys_job_batch_t inline_batch = {
                            .user_args = &ss_shared,
                            .start = 0,
                            .count = ss_island_count,
                            .batch_idx = 0,
                        };
                        ss_island_batch_job(&inline_batch);
                    }
                }
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

            /* ── Stage 11b: Prepare XPBD Solve (T2-T4 islands) ─────── */
            /* XPBD operates on T2-T4 islands which are disjoint from
             * T0/T1 TGS islands, so we dispatch XPBD as a fiber job
             * BEFORE TGS and let them run concurrently. */
            uint32_t xpbd_count = 0;
            for (uint32_t ii = 0; ii < islands.count; ii++) {
                phys_island_t *isle = &islands.islands[ii];
                if (isle->sleeping || isle->skip || isle->constraint_count == 0) continue;
                uint32_t first_ci = isle->constraint_indices[0];
                if (constraints[first_ci].solver_mode == PHYS_SOLVER_XPBD) {
                    xpbd_count += isle->constraint_count;
                }
            }

            xpbd_batch_shared_t xpbd_shared = {0};
            bool xpbd_dispatched = false;
            uint32_t xpbd_actual_count = 0;

            /* Max batches for XPBD constraint dispatch. */
            #define XPBD_MAX_BATCHES 64
            phys_job_batch_t xpbd_batches[XPBD_MAX_BATCHES];

            if (xpbd_count > 0) {
                /* Gather XPBD constraints into a contiguous arena array. */
                phys_constraint_t *xpbd_constraints = phys_frame_arena_alloc(
                    &world->frame_arena,
                    xpbd_count * sizeof(phys_constraint_t),
                    _Alignof(phys_constraint_t));

                if (xpbd_constraints) {
                    /* Copy XPBD constraints and seed bodies_next with
                     * starting positions for XPBD body indices.  We use
                     * bodies_next as the XPBD position workspace — it's
                     * unused until the integration stage which runs after
                     * both solvers complete. */
                    uint32_t xc = 0;
                    phys_body_t *xpbd_bodies = world->body_pool.bodies_next;
                    for (uint32_t ii = 0; ii < islands.count; ii++) {
                        phys_island_t *isle = &islands.islands[ii];
                        if (isle->sleeping || isle->skip || isle->constraint_count == 0) continue;
                        uint32_t first_ci = isle->constraint_indices[0];
                        if (constraints[first_ci].solver_mode != PHYS_SOLVER_XPBD) {
                            continue;
                        }
                        for (uint32_t c = 0; c < isle->constraint_count && xc < xpbd_count; c++) {
                            xpbd_constraints[xc++] = constraints[isle->constraint_indices[c]];
                        }
                        /* Seed XPBD body positions from bodies_curr. */
                        for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                            uint32_t idx = isle->body_indices[bi];
                            if (idx < body_cap) {
                                xpbd_bodies[idx] = world->body_pool.bodies_curr[idx];
                            }
                        }
                    }
                    xpbd_actual_count = xc;

                    /* Determine XPBD iterations and compliance from the
                     * highest-fidelity XPBD tier (lowest tier number). */
                    uint32_t xpbd_iters = 2;
                    float xpbd_compliance = 1e-4f;
                    for (uint32_t t = 0; t < PHYS_TIER_COUNT; t++) {
                        if (plan.tier_params[t].solver_mode == PHYS_SOLVER_XPBD &&
                            plan.tier_params[t].iterations > xpbd_iters) {
                            xpbd_iters = plan.tier_params[t].iterations;
                            xpbd_compliance = plan.tier_params[t].compliance;
                        }
                    }

                    /* Fill shared context for batched XPBD fiber jobs. */
                    xpbd_shared = (xpbd_batch_shared_t){
                        .constraints = xpbd_constraints,
                        .bodies_out  = xpbd_bodies,
                        .iterations  = xpbd_iters,
                        .omega       = 0.5f,
                        .dt          = substep_dt,
                        .compliance  = xpbd_compliance,
                    };

                    /* Dispatch XPBD constraint batches — runs concurrently
                     * with TGS which operates on disjoint T0/T1 islands. */
                    uint32_t xpbd_bs = phys_batch_size(jobs, xpbd_actual_count,
                                                       4, XPBD_MAX_BATCHES);
                    phys_dispatch_stage(jobs, PHYS_STAGE_XPBD_SOLVE,
                                        xpbd_constraint_batch_job, &xpbd_shared,
                                        xpbd_actual_count, xpbd_bs, xpbd_batches);
                    xpbd_dispatched = true;
                }
            }

            /* ── Stage 11a: TGS Solve (T0/T1 islands) ─────────────── */
#ifdef TRACY_ENABLE
            TracyCZoneN(z_tgs, "Phys.Solve.IteratingTGS", true);
#endif
            phys_stage_tgs_solve_par(&(phys_tgs_solve_args_t){
                .islands    = &islands,
                .constraints = constraints,
                .bodies     = world->body_pool.bodies_curr,
                .inv_inertia_world = inv_inertia_world,
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

            /* Wait for XPBD fibers to finish (usually already done). */
            if (xpbd_dispatched) {
                phys_wait_stage(jobs, PHYS_STAGE_XPBD_SOLVE);

                /* Derive velocities from XPBD position deltas and merge
                 * into the shared velocity array for XPBD-island bodies.
                 * XPBD wrote its solved positions into bodies_next. */
                const phys_body_t *bodies_in = world->body_pool.bodies_curr;
                const phys_body_t *bodies_solved = world->body_pool.bodies_next;
                float inv_dt = (substep_dt > 0.0f) ? 1.0f / substep_dt : 0.0f;
                for (uint32_t ii = 0; ii < islands.count; ii++) {
                    const phys_island_t *isle = &islands.islands[ii];
                    if (isle->sleeping || isle->skip || isle->constraint_count == 0) continue;
                    uint32_t first_ci = isle->constraint_indices[0];
                    if (constraints[first_ci].solver_mode != PHYS_SOLVER_XPBD) continue;
                    for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                        uint32_t idx = isle->body_indices[bi];
                        if (idx < body_cap) {
                            phys_vec3_t dp = vec3_sub(bodies_solved[idx].position,
                                                       bodies_in[idx].position);
                            velocities[idx].linear  = vec3_scale(dp, inv_dt);
                            velocities[idx].angular = bodies_in[idx].angular_vel;
                        }
                    }
                }
            }

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
                .skip_body              = body_sub_substepped,
            }, jobs, &world->frame_arena);
#ifdef TRACY_ENABLE
            TracyCZoneEnd(z_integ);
#endif

            /* ── Animation integrate callback ──────────────────── */
            /* Called after standard integration for dynamic
             * anim-tier bodies that need animation-driven velocity
             * corrections or position blending. */
            if (world->anim_integrate_cb) {
                world->anim_integrate_cb(world->anim_integrate_user,
                                         world, sub, substep_dt);
            }
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

    /* ── Apply deferred mutations (thread-safe) ────────────────── */
    /* If the tick runner staged body mutations (SET_POSITION, etc.),
     * copy the latest physics state into bodies_next, apply the
     * mutations there, then swap.  This publishes commanded state
     * atomically — the network thread reading bodies_curr never sees
     * partially-written fields. */
    if (world->pending_mutations &&
        world->pending_mutations->used > 0) {
        memcpy(world->body_pool.bodies_next,
               world->body_pool.bodies_curr,
               body_cap * sizeof(phys_body_t));
        phys_cmd_apply_mutations(world->pending_mutations,
                                 world->body_pool.bodies_next,
                                 body_cap);
        phys_body_pool_swap_buffers(&world->body_pool);
    }

    /* Snapshot post-tick body positions for next tick's CCD.
     * After the final buffer swap, bodies_curr holds the latest state. */
    memcpy(world->body_pool.bodies_ccd_prev, world->body_pool.bodies_curr,
           body_cap * sizeof(phys_body_t));

    /* Increment tick counter. */
    world->tick_count++;

    /* Expire old manifold cache entries (keep for 30 ticks). */
    phys_manifold_cache_expire(&world->manifold_cache,
                               (uint32_t)world->tick_count, 30);

#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_tick);
#endif
}

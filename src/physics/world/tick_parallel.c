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
#include "ferrum/physics/constraint_rebuild.h"
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
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/physics/collision/halfspace.h"
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
#include <stdio.h>

#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

/* ── Debug substep dump ───────────────────────────────────────────── */

/**
 * @brief Dump body positions, velocities, and joint constraint errors
 *        for every active body in the world.  Called per-substep when
 *        world->debug_substep_dump is non-zero.
 */
static void debug_dump_substep(
    const phys_world_t *world,
    const phys_body_t *bodies,
    const phys_velocity_t *velocities,
    const phys_velocity_t *pseudo_velocities,
    uint32_t body_cap,
    uint32_t tick,
    uint32_t substep,
    const char *label)
{
    fprintf(stderr, "\n=== TICK %u  SUB %u  [%s] ===\n", tick, substep, label);

    /* Dump all active bodies with non-zero inv_mass (dynamic). */
    for (uint32_t i = 0; i < body_cap; i++) {
        const phys_body_t *b = &bodies[i];
        if (b->inv_mass <= 0.0f && !(b->flags & 0x8000u)) continue; /* skip static */
        if (b->inv_mass == 0.0f && b->position.x == 0.0f &&
            b->position.y == 0.0f && b->position.z == 0.0f) continue;
        float speed = sqrtf(b->linear_vel.x * b->linear_vel.x +
                            b->linear_vel.y * b->linear_vel.y +
                            b->linear_vel.z * b->linear_vel.z);
        float aspeed = sqrtf(b->angular_vel.x * b->angular_vel.x +
                             b->angular_vel.y * b->angular_vel.y +
                             b->angular_vel.z * b->angular_vel.z);
        const char *tag = (b->flags & PHYS_BODY_FLAG_NO_BROADPHASE) ? "G"
                        : (b->inv_mass == 0.0f) ? "K" : "C";
        fprintf(stderr, "  b%03u[%s] pos=(%.4f,%.4f,%.4f) vel=(%.3f,%.3f,%.3f)"
                " |v|=%.3f |w|=%.3f tier=%u\n",
                i, tag, b->position.x, b->position.y, b->position.z,
                b->linear_vel.x, b->linear_vel.y, b->linear_vel.z,
                speed, aspeed, b->tier);
        if (velocities) {
            fprintf(stderr, "        solver_v=(%.3f,%.3f,%.3f) solver_w=(%.3f,%.3f,%.3f)\n",
                    velocities[i].linear.x, velocities[i].linear.y, velocities[i].linear.z,
                    velocities[i].angular.x, velocities[i].angular.y, velocities[i].angular.z);
        }
        if (pseudo_velocities) {
            float pv_mag = sqrtf(pseudo_velocities[i].linear.x * pseudo_velocities[i].linear.x +
                                 pseudo_velocities[i].linear.y * pseudo_velocities[i].linear.y +
                                 pseudo_velocities[i].linear.z * pseudo_velocities[i].linear.z);
            if (pv_mag > 1e-6f) {
                fprintf(stderr, "        pseudo_v=(%.3f,%.3f,%.3f) |pv|=%.4f\n",
                        pseudo_velocities[i].linear.x, pseudo_velocities[i].linear.y,
                        pseudo_velocities[i].linear.z, pv_mag);
            }
        }
    }

    /* Dump joint anchor errors. */
    for (uint32_t ji = 0; ji < world->joint_count; ji++) {
        const phys_joint_t *j = &world->joints[ji];
        if (j->body_a >= body_cap || j->body_b >= body_cap) continue;
        const phys_body_t *ba = &bodies[j->body_a];
        const phys_body_t *bb = &bodies[j->body_b];
        phys_vec3_t wa = vec3_add(ba->position,
            quat_rotate_vec3(ba->orientation, j->local_anchor_a));
        phys_vec3_t wb = vec3_add(bb->position,
            quat_rotate_vec3(bb->orientation, j->local_anchor_b));
        phys_vec3_t d = vec3_sub(wa, wb);
        float err = sqrtf(vec3_dot(d, d));
        const char *jt = j->type == 1 ? "BL" : j->type == 2 ? "HN"
                       : j->type == 3 ? "DL" : j->type == 4 ? "CT"
                       : j->type == 10 ? "LK" : "??";
        fprintf(stderr, "  j%02u[%s] b%u→b%u err=%.4f d=(%.4f,%.4f,%.4f)\n",
                ji, jt, j->body_a, j->body_b, err, d.x, d.y, d.z);
    }
    fprintf(stderr, "=== END SUB %u [%s] ===\n", substep, label);
}

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

    /* Sub-substeps are velocity-driven only.  Ghost/joint-only islands
     * at rest get nsub=1 (no sub-substeps); the normal solver + compliance
     * regularization handles joint stability. */
    uint32_t min_subs = 1;

    const float lo2 = SUBSUB_SPEED_LOW  * SUBSUB_SPEED_LOW;
    const float hi2 = SUBSUB_SPEED_HIGH * SUBSUB_SPEED_HIGH;

    uint32_t speed_subs;
    if (max_speed_sq <= lo2) { speed_subs = 1; }
    else if (max_speed_sq >= hi2) { speed_subs = SUBSUB_MAX; }
    else {
        float t = (max_speed_sq - lo2) / (hi2 - lo2);
        t = sqrtf(t);
        speed_subs = 1 + (uint32_t)(t * (float)(SUBSUB_MAX - 1));
    }

    return speed_subs > min_subs ? speed_subs : min_subs;
}

static uint8_t stable_joint_lambda_rows(const phys_joint_t *joint)
{
    if (!joint) {
        return 0;
    }

    switch (joint->type) {
    case PHYS_JOINT_LIMIT_ROTATION:
    case PHYS_JOINT_LIMIT_POSITION:
        return 0;
    case PHYS_JOINT_CONE_TWIST:
        return 3;
    default:
        return joint->row_count;
    }
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
    const uint32_t *constraint_joint_indices,
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

        if (!constraint_joint_indices) continue;
        uint32_t ji = constraint_joint_indices[c_idx];
        if (ji >= joint_count) continue;

        phys_joint_t *j = &joints[ji];
        float saved_lambda[PHYS_JOINT_MAX_ROWS] = {0};
        uint8_t saved_rows = 0;
        uint8_t stable_rows = stable_joint_lambda_rows(j);
        for (uint8_t r = 0; r < c->row_count &&
                            r < stable_rows &&
                            saved_rows < PHYS_JOINT_MAX_ROWS; ++r) {
            saved_lambda[saved_rows++] = c->rows[r].lambda;
        }
        uint32_t next_idx = UINT32_MAX;
        if (ci + 1 < island->constraint_count) {
            next_idx = island->constraint_indices[ci + 1];
            if (next_idx < constraint_count &&
                constraints[next_idx].is_joint &&
                constraint_joint_indices[next_idx] == ji) {
                phys_constraint_t *next_c = &constraints[next_idx];
                for (uint8_t r = 0; r < next_c->row_count &&
                                    saved_rows < stable_rows &&
                                    saved_rows < PHYS_JOINT_MAX_ROWS; ++r) {
                    saved_lambda[saved_rows++] = next_c->rows[r].lambda;
                }
            } else {
                next_idx = UINT32_MAX;
            }
        }

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
            if (j->ik_target_body != UINT32_MAX) {
                j->ik_target_pos = bodies[j->ik_target_body].position;
            }
            phys_joint_build_ik(j, &bodies[j->body_a],
                                &bodies[j->body_b],
                                &bodies[j->ik_ee_body], dt);
            break;
        case PHYS_JOINT_CONE_TWIST:
            phys_joint_build_cone_twist(j, &bodies[j->body_a],
                                        &bodies[j->body_b], dt);
            break;
        }
        phys_constraint_t tmp[2];
        uint32_t written = phys_joint_build_constraints(
            j, tmp, 2, c->solver_mode);
        uint8_t restore_idx = 0;
        for (uint32_t wi = 0; wi < written; ++wi) {
            for (uint8_t r = 0; r < tmp[wi].row_count &&
                                restore_idx < saved_rows; ++r) {
                tmp[wi].rows[r].lambda = saved_lambda[restore_idx++];
            }
        }
        if (written >= 1) {
            *c = tmp[0];
        }
        if (written >= 2 && ci + 1 < island->constraint_count) {
            if (next_idx < constraint_count &&
                constraints[next_idx].is_joint &&
                constraint_joint_indices[next_idx] == ji) {
                constraints[next_idx] = tmp[1];
                ci++;
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
    const uint32_t           *constraint_joint_indices;
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

static bool island_routes_xpbd_(const phys_island_t *island,
                                const phys_constraint_t *constraints,
                                uint32_t constraint_count,
                                const phys_body_t *bodies);

/**
 * @brief Run the full sub-substep loop for a single island.
 */
static void ss_solve_island(const ss_shared_t *s, phys_island_t *isle) {
    if (island_routes_xpbd_(isle, s->constraints, s->constraint_count,
                            s->bodies)) {
        return;
    }

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
                s->constraint_joint_indices,
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

        /* Init velocity workspace from body state.
         * Gravity is NOT applied here — phys_stage_tgs_solve's
         * init_velocities handles it to avoid double-counting. */
        for (uint32_t bi = 0; bi < isle->body_count; bi++) {
            uint32_t idx = isle->body_indices[bi];
            s->ss_vel[idx].linear  = s->bodies[idx].linear_vel;
            s->ss_vel[idx].angular = s->bodies[idx].angular_vel;
            s->ss_pseudo[idx] = (phys_velocity_t){{0,0,0},{0,0,0}};
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
            .tick_dt    = ss_dt,
            .slop       = s->slop,
            .tier_substep_counts = NULL,
            .frame_arena = s->frame_arena,
            .island_color_threshold = s->island_color_threshold,
            .joints      = s->joints,
            .joint_count = s->joint_count,
            .bodies_mut  = s->bodies,
            .inv_inertia_world_mut = s->inv_inertia_world,
            .constraint_joint_indices = s->constraint_joint_indices,
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

            /* Velocity damping via implicit Euler integration.
             *
             * The ODE for force-proportional damping is:
             *   Linear:  dv/dt = -c * inv_mass * v
             *   Angular: dω/dt = -c * ω  (mass-independent for stability)
             *
             * Explicit Euler (old): v_new = v * (1 - c*inv_mass*dt)
             *   UNSTABLE when c*inv_mass*dt > 2 (oscillation/explosion).
             *
             * Implicit Euler (new): v_new = v / (1 + c*inv_mass*dt)
             *   UNCONDITIONALLY STABLE for any c > 0, dt > 0.
             *   Approaches zero smoothly; never reverses or amplifies.
             *
             * Angular damping is mass-independent (no inertia tensor)
             * to avoid instability with thin/light body parts whose
             * inverse inertia can be very large (I_inv > 100). */
            {
                float ld = b->linear_damping;
                float ad = b->angular_damping;
                if (ld == 0.0f && s->velocity_damping > 0.0f &&
                    s->velocity_damping < 1.0f) {
                    ld = 1.0f - s->velocity_damping;
                }
                if (ad == 0.0f && s->velocity_damping > 0.0f &&
                    s->velocity_damping < 1.0f) {
                    ad = 1.0f - s->velocity_damping;
                }
                if (ld > 0.0f) {
                    float lin_factor = 1.0f / (1.0f + ld * b->inv_mass * ss_dt);
                    b->linear_vel.x *= lin_factor;
                    b->linear_vel.y *= lin_factor;
                    b->linear_vel.z *= lin_factor;
                }
                if (ad > 0.0f) {
                    float ang_factor = 1.0f / (1.0f + ad * ss_dt);
                    b->angular_vel.x *= ang_factor;
                    b->angular_vel.y *= ang_factor;
                    b->angular_vel.z *= ang_factor;
                }
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

static void xpbd_disable_contact_constraint_(phys_constraint_t *c)
{
    if (!c) {
        return;
    }

    c->penetration = 0.0f;
    for (uint8_t r = 0; r < 3 && r < PHYS_MAX_CONSTRAINT_ROWS; ++r) {
        c->rows[r].J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        c->rows[r].J_wa = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        c->rows[r].J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        c->rows[r].J_wb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        c->rows[r].effective_mass = 0.0f;
        c->rows[r].bias = 0.0f;
        c->rows[r].lambda = 0.0f;
        c->rows[r].lambda_min = 0.0f;
        c->rows[r].lambda_max = 0.0f;
        c->rows[r].pseudo_lambda = 0.0f;
        c->rows[r].damping = 0.0f;
        c->rows[r].flags = 0;
    }
}

static void xpbd_refresh_halfspace_contact_constraint_(
    phys_constraint_t *c,
    phys_body_t *bodies,
    const phys_collider_t *colliders,
    const phys_sphere_t *spheres,
    const phys_box_t *boxes,
    const phys_capsule_t *capsules,
    const phys_convex_hull_t *convex_hulls,
    const phys_halfspace_t *halfspaces,
    float dt,
    float baumgarte,
    float slop)
{
    if (!c || !bodies || !colliders || !halfspaces) {
        return;
    }

    const uint32_t orig_a = c->body_a;
    const uint32_t orig_b = c->body_b;
    const phys_collider_t *ca = &colliders[orig_a];
    const phys_collider_t *cb = &colliders[orig_b];

    if (ca->type != PHYS_SHAPE_HALFSPACE && cb->type != PHYS_SHAPE_HALFSPACE) {
        return;
    }

    uint32_t body_a = orig_a;
    uint32_t body_b = orig_b;
    const phys_collider_t *c0 = ca;
    const phys_collider_t *c1 = cb;
    phys_vec3_t w0 = phys_collider_world_center(ca, bodies[orig_a].position,
                                                bodies[orig_a].orientation);
    phys_quat_t q0 = phys_collider_world_rotation(ca, bodies[orig_a].orientation);
    phys_vec3_t w1 = phys_collider_world_center(cb, bodies[orig_b].position,
                                                bodies[orig_b].orientation);
    phys_quat_t q1 = phys_collider_world_rotation(cb, bodies[orig_b].orientation);

    if (c0->type > c1->type) {
        body_a = orig_b;
        body_b = orig_a;
        c0 = cb;
        c1 = ca;
        w0 = phys_collider_world_center(cb, bodies[orig_b].position,
                                        bodies[orig_b].orientation);
        q0 = phys_collider_world_rotation(cb, bodies[orig_b].orientation);
        w1 = phys_collider_world_center(ca, bodies[orig_a].position,
                                        bodies[orig_a].orientation);
        q1 = phys_collider_world_rotation(ca, bodies[orig_a].orientation);
    }

    if (c1->type != PHYS_SHAPE_HALFSPACE) {
        return;
    }

    const phys_halfspace_t *hs = &halfspaces[c1->shape_index];
    phys_contact_point_t contacts[4];
    int contact_count = 0;

    switch (c0->type) {
    case PHYS_SHAPE_SPHERE: {
        float r = spheres[c0->shape_index].radius;
        if (phys_sphere_vs_halfspace(w0, r, hs->normal, hs->distance,
                                     0.0f, &contacts[0])) {
            contact_count = 1;
        }
        break;
    }
    case PHYS_SHAPE_BOX: {
        phys_vec3_t he = boxes[c0->shape_index].half_extents;
        contact_count = phys_box_vs_halfspace(w0, q0, he,
                                              hs->normal, hs->distance,
                                              0.0f, contacts, 4);
        break;
    }
    case PHYS_SHAPE_CAPSULE: {
        float r = capsules[c0->shape_index].radius;
        float hh = capsules[c0->shape_index].half_height;
        contact_count = phys_capsule_vs_halfspace(w0, q0, r, hh,
                                                  hs->normal, hs->distance,
                                                  0.0f, contacts, 2);
        break;
    }
    case PHYS_SHAPE_CONVEX: {
        const phys_convex_hull_t *hull = &convex_hulls[c0->shape_index];
        contact_count = phys_convex_hull_vs_halfspace(hull, w0, q0,
                                                      hs->normal, hs->distance,
                                                      0.0f, contacts, 4);
        break;
    }
    default:
        xpbd_disable_contact_constraint_(c);
        return;
    }

    if (contact_count <= 0) {
        xpbd_disable_contact_constraint_(c);
        return;
    }

    int pick = c->point_idx;
    if (pick >= contact_count) {
        pick = contact_count - 1;
    }
    if (pick < 0) {
        pick = 0;
    }

    float lambdas[3] = {0.0f, 0.0f, 0.0f};
    uint8_t old_row_count = c->row_count;
    for (uint8_t r = 0; r < old_row_count && r < 3; ++r) {
        lambdas[r] = c->rows[r].lambda;
    }

    const float friction = c->friction;
    const float restitution = phys_combine_restitution(
        bodies[body_a].restitution, bodies[body_b].restitution);
    const uint8_t solver_mode = c->solver_mode;
    const uint8_t is_joint = c->is_joint;
    const float compliance = c->compliance;
    const float joint_damping = c->joint_damping;
    const uint32_t manifold_idx = c->manifold_idx;
    const uint8_t point_idx = c->point_idx;

    phys_constraint_build_contact(c, &bodies[body_a], &bodies[body_b],
                                  &contacts[pick], friction, restitution,
                                  dt, baumgarte, slop);
    c->body_a = body_a;
    c->body_b = body_b;
    c->solver_mode = solver_mode;
    c->is_joint = is_joint;
    c->compliance = compliance;
    c->joint_damping = joint_damping;
    c->manifold_idx = manifold_idx;
    c->point_idx = point_idx;

    for (uint8_t r = 0; r < c->row_count && r < 3; ++r) {
        c->rows[r].lambda = lambdas[r];
    }
}

static bool island_routes_xpbd_(const phys_island_t *island,
                                const phys_constraint_t *constraints,
                                uint32_t constraint_count,
                                const phys_body_t *bodies)
{
    if (!island || !bodies) {
        return false;
    }

    if (!constraints) {
        return false;
    }

    for (uint32_t ci = 0; ci < island->constraint_count; ++ci) {
        uint32_t idx = island->constraint_indices[ci];
        if (idx < constraint_count &&
            constraints[idx].solver_mode == PHYS_SOLVER_XPBD) {
            return true;
        }
    }

    return false;
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

    /* ── Seed bodies_next from bodies_curr ─────────────────────── */
    /* The substep loop works entirely on bodies_next (the "scratch"
     * buffer).  bodies_curr remains untouched and readable by the
     * render thread without synchronisation.  A single swap at the
     * end of the tick publishes the result. */
    memcpy(world->body_pool.bodies_next, world->body_pool.bodies_curr,
           body_cap * sizeof(phys_body_t));

    /* Clear CONTACT_RESTING flag on all active bodies before substep loop.
     * The flag will be re-set each substep based on contact normals. */
    for (uint32_t i = 0; i < body_cap; i++) {
        if (active[i]) {
            world->body_pool.bodies_next[i].flags &=
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
                .bodies     = world->body_pool.bodies_next,
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
        phys_mat3_t *inv_inertia_world = NULL;
        uint32_t *constraint_joint_indices = NULL;

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
        constraint_joint_indices = phys_frame_arena_alloc(
            &world->frame_arena,
            max_constraints * sizeof(uint32_t),
            _Alignof(uint32_t));
        if (constraint_joint_indices) {
            for (uint32_t i = 0; i < max_constraints; ++i) {
                constraint_joint_indices[i] = UINT32_MAX;
            }
        }

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
                .bodies             = world->body_pool.bodies_next,
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
                .exclude_set        = world->collision_exclude.capacity > 0
                                         ? &world->collision_exclude : NULL,
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
                    .bodies               = world->body_pool.bodies_next,
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
                .bodies              = world->body_pool.bodies_next,
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
            const phys_body_t *bodies = world->body_pool.bodies_next;
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
                case PHYS_JOINT_CONE_TWIST:
                    phys_joint_build_cone_twist(j,
                        &bodies[j->body_a], &bodies[j->body_b], substep_dt);
                    break;
                }

                if (constraint_count < max_constraints) {
                    uint32_t remaining = max_constraints - constraint_count;
                    /* Use tier-based solver mode for all joints,
                     * including ghost bodies (which are now T0/TGS). */
                    uint8_t jmode = (uint8_t)phys_tier_cross_solver_mode(
                        bodies[j->body_a].tier, bodies[j->body_b].tier);
                    uint32_t dst_start = constraint_count;
                    uint32_t written = phys_joint_build_constraints(
                        j, &constraints[dst_start], remaining, jmode);
                    if (constraint_joint_indices) {
                        for (uint32_t slot = 0; slot < written; ++slot) {
                            constraint_joint_indices[dst_start + slot] = ji;
                        }
                    }
                    constraint_count += written;
                }
            }
        }

        {
            static uint32_t contact_dbg_ticks = 0;
            if (contact_dbg_ticks < 24 && sub == 0) {
                uint32_t halfspace_manifold_count = 0;
                uint32_t halfspace_constraint_count = 0;
                const phys_constraint_t *first_halfspace_constraint = NULL;
                const phys_constraint_t *first_contact_constraint = NULL;
                for (uint32_t mi = 0; mi < manifold_count; ++mi) {
                    const phys_manifold_t *m = &manifolds[mi];
                    if (world->colliders[m->body_a].type == PHYS_SHAPE_HALFSPACE ||
                        world->colliders[m->body_b].type == PHYS_SHAPE_HALFSPACE) {
                        halfspace_manifold_count++;
                    }
                }
                for (uint32_t ci = 0; ci < joint_constraint_start && ci < constraint_count; ++ci) {
                    const phys_constraint_t *c = &constraints[ci];
                    if (!first_contact_constraint) {
                        first_contact_constraint = c;
                    }
                    if (world->colliders[c->body_a].type == PHYS_SHAPE_HALFSPACE ||
                        world->colliders[c->body_b].type == PHYS_SHAPE_HALFSPACE) {
                        halfspace_constraint_count++;
                        if (!first_halfspace_constraint) {
                            first_halfspace_constraint = c;
                        }
                    }
                }
                fprintf(stderr,
                        "[CONTACT-DBG] tick=%u sub=%u pairs=%u manifolds=%u halfspace_manifolds=%u "
                        "contact_constraints=%u halfspace_constraints=%u\n",
                        world->tick_count, sub, pair_count, manifold_count,
                        halfspace_manifold_count, joint_constraint_start,
                        halfspace_constraint_count);
                if (first_halfspace_constraint) {
                    const phys_constraint_t *c = first_halfspace_constraint;
                    const phys_jacobian_row_t *row = &c->rows[0];
                    fprintf(stderr,
                            "  [CONTACT-ROW] bodies=%u,%u pen=%.4f bias=%.4f lambda=%.4f "
                            "Jva.y=%.3f Jvb.y=%.3f\n",
                            c->body_a, c->body_b, c->penetration, row->bias,
                            row->lambda, row->J_va.y, row->J_vb.y);
                }
                if (!first_halfspace_constraint && first_contact_constraint) {
                    const phys_constraint_t *c = first_contact_constraint;
                    fprintf(stderr,
                            "  [CONTACT-FIRST] bodies=%u,%u types=%u,%u pen=%.4f "
                            "solver=%u flagsA=0x%x flagsB=0x%x\n",
                            c->body_a, c->body_b,
                            world->colliders[c->body_a].type,
                            world->colliders[c->body_b].type,
                            c->penetration, c->solver_mode,
                            world->body_pool.bodies_next[c->body_a].flags,
                            world->body_pool.bodies_next[c->body_b].flags);
                }
                contact_dbg_ticks++;
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
                            world->body_pool.bodies_next[b].flags |=
                                PHYS_BODY_FLAG_CONTACT_RESTING;
                        }
                        if (ny < -0.5f && a < body_cap) {
                            world->body_pool.bodies_next[a].flags |=
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
            .bodies           = world->body_pool.bodies_next,
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
            .bodies           = world->body_pool.bodies_next,
            .body_count       = body_cap,
            .constraints      = constraints,
            .constraint_count = constraint_count,
        });

        if (constraints) {
            for (uint32_t ii = 0; ii < islands.count; ++ii) {
                phys_island_t *isle = &islands.islands[ii];
                if (isle->sleeping || isle->skip || isle->constraint_count == 0) {
                    continue;
                }
                if (!island_routes_xpbd_(isle, constraints, constraint_count,
                                         world->body_pool.bodies_next)) {
                    continue;
                }
                for (uint32_t ci = 0; ci < isle->constraint_count; ++ci) {
                    uint32_t idx = isle->constraint_indices[ci];
                    if (idx < constraint_count) {
                        constraints[idx].solver_mode =
                            (uint8_t)PHYS_SOLVER_XPBD;
                    }
                }
            }
        }

        /* Mark islands whose tier needs fewer substeps than the
         * current iteration.  Tier promotion guarantees all bodies
         * in an island share the same tier. */
        if (sub > 0) {
            for (uint32_t i = 0; i < islands.count; i++) {
                phys_island_t *isle = &islands.islands[i];
                if (isle->body_count == 0) { continue; }
                uint8_t tier = world->body_pool.bodies_next[
                                   isle->body_indices[0]].tier;
                uint32_t tier_subs = plan.tier_params[tier].substeps;
                if (tier_subs == 0) { tier_subs = 1; }
                isle->skip = (sub >= tier_subs);
            }
        }

        /* Precompute world-space inverse inertia tensors. */
        inv_inertia_world = phys_frame_arena_alloc(
            &world->frame_arena,
            (body_cap > 0 ? body_cap : 1) * sizeof(phys_mat3_t),
            _Alignof(phys_mat3_t));
        if (inv_inertia_world) {
            for (uint32_t i = 0; i < body_cap; i++) {
                if (!world->body_pool.active[i]) {
                    inv_inertia_world[i] = (phys_mat3_t){{0}};
                    continue;
                }
                const phys_body_t *b = &world->body_pool.bodies_next[i];
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
                        if (island_routes_xpbd_(isle, constraints,
                                                constraint_count,
                                                world->body_pool.bodies_next)) {
                            continue;
                        }
                        uint32_t nsub = compute_island_sub_substeps(
                            isle, world->body_pool.bodies_next, body_cap);
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
                        .constraint_joint_indices = constraint_joint_indices,
                        .bodies             = world->body_pool.bodies_next,
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
            /* Zero-initialize velocities and pseudo-velocities. */
            size_t vel_size = (body_cap > 0 ? body_cap : 1)
                            * sizeof(phys_velocity_t);
            memset(velocities, 0, vel_size);
            if (pseudo_velocities) {
                memset(pseudo_velocities, 0, vel_size);
            }

            /* ── Stage 11b: Prepare XPBD Solve (T2-T4 islands) ─────── */
            /* XPBD operates on T2-T4 islands which are disjoint from
             * T0/T1 TGS islands, so we dispatch XPBD as a fiber job
             * BEFORE TGS and let them run concurrently. */
            uint32_t xpbd_count = 0;
            for (uint32_t ii = 0; ii < islands.count; ii++) {
                phys_island_t *isle = &islands.islands[ii];
                if (isle->sleeping || isle->skip || isle->constraint_count == 0) continue;
                if (island_routes_xpbd_(isle, constraints, constraint_count,
                                        world->body_pool.bodies_next)) {
                    xpbd_count += isle->constraint_count;
                }
            }
            {
                static uint32_t xpbd_route_dbg_ticks = 0;
                if (xpbd_route_dbg_ticks < 24 && sub == 0) {
                    uint32_t xpbd_halfspace_constraints = 0;
                    for (uint32_t ii = 0; ii < islands.count; ++ii) {
                        phys_island_t *isle = &islands.islands[ii];
                        if (isle->sleeping || isle->skip || isle->constraint_count == 0) {
                            continue;
                        }
                        if (!island_routes_xpbd_(isle, constraints, constraint_count,
                                                 world->body_pool.bodies_next)) {
                            continue;
                        }
                        for (uint32_t ci = 0; ci < isle->constraint_count; ++ci) {
                            const phys_constraint_t *c =
                                &constraints[isle->constraint_indices[ci]];
                            if (c->is_joint) {
                                continue;
                            }
                            if (world->colliders[c->body_a].type == PHYS_SHAPE_HALFSPACE ||
                                world->colliders[c->body_b].type == PHYS_SHAPE_HALFSPACE) {
                                xpbd_halfspace_constraints++;
                            }
                        }
                    }
                    fprintf(stderr,
                            "[XPBD-ROUTE] tick=%u sub=%u xpbd_count=%u halfspace_constraints=%u islands=%u\n",
                            world->tick_count, sub, xpbd_count,
                            xpbd_halfspace_constraints, islands.count);
                    if (world->tick_count < 4 && islands.count > 0) {
                        phys_island_t *isle = &islands.islands[0];
                        fprintf(stderr,
                                "  [XPBD-ROUTE-DETAIL] bodies=%u constraints=%u route=%d\n",
                                isle->body_count, isle->constraint_count,
                                (int)island_routes_xpbd_(
                                    isle, constraints, constraint_count,
                                    world->body_pool.bodies_next));
                        for (uint32_t bi = 0; bi < isle->body_count && bi < 6; ++bi) {
                            uint32_t idx = isle->body_indices[bi];
                            fprintf(stderr,
                                    "    body[%u] idx=%u tier=%u flags=0x%x\n",
                                    bi, idx, world->body_pool.bodies_next[idx].tier,
                                    world->body_pool.bodies_next[idx].flags);
                        }
                        for (uint32_t ci = 0; ci < isle->constraint_count && ci < 6; ++ci) {
                            uint32_t idx = isle->constraint_indices[ci];
                            fprintf(stderr,
                                    "    constraint[%u] idx=%u solver=%u is_joint=%u\n",
                                    ci, idx, constraints[idx].solver_mode,
                                    constraints[idx].is_joint);
                        }
                    }
                    xpbd_route_dbg_ticks++;
                }
            }
            /* DEBUG: one-shot diagnostic for XPBD routing */
            {
                static int dbg_once = 0;
                if (!dbg_once && sub == 0) {
                    dbg_once = 1;
                    fprintf(stderr, "[XPBD-DBG] islands=%u xpbd_count=%u constraint_count=%u\n",
                        islands.count, xpbd_count, constraint_count);
                    for (uint32_t ii = 0; ii < islands.count && ii < 5; ii++) {
                        phys_island_t *isle = &islands.islands[ii];
                        if (isle->sleeping || isle->skip) continue;
                        uint32_t first_ci = isle->constraint_count > 0 ? isle->constraint_indices[0] : UINT32_MAX;
                        uint8_t smode = first_ci < constraint_count ? constraints[first_ci].solver_mode : 255;
                        fprintf(stderr, "  island[%u]: bodies=%u constraints=%u solver_mode=%u\n",
                            ii, isle->body_count, isle->constraint_count, smode);
                        for (uint32_t bi = 0; bi < isle->body_count && bi < 3; bi++) {
                            uint32_t idx = isle->body_indices[bi];
                            const phys_body_t *b = &world->body_pool.bodies_next[idx];
                            fprintf(stderr, "    body[%u] idx=%u tier=%u flags=0x%x inv_mass=%.3f\n",
                                bi, idx, b->tier, b->flags, b->inv_mass);
                        }
                    }
                }
            }

            xpbd_batch_shared_t xpbd_shared = {0};
            bool xpbd_dispatched = false;
            uint32_t xpbd_actual_count = 0;
            uint32_t xpbd_body_count = 0;
            uint32_t *xpbd_body_indices = NULL;
            phys_vec3_t *xpbd_start_pos = NULL;
            phys_quat_t *xpbd_start_orient = NULL;

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
                    uint32_t *xpbd_constraint_source_indices =
                        phys_frame_arena_alloc(
                            &world->frame_arena,
                            xpbd_count * sizeof(uint32_t),
                            _Alignof(uint32_t));
                    uint32_t *xpbd_constraint_joint_indices =
                        phys_frame_arena_alloc(
                            &world->frame_arena,
                            xpbd_count * sizeof(uint32_t),
                            _Alignof(uint32_t));

                    /* Collect XPBD body indices for substep integration. */
                    xpbd_body_indices = phys_frame_arena_alloc(
                        &world->frame_arena,
                        (body_cap > 0 ? body_cap : 1) * sizeof(uint32_t),
                        _Alignof(uint32_t));
                    uint8_t *is_xpbd_body = phys_frame_arena_alloc(
                        &world->frame_arena,
                        (body_cap > 0 ? body_cap : 1) * sizeof(uint8_t),
                        _Alignof(uint8_t));
                    if (is_xpbd_body) {
                        memset(is_xpbd_body, 0,
                               (body_cap > 0 ? body_cap : 1) * sizeof(uint8_t));
                    }

                    /* Map joint-owned constraint slots so rebuilt joint rows
                     * overwrite their original XPBD slots while contact rows
                     * remain intact throughout the solve. */
                    uint32_t xpbd_joint_count = 0;
                    uint32_t *xpbd_joint_indices = phys_frame_arena_alloc(
                        &world->frame_arena,
                        (world->joint_count > 0 ? world->joint_count : 1)
                            * sizeof(uint32_t),
                        _Alignof(uint32_t));
                    uint32_t *xpbd_joint_temp_start = phys_frame_arena_alloc(
                        &world->frame_arena,
                        (world->joint_count > 0 ? world->joint_count : 1)
                            * sizeof(uint32_t),
                        _Alignof(uint32_t));
                    const phys_body_t *bodies_c = world->body_pool.bodies_next;

                    if (xpbd_joint_temp_start) {
                        for (uint32_t ji = 0; ji < world->joint_count; ++ji) {
                            xpbd_joint_temp_start[ji] = UINT32_MAX;
                        }
                    }
                    for (uint32_t ii = 0; ii < islands.count; ii++) {
                        phys_island_t *isle = &islands.islands[ii];
                        if (isle->sleeping || isle->skip || isle->constraint_count == 0) continue;
                        if (!island_routes_xpbd_(isle, constraints, constraint_count,
                                                 world->body_pool.bodies_next)) {
                            continue;
                        }
                        for (uint32_t c = 0; c < isle->constraint_count && xc < xpbd_count; c++) {
                            uint32_t src_idx = isle->constraint_indices[c];
                            xpbd_constraints[xc] = constraints[src_idx];
                            if (xpbd_constraint_source_indices) {
                                xpbd_constraint_source_indices[xc] = src_idx;
                            }
                            if (xpbd_constraint_joint_indices) {
                                xpbd_constraint_joint_indices[xc] =
                                    (constraint_joint_indices && src_idx < constraint_count)
                                    ? constraint_joint_indices[src_idx]
                                    : UINT32_MAX;
                            }
                            if (xpbd_joint_temp_start &&
                                constraint_joint_indices &&
                                xpbd_constraints[xc].is_joint &&
                                src_idx < constraint_count) {
                                uint32_t ji = constraint_joint_indices[src_idx];
                                if (ji < world->joint_count &&
                                    xpbd_joint_temp_start[ji] == UINT32_MAX) {
                                    xpbd_joint_temp_start[ji] = xc;
                                }
                            }
                            xc++;
                        }
                        /* Seed XPBD body positions from bodies_curr and
                         * record which bodies are XPBD-managed. */
                        for (uint32_t bi = 0; bi < isle->body_count; bi++) {
                            uint32_t idx = isle->body_indices[bi];
                            if (idx < body_cap) {
                                xpbd_bodies[idx] = bodies_c[idx];
                                if (is_xpbd_body && !is_xpbd_body[idx]) {
                                    is_xpbd_body[idx] = 1;
                                    if (xpbd_body_indices) {
                                        xpbd_body_indices[xpbd_body_count++] = idx;
                                    }
                                }
                            }
                        }
                    }
                    xpbd_actual_count = xc;

                    /* Identify the joints whose constraint rows were copied
                     * into the XPBD workspace. */
                    if (xpbd_joint_indices && xpbd_joint_temp_start &&
                        world->joint_count > 0) {
                        for (uint32_t ji = 0; ji < world->joint_count; ji++) {
                            if (xpbd_joint_temp_start[ji] != UINT32_MAX) {
                                xpbd_joint_indices[xpbd_joint_count++] = ji;
                            }
                        }
                    }

                    /* The dedicated anim-tier XPBD settings drive the
                     * sub-substep solve for all XPBD-routed islands. */
                    uint32_t xpbd_iters =
                        plan.tier_params[PHYS_TIER_ANIM].iterations;
                    float xpbd_compliance =
                        plan.tier_params[PHYS_TIER_ANIM].compliance;
                    /* Apply configurable minimum compliance floor so the
                     * spectral radius of the XPBD iteration stays below 1.
                     * Without this, stiff coupled chains diverge. */
                    if (xpbd_compliance < world->config.xpbd_min_compliance) {
                        xpbd_compliance = world->config.xpbd_min_compliance;
                    }
                    uint32_t xpbd_substeps =
                        plan.tier_params[PHYS_TIER_ANIM].substeps;
                    if (xpbd_iters == 0) {
                        xpbd_iters = plan.solver_iterations;
                    }
                    if (xpbd_substeps == 0) {
                        xpbd_substeps = 1;
                    }

                    /* Save pre-XPBD positions/orientations so we can
                     * derive final velocities for the shared velocity
                     * array after all XPBD substeps complete. */
                    xpbd_start_pos = phys_frame_arena_alloc(
                        &world->frame_arena,
                        xpbd_body_count * sizeof(phys_vec3_t),
                        _Alignof(phys_vec3_t));
                    xpbd_start_orient = phys_frame_arena_alloc(
                        &world->frame_arena,
                        xpbd_body_count * sizeof(phys_quat_t),
                        _Alignof(phys_quat_t));
                    for (uint32_t bi = 0; bi < xpbd_body_count; bi++) {
                        uint32_t idx = xpbd_body_indices[bi];
                        if (xpbd_start_pos)
                            xpbd_start_pos[bi] = xpbd_bodies[idx].position;
                        if (xpbd_start_orient)
                            xpbd_start_orient[bi] = xpbd_bodies[idx].orientation;
                    }

                    /* XPBD substepping: subdivide the substep_dt into
                     * multiple sub-substeps.  Each sub-substep:
                     *   1. Save pre-predict positions
                     *   2. Predict with gravity (semi-implicit Euler)
                     *   3. Rebuild joints from current body positions
                     *   4. Solve constraints in parallel (dispatch+wait)
                     *   5. Derive velocities from position deltas
                     * This gives much better convergence for long chains
                     * than doing many iterations on a single large dt. */
                    float xpbd_sub_dt = substep_dt / (float)xpbd_substeps;

                    /* Allocate scratch for pre-predict positions so we
                     * can derive velocities after each substep. */
                    phys_vec3_t *xpbd_prev_pos = phys_frame_arena_alloc(
                        &world->frame_arena,
                        xpbd_body_count * sizeof(phys_vec3_t),
                        _Alignof(phys_vec3_t));
                    phys_quat_t *xpbd_prev_orient = phys_frame_arena_alloc(
                        &world->frame_arena,
                        xpbd_body_count * sizeof(phys_quat_t),
                        _Alignof(phys_quat_t));

                    for (uint32_t xsub = 0; xsub < xpbd_substeps; xsub++) {
                        /* 1. Save pre-predict state and predict with
                         *    gravity (semi-implicit Euler). */
                        for (uint32_t bi = 0; bi < xpbd_body_count; bi++) {
                            uint32_t idx = xpbd_body_indices[bi];
                            phys_body_t *b = &xpbd_bodies[idx];
                            if (xpbd_prev_pos) xpbd_prev_pos[bi] = b->position;
                            if (xpbd_prev_orient) xpbd_prev_orient[bi] = b->orientation;
                            if (b->inv_mass <= 0.0f) continue;
                            if (!(b->flags & PHYS_BODY_FLAG_NO_GRAVITY)) {
                                b->linear_vel = vec3_add(b->linear_vel,
                                    vec3_scale(world->config.gravity, xpbd_sub_dt));
                            }
                            b->position = vec3_add(b->position,
                                vec3_scale(b->linear_vel, xpbd_sub_dt));
                            /* Predict orientation: q' = q + 0.5 * (ω,0) * q * dt */
                            phys_vec3_t w = b->angular_vel;
                            float wmag = sqrtf(w.x*w.x + w.y*w.y + w.z*w.z);
                            if (wmag > 1e-8f) {
                                phys_quat_t wq = { w.x, w.y, w.z, 0.0f };
                                phys_quat_t dq = quat_mul(wq, b->orientation);
                                b->orientation.x += 0.5f * dq.x * xpbd_sub_dt;
                                b->orientation.y += 0.5f * dq.y * xpbd_sub_dt;
                                b->orientation.z += 0.5f * dq.z * xpbd_sub_dt;
                                b->orientation.w += 0.5f * dq.w * xpbd_sub_dt;
                                b->orientation = quat_normalize_safe(
                                    b->orientation, 1e-8f);
                            }
                        }
                        /* DEBUG: dump first few body positions pre-solve */
                        {
                            static uint32_t pre_solve_dbg = 0;
                            if (pre_solve_dbg < 3 && sub == 0 && xsub == 0) {
                                pre_solve_dbg++;
                                fprintf(stderr, "[XPBD-PRE] tick=%u xpbd_body_count=%u compliance=%.6f sub_dt=%.6f\n",
                                    world->tick_count, xpbd_body_count, xpbd_compliance, xpbd_sub_dt);
                                uint32_t show = xpbd_body_count < 8 ? xpbd_body_count : 8;
                                for (uint32_t bi = 0; bi < show; bi++) {
                                    uint32_t idx = xpbd_body_indices[bi];
                                    phys_body_t *b = &xpbd_bodies[idx];
                                    float vlen = sqrtf(b->linear_vel.x*b->linear_vel.x +
                                                       b->linear_vel.y*b->linear_vel.y +
                                                       b->linear_vel.z*b->linear_vel.z);
                                    float wlen = sqrtf(b->angular_vel.x*b->angular_vel.x +
                                                       b->angular_vel.y*b->angular_vel.y +
                                                       b->angular_vel.z*b->angular_vel.z);
                                    fprintf(stderr, "  b[%u] idx=%u pos=(%.3f,%.3f,%.3f) |v|=%.3f |w|=%.3f im=%.4f\n",
                                        bi, idx, b->position.x, b->position.y, b->position.z,
                                        vlen, wlen, b->inv_mass);
                                }
                            }
                        }

                        /* 2-3. Solve loop: rebuild joints + solve for
                         *       each iteration so C is always fresh. */
                        for (uint32_t xiter = 0; xiter < xpbd_iters; xiter++) {
                            for (uint32_t ci = 0; ci < xpbd_actual_count; ++ci) {
                                if (xpbd_constraints[ci].is_joint) {
                                    continue;
                                }
                                xpbd_refresh_halfspace_contact_constraint_(
                                    &xpbd_constraints[ci],
                                    xpbd_bodies,
                                    world->colliders,
                                    world->spheres,
                                    world->boxes,
                                    world->capsules,
                                    world->convex_hulls,
                                    world->halfspaces,
                                    xpbd_sub_dt,
                                    world->config.baumgarte,
                                    world->config.slop);
                            }

                            /* Rebuild XPBD joints from current body
                             * positions so biases reflect updated state. */
                            for (uint32_t jj = 0; jj < xpbd_joint_count; jj++) {
                                uint32_t ji = xpbd_joint_indices[jj];
                                phys_joint_t *j = &world->joints[ji];
                                uint32_t temp_start = xpbd_joint_temp_start
                                    ? xpbd_joint_temp_start[ji] : UINT32_MAX;
                                if (temp_start == UINT32_MAX ||
                                    temp_start >= xpbd_actual_count) {
                                    continue;
                                }

                                float saved_lambda[PHYS_JOINT_MAX_ROWS] = {0};
                                uint8_t saved_rows = 0;
                                uint8_t stable_rows = stable_joint_lambda_rows(j);
                                phys_constraint_t *saved_c0 =
                                    &xpbd_constraints[temp_start];
                                for (uint8_t r = 0; r < saved_c0->row_count &&
                                                    r < stable_rows &&
                                                    saved_rows < PHYS_JOINT_MAX_ROWS; ++r) {
                                    saved_lambda[saved_rows++] = saved_c0->rows[r].lambda;
                                }
                                uint32_t second_idx = temp_start + 1;
                                if (second_idx < xpbd_actual_count &&
                                    xpbd_constraints[second_idx].is_joint &&
                                    xpbd_constraint_joint_indices &&
                                    xpbd_constraint_joint_indices[second_idx] == ji) {
                                    phys_constraint_t *saved_c1 =
                                        &xpbd_constraints[second_idx];
                                    for (uint8_t r = 0; r < saved_c1->row_count &&
                                                        saved_rows < stable_rows &&
                                                        saved_rows < PHYS_JOINT_MAX_ROWS; ++r) {
                                        saved_lambda[saved_rows++] = saved_c1->rows[r].lambda;
                                    }
                                }

                                switch (j->type) {
                                case PHYS_JOINT_DISTANCE:
                                    phys_joint_build_distance(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_BALL:
                                    phys_joint_build_ball(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_HINGE:
                                    phys_joint_build_hinge(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_LOCK:
                                    phys_joint_build_lock(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_COPY_ROTATION:
                                    phys_joint_build_copy_rotation(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_LIMIT_ROTATION:
                                    phys_joint_build_limit_rotation(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_LIMIT_POSITION:
                                    phys_joint_build_limit_position(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_AIM:
                                    phys_joint_build_aim(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_IK:
                                    if (j->ik_target_body != UINT32_MAX) {
                                        j->ik_target_pos =
                                            xpbd_bodies[j->ik_target_body].position;
                                    }
                                    phys_joint_build_ik(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b],
                                        &xpbd_bodies[j->ik_ee_body], xpbd_sub_dt);
                                    break;
                                case PHYS_JOINT_CONE_TWIST:
                                    phys_joint_build_cone_twist(j,
                                        &xpbd_bodies[j->body_a],
                                        &xpbd_bodies[j->body_b], xpbd_sub_dt);
                                    break;
                                }

                                /* Rebuild joint rows in place so contact
                                 * constraints stay live for the XPBD solve. */
                                uint32_t remaining = xpbd_actual_count - temp_start;
                                uint32_t written = phys_joint_build_constraints(
                                    j, &xpbd_constraints[temp_start], remaining,
                                    (uint8_t)PHYS_SOLVER_XPBD);
                                uint8_t restore_idx = 0;
                                for (uint32_t wi = 0; wi < written; ++wi) {
                                    phys_constraint_t *dst =
                                        &xpbd_constraints[temp_start + wi];
                                    for (uint8_t r = 0; r < dst->row_count &&
                                                        restore_idx < saved_rows; ++r) {
                                        dst->rows[r].lambda = saved_lambda[restore_idx++];
                                    }
                                }
                            }

                            /* Solve 1 iteration in parallel. */
                            xpbd_shared = (xpbd_batch_shared_t){
                                .constraints = xpbd_constraints,
                                .bodies_out  = xpbd_bodies,
                                .iterations  = 1,
                                .omega       = 0.7f,
                                .dt          = xpbd_sub_dt,
                                .compliance  = xpbd_compliance,
                            };

                            xpbd_batches[0] = (phys_job_batch_t){
                                .user_args = &xpbd_shared,
                                .start = 0,
                                .count = xpbd_actual_count,
                                .batch_idx = 0,
                            };
                            xpbd_constraint_batch_job(&xpbd_batches[0]);

                            /* DEBUG: per-iteration max joint error */
                            {
                                static uint32_t iter_dbg_ticks = 0;
                                if (iter_dbg_ticks < 3 && sub == 0 && xsub == 0) {
                                    float max_bias = 0.0f;
                                    uint32_t max_ci = 0;
                                    for (uint32_t ci = 0; ci < xpbd_actual_count; ci++) {
                                        phys_constraint_t *cc = &xpbd_constraints[ci];
                                        if (!cc->is_joint) continue;
                                        for (uint8_t rr = 0; rr < cc->row_count; rr++) {
                                            float ab = fabsf(cc->rows[rr].bias);
                                            if (ab > max_bias) { max_bias = ab; max_ci = ci; }
                                        }
                                    }
                                    fprintf(stderr, "  [XPBD-ITER] tick=%u iter=%u max_joint_bias=%.6f ci=%u "
                                            "ba=%u bb=%u rows=%u lambda0=%.4f\n",
                                            world->tick_count, xiter, max_bias, max_ci,
                                            xpbd_constraints[max_ci].body_a,
                                            xpbd_constraints[max_ci].body_b,
                                            xpbd_constraints[max_ci].row_count,
                                            xpbd_constraints[max_ci].rows[0].lambda);
                                    if (xiter == xpbd_iters - 1) iter_dbg_ticks++;
                                }
                            }
                        } /* end iteration loop */

                        /* DEBUG: post-solve body positions */
                        {
                            static uint32_t post_solve_dbg = 0;
                            if (post_solve_dbg < 3 && sub == 0 && xsub == 0) {
                                post_solve_dbg++;
                                uint32_t show = xpbd_body_count < 5 ? xpbd_body_count : 5;
                                for (uint32_t bi = 0; bi < show; bi++) {
                                    uint32_t idx = xpbd_body_indices[bi];
                                    phys_body_t *b = &xpbd_bodies[idx];
                                    fprintf(stderr, "  [XPBD-POST] b[%u] idx=%u pos=(%.3f,%.3f,%.3f)\n",
                                        bi, idx, b->position.x, b->position.y, b->position.z);
                                }
                            }
                        }

                        /* 4. Derive velocities from position/orientation
                         *    deltas: v = (x_solved - x_prev) / sub_dt. */
                        float inv_sub_dt = (xpbd_sub_dt > 0.0f)
                                         ? 1.0f / xpbd_sub_dt : 0.0f;
                        for (uint32_t bi = 0; bi < xpbd_body_count; bi++) {
                            uint32_t idx = xpbd_body_indices[bi];
                            phys_body_t *b = &xpbd_bodies[idx];
                            if (b->inv_mass <= 0.0f) continue;

                            /* Linear velocity from position delta. */
                            if (xpbd_prev_pos) {
                                phys_vec3_t dp = vec3_sub(b->position,
                                                          xpbd_prev_pos[bi]);
                                b->linear_vel = vec3_scale(dp, inv_sub_dt);
                            }

                            /* Angular velocity from orientation delta. */
                            if (xpbd_prev_orient) {
                                phys_quat_t qi_conj = quat_conjugate(
                                    xpbd_prev_orient[bi]);
                                phys_quat_t dq = quat_mul(b->orientation,
                                                           qi_conj);
                                float sign = (dq.w >= 0.0f) ? 1.0f : -1.0f;
                                b->angular_vel = (phys_vec3_t){
                                    2.0f * dq.x * sign * inv_sub_dt,
                                    2.0f * dq.y * sign * inv_sub_dt,
                                    2.0f * dq.z * sign * inv_sub_dt
                                };
                            }

                            /* Light damping to prevent energy gain.
                             * Use implicit Euler: v /= (1 + c*dt) for
                             * unconditional stability (explicit Euler
                             * v *= (1-c*dt) can oscillate/go negative
                             * when c*dt > 1). */
                            if (world->config.velocity_damping > 0.0f) {
                                float damp = 1.0f / (1.0f + world->config.velocity_damping * xpbd_sub_dt);
                                b->linear_vel = vec3_scale(b->linear_vel, damp);
                                b->angular_vel = vec3_scale(b->angular_vel, damp);
                            }
                        }
                    }

                    /* Propagate solved lambdas and rebuilt joint rows back to
                     * the main constraint array for cache commit/warmstart. */
                    if (xpbd_constraint_source_indices && constraints) {
                        for (uint32_t i = 0; i < xpbd_actual_count; ++i) {
                            uint32_t src_idx = xpbd_constraint_source_indices[i];
                            if (src_idx < constraint_count) {
                                constraints[src_idx] = xpbd_constraints[i];
                            }
                        }

                        {
                            static uint32_t contact_solved_dbg_ticks = 0;
                            if (contact_solved_dbg_ticks < 24 && sub == 0) {
                                for (uint32_t i = 0; i < xpbd_actual_count; ++i) {
                                    const phys_constraint_t *c = &xpbd_constraints[i];
                                    if (c->is_joint) {
                                        continue;
                                    }
                                    if (world->colliders[c->body_a].type != PHYS_SHAPE_HALFSPACE &&
                                        world->colliders[c->body_b].type != PHYS_SHAPE_HALFSPACE) {
                                        continue;
                                    }
                                    fprintf(stderr,
                                            "  [CONTACT-SOLVED] bodies=%u,%u pen=%.4f lambda=%.4f "
                                            "posA.y=%.3f posB.y=%.3f\n",
                                            c->body_a, c->body_b, c->penetration,
                                            c->rows[0].lambda,
                                            world->body_pool.bodies_next[c->body_a].position.y,
                                            world->body_pool.bodies_next[c->body_b].position.y);
                                    break;
                                }
                                contact_solved_dbg_ticks++;
                            }
                        }
                    }
                    /* Mark XPBD as "dispatched" so the velocity derivation
                     * below runs — even though we already waited per-substep,
                     * we still need to write final velocities into the
                     * shared velocity array. */
                    xpbd_dispatched = true;
                }
            }

            /* ── Stage 11a: TGS Solve (T0/T1 islands) ─────────────── */

            /* Debug: dump state BEFORE solver. */
            if (world->debug_substep_dump) {
                debug_dump_substep(world, world->body_pool.bodies_next,
                                   velocities, pseudo_velocities, body_cap,
                                   (uint32_t)world->tick_count, sub,
                                   "PRE-SOLVE");
            }
#ifdef TRACY_ENABLE
            TracyCZoneN(z_tgs, "Phys.Solve.IteratingTGS", true);
#endif
            phys_stage_tgs_solve_par(&(phys_tgs_solve_args_t){
                .islands    = &islands,
                .constraints = constraints,
                .bodies     = world->body_pool.bodies_next,
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
                .bodies_mut  = world->body_pool.bodies_next,
                .inv_inertia_world_mut = inv_inertia_world,
                .constraint_joint_indices = constraint_joint_indices,
                .skip_body   = body_sub_substepped,
                .manifolds   = manifolds,
                .manifold_count = manifold_count,
                .baumgarte   = world->config.baumgarte,
            }, jobs, &world->frame_arena);
#ifdef TRACY_ENABLE
            TracyCZoneEnd(z_tgs);
#endif

            /* Debug: dump state AFTER solver, BEFORE integration. */
            if (world->debug_substep_dump) {
                debug_dump_substep(world, world->body_pool.bodies_next,
                                   velocities, pseudo_velocities, body_cap,
                                   (uint32_t)world->tick_count, sub,
                                   "POST-SOLVE");
            }

            if (xpbd_dispatched) {
                phys_wait_stage(jobs, PHYS_STAGE_XPBD_SOLVE);

                /* Derive velocities from XPBD position+orientation deltas
                 * and merge into the shared velocity array for XPBD bodies.
                 * Use pre-XPBD saved state vs post-XPBD bodies_next. */
                float inv_dt = (substep_dt > 0.0f) ? 1.0f / substep_dt : 0.0f;

                /* Allocate skip array if needed so integration doesn't
                 * overwrite the XPBD-solved positions in bodies_next. */
                if (!body_sub_substepped) {
                    body_sub_substepped = phys_frame_arena_alloc(
                        &world->frame_arena,
                        (body_cap > 0 ? body_cap : 1) * sizeof(uint8_t),
                        _Alignof(uint8_t));
                    if (body_sub_substepped) {
                        memset(body_sub_substepped, 0,
                               (body_cap > 0 ? body_cap : 1));
                    }
                }

                const phys_body_t *bodies_solved = world->body_pool.bodies_next;
                for (uint32_t bi = 0; bi < xpbd_body_count; bi++) {
                    uint32_t idx = xpbd_body_indices[bi];
                    if (idx >= body_cap) continue;
                    if (xpbd_start_pos) {
                        phys_vec3_t dp = vec3_sub(bodies_solved[idx].position,
                                                   xpbd_start_pos[bi]);
                        velocities[idx].linear = vec3_scale(dp, inv_dt);
                    } else {
                        velocities[idx].linear = bodies_solved[idx].linear_vel;
                    }
                    if (xpbd_start_orient) {
                        phys_quat_t qi_conj = quat_conjugate(xpbd_start_orient[bi]);
                        phys_quat_t dq = quat_mul(bodies_solved[idx].orientation, qi_conj);
                        float sign = (dq.w >= 0.0f) ? 1.0f : -1.0f;
                        velocities[idx].angular = (phys_vec3_t){
                            2.0f * dq.x * sign * inv_dt,
                            2.0f * dq.y * sign * inv_dt,
                            2.0f * dq.z * sign * inv_dt
                        };
                    } else {
                        velocities[idx].angular = bodies_solved[idx].angular_vel;
                    }
                    if (body_sub_substepped) {
                        body_sub_substepped[idx] = 1;
                    }
                }
            }

            /* Write back solved lambdas to joint cache for warmstarting
             * the next substep.  Joint constraints start at
             * joint_constraint_start in the constraint array. */
            if (world->joint_count > 0 && constraints) {
                /* Compute per-body contact impulse magnitude for break
                 * evaluation.  Break strength is triggered by external
                 * forces (contacts), not the joint's own correction. */
                float *body_contact_impulse = phys_frame_arena_alloc(
                    &world->frame_arena,
                    (body_cap > 0 ? body_cap : 1) * sizeof(float),
                    _Alignof(float));
                if (body_contact_impulse) {
                    for (uint32_t bi = 0; bi < body_cap; bi++)
                        body_contact_impulse[bi] = 0.0f;
                    for (uint32_t ci = 0; ci < joint_constraint_start && ci < constraint_count; ci++) {
                        const phys_constraint_t *c = &constraints[ci];
                        if (c->is_joint) continue; /* skip joints */
                        float imp2 = 0.0f;
                        for (uint8_t r = 0; r < c->row_count; r++) {
                            float lam = c->rows[r].lambda;
                            imp2 += lam * lam;
                        }
                        float imp = sqrtf(imp2);
                        if (c->body_a < body_cap)
                            body_contact_impulse[c->body_a] += imp;
                        if (c->body_b < body_cap)
                            body_contact_impulse[c->body_b] += imp;
                    }
                }

                uint8_t *joint_row_write = phys_frame_arena_alloc(
                    &world->frame_arena,
                    (world->joint_count > 0 ? world->joint_count : 1) * sizeof(uint8_t),
                    _Alignof(uint8_t));
                float *joint_impulse_sq = phys_frame_arena_alloc(
                    &world->frame_arena,
                    (world->joint_count > 0 ? world->joint_count : 1) * sizeof(float),
                    _Alignof(float));

                if (joint_row_write && joint_impulse_sq && constraint_joint_indices) {
                    memset(joint_row_write, 0,
                           (world->joint_count > 0 ? world->joint_count : 1) * sizeof(uint8_t));
                    for (uint32_t ji = 0; ji < world->joint_count; ++ji) {
                        memset(world->joints[ji].cached_lambda, 0,
                               sizeof(world->joints[ji].cached_lambda));
                        joint_impulse_sq[ji] = 0.0f;
                    }

                    for (uint32_t ci = joint_constraint_start; ci < constraint_count; ++ci) {
                        const phys_constraint_t *c = &constraints[ci];
                        if (!c->is_joint) {
                            continue;
                        }
                        uint32_t ji = constraint_joint_indices[ci];
                        if (ji >= world->joint_count) {
                            continue;
                        }
                        phys_joint_t *j = &world->joints[ji];
                        for (uint8_t r = 0; r < c->row_count; ++r) {
                            uint8_t row_idx = joint_row_write[ji];
                            if (row_idx < PHYS_JOINT_MAX_ROWS) {
                                j->cached_lambda[row_idx] = c->rows[r].lambda;
                                joint_row_write[ji] = (uint8_t)(row_idx + 1);
                            }
                            joint_impulse_sq[ji] += c->rows[r].lambda * c->rows[r].lambda;
                        }
                    }
                } else {
                    for (uint32_t ji = 0; ji < world->joint_count; ++ji) {
                        memset(world->joints[ji].cached_lambda, 0,
                               sizeof(world->joints[ji].cached_lambda));
                    }
                }

                for (uint32_t ji = 0; ji < world->joint_count; ++ji) {
                    phys_joint_t *j = &world->joints[ji];
                    float impulse_mag = (joint_impulse_sq && ji < world->joint_count)
                                      ? sqrtf(joint_impulse_sq[ji])
                                      : 0.0f;
                    j->accumulated_impulse += impulse_mag;

                    /* Yield: shift rest configuration when threshold
                     * exceeded, allowing plastic deformation. */
                    if (j->yield_strength > 0.0f &&
                        j->accumulated_impulse > j->yield_strength) {
                        /* Shift rest orientation toward current relative
                         * orientation, dissipating the accumulated stress. */
                        const phys_body_t *ba =
                            &world->body_pool.bodies_curr[j->body_a];
                        const phys_body_t *bb =
                            &world->body_pool.bodies_curr[j->body_b];
                        quat_t q_rel = quat_mul(
                            bb->orientation,
                            quat_conjugate(ba->orientation));
                        /* Blend 10% toward the deformed configuration
                         * using normalized linear interpolation (nlerp). */
                        quat_t rest = j->rest_relative_orient;
                        quat_t blended = {
                            .x = rest.x * 0.9f + q_rel.x * 0.1f,
                            .y = rest.y * 0.9f + q_rel.y * 0.1f,
                            .z = rest.z * 0.9f + q_rel.z * 0.1f,
                            .w = rest.w * 0.9f + q_rel.w * 0.1f
                        };
                        j->rest_relative_orient =
                            quat_normalize_safe(blended, 1e-6f);
                        j->accumulated_impulse = 0.0f;
                    }

                    /* Break: triggered by contact impulse on the joint's
                     * bodies, not by the joint's own correction impulse.
                     * A joint breaks when an external impact delivers
                     * enough force to exceed its structural capacity. */
                    if (j->break_strength > 0.0f && body_contact_impulse) {
                        float contact_a = (j->body_a < body_cap)
                            ? body_contact_impulse[j->body_a] : 0.0f;
                        float contact_b = (j->body_b < body_cap)
                            ? body_contact_impulse[j->body_b] : 0.0f;
                        float max_contact = (contact_a > contact_b)
                            ? contact_a : contact_b;
                        if (max_contact > j->break_strength) {
                            j->broken = 1;
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
                    velocities[i].linear  = world->body_pool.bodies_next[i].linear_vel;
                    velocities[i].angular = world->body_pool.bodies_next[i].angular_vel;
                    if (world->body_pool.bodies_next[i].inv_mass > 0.0f &&
                        !phys_body_is_sleeping(&world->body_pool.bodies_next[i]) &&
                        !(world->body_pool.bodies_next[i].flags & PHYS_BODY_FLAG_NO_GRAVITY)) {
                        uint8_t tier = world->body_pool.bodies_next[i].tier;
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

        /* Scale pseudo-velocities for tier dt mismatch.
         * The solver uses inv_dt = 1/substep_dt, but integration uses
         * body_dt = tick_dt / tier_substeps[tier].  When body_dt != substep_dt
         * (e.g. T0 has 2 substeps, max_substeps=4 → body_dt = 2×substep_dt),
         * pseudo-velocities must be scaled by substep_dt/body_dt =
         * tier_substeps/max_substeps so position corrections match. */
        if (pseudo_velocities && max_substeps > 1) {
            for (uint32_t i = 0; i < body_cap; ++i) {
                uint8_t tier = world->body_pool.bodies_next[i].tier;
                uint32_t ts = tier_substep_counts[tier];
                if (ts == 0) { ts = 1; }
                if (ts == max_substeps) { continue; }
                float scale = (float)ts / (float)max_substeps;
                pseudo_velocities[i].linear = vec3_scale(
                    pseudo_velocities[i].linear, scale);
                pseudo_velocities[i].angular = vec3_scale(
                    pseudo_velocities[i].angular, scale);
            }
        }

        if (velocities) {
#ifdef TRACY_ENABLE
            TracyCZoneN(z_integ, "Phys.Integrate.Stepping", true);
#endif
            phys_stage_integrate_par(&(phys_integrate_args_t){
                .bodies_in              = world->body_pool.bodies_next,
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
                .inv_inertia_world      = inv_inertia_world,
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

            /* Debug: dump state AFTER integration. */
            if (world->debug_substep_dump) {
                debug_dump_substep(world, world->body_pool.bodies_next,
                                   velocities, pseudo_velocities, body_cap,
                                   (uint32_t)world->tick_count, sub,
                                   "POST-INTEGRATE");
            }
        }

        /* ── Stage 12c: CCD (swept sphere vs static mesh) ─────── */
        {
            phys_stage_ccd(&(phys_ccd_args_t){
                .bodies_prev      = world->body_pool.bodies_ccd_prev,
                .bodies_read      = world->body_pool.bodies_next,
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

    } /* end substep loop */

    /* ── Publish: single swap to make solved state visible ──────── */
    /* The substep loop worked entirely on bodies_next.  One swap
     * atomically publishes the result as bodies_curr, so the render
     * thread never sees a partially-updated buffer. */
    phys_body_pool_swap_buffers(&world->body_pool);

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

    /* Remove broken joints (reverse order to preserve indices). */
    for (uint32_t ji = world->joint_count; ji > 0; --ji) {
        if (world->joints[ji - 1].broken) {
            phys_world_remove_joint(world, ji - 1);
        }
    }

    /* Increment tick counter. */
    world->tick_count++;

    /* Expire old manifold cache entries (keep for 30 ticks). */
    phys_manifold_cache_expire(&world->manifold_cache,
                               (uint32_t)world->tick_count, 30);

#ifdef TRACY_ENABLE
    TracyCZoneEnd(z_tick);
#endif
}

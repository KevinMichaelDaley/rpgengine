/**
 * @file tgs_solve_par.c
 * @brief Parallel TGS velocity solver with split impulse — one job per island.
 *
 * Dispatches one job per island.  Each job runs the iterative
 * sequential impulse solver on a single island's constraints.
 * Islands access disjoint body indices, so no write contention
 * on the shared velocities array.
 *
 * Non-static functions: 1 (phys_stage_tgs_solve_par).
 */

#include "ferrum/physics/par/tgs_solve_par.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/math/vec3.h"

/** Minimum penetration excess to correct (avoids micro-jitter). */
#define SPLIT_MIN_PHI 1e-6f

/* ── Shared context for all island jobs ────────────────────────── */

/**
 * @brief Shared read/write context passed to each island job.
 *
 * Each job receives a phys_job_batch_t whose user_args points here.
 * The batch's start field is the island index to solve.
 */
typedef struct tgs_solve_shared {
    const phys_island_list_t *islands;      /**< Island decomposition. */
    phys_constraint_t        *constraints;  /**< Constraint array (lambda updated). */
    const phys_body_t        *bodies;       /**< Body array (read-only). */
    phys_velocity_t          *velocities;   /**< Solver velocity workspace. */
    phys_velocity_t          *pseudo_velocities; /**< Split-impulse workspace (may be NULL). */
    uint32_t                  iterations;   /**< Solver iteration count. */
    float                     slop;         /**< Penetration slop threshold. */
    float                     inv_dt;       /**< 1 / substep dt. */
} tgs_solve_shared_t;

/* ── Solve a single Jacobian row (static helper) ──────────────── */

/**
 * @brief Solve one constraint row: compute impulse delta, clamp
 *        the accumulated lambda, and apply velocity corrections.
 */
static void solve_row(phys_jacobian_row_t *row,
                       phys_velocity_t *va,
                       phys_velocity_t *vb,
                       float inv_mass_a,
                       const phys_vec3_t *inv_i_a,
                       float inv_mass_b,
                       const phys_vec3_t *inv_i_b)
{
    /* Compute J·v (relative velocity along the constraint direction). */
    float jv = vec3_dot(row->J_va, va->linear)
             + vec3_dot(row->J_wa, va->angular)
             + vec3_dot(row->J_vb, vb->linear)
             + vec3_dot(row->J_wb, vb->angular);

    /* Impulse delta from constraint violation. */
    float delta_lambda = (row->bias - jv) * row->effective_mass;

    /* Clamp accumulated impulse within bounds. */
    float old_lambda = row->lambda;
    row->lambda = old_lambda + delta_lambda;
    if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
    if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
    delta_lambda = row->lambda - old_lambda;

    /* Apply linear velocity corrections. */
    va->linear = vec3_add(va->linear,
                          vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    vb->linear = vec3_add(vb->linear,
                          vec3_scale(row->J_vb, inv_mass_b * delta_lambda));

    /* Apply angular velocity corrections (diagonal inertia). */
    va->angular.x += inv_i_a->x * row->J_wa.x * delta_lambda;
    va->angular.y += inv_i_a->y * row->J_wa.y * delta_lambda;
    va->angular.z += inv_i_a->z * row->J_wa.z * delta_lambda;

    vb->angular.x += inv_i_b->x * row->J_wb.x * delta_lambda;
    vb->angular.y += inv_i_b->y * row->J_wb.y * delta_lambda;
    vb->angular.z += inv_i_b->z * row->J_wb.z * delta_lambda;
}

/* ── Solve split-impulse position correction row (static helper) ─ */

/**
 * @brief Solve the position correction pseudo-impulse for a normal row.
 *
 * See tgs_solve.c for detailed documentation.
 */
static void solve_position_row(phys_jacobian_row_t *row,
                                phys_velocity_t *pva,
                                phys_velocity_t *pvb,
                                float penetration,
                                float slop,
                                float inv_dt,
                                float inv_mass_a,
                                const phys_vec3_t *inv_i_a,
                                float inv_mass_b,
                                const phys_vec3_t *inv_i_b)
{
    float excess = penetration - slop;
    if (excess < SPLIT_MIN_PHI) { return; }

    float pos_bias = excess * inv_dt;

    float jv = vec3_dot(row->J_va, pva->linear)
             + vec3_dot(row->J_wa, pva->angular)
             + vec3_dot(row->J_vb, pvb->linear)
             + vec3_dot(row->J_wb, pvb->angular);

    float delta_lambda = (pos_bias - jv) * row->effective_mass;

    float old_lambda = row->pseudo_lambda;
    float new_lambda = old_lambda + delta_lambda;
    if (new_lambda < 0.0f) { new_lambda = 0.0f; }
    delta_lambda = new_lambda - old_lambda;
    row->pseudo_lambda = new_lambda;

    if (fabsf(delta_lambda) < 1e-10f) { return; }

    pva->linear = vec3_add(pva->linear,
                           vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    pvb->linear = vec3_add(pvb->linear,
                           vec3_scale(row->J_vb, inv_mass_b * delta_lambda));

    pva->angular.x += inv_i_a->x * row->J_wa.x * delta_lambda;
    pva->angular.y += inv_i_a->y * row->J_wa.y * delta_lambda;
    pva->angular.z += inv_i_a->z * row->J_wa.z * delta_lambda;

    pvb->angular.x += inv_i_b->x * row->J_wb.x * delta_lambda;
    pvb->angular.y += inv_i_b->y * row->J_wb.y * delta_lambda;
    pvb->angular.z += inv_i_b->z * row->J_wb.z * delta_lambda;
}

/* ── Solve a single island (static helper) ────────────────────── */

/**
 * @brief Solve one island: iterate over its constraints for the
 *        configured number of solver iterations.
 */
static void solve_island(const tgs_solve_shared_t *shared,
                          const phys_island_t *island)
{
    if (island->sleeping || island->skip) return;

    phys_velocity_t *pseudo = shared->pseudo_velocities;

    for (uint32_t iter = 0; iter < shared->iterations; iter++) {
        for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
            uint32_t c_idx = island->constraint_indices[ci];
            phys_constraint_t *c = &shared->constraints[c_idx];

            phys_velocity_t *va = &shared->velocities[c->body_a];
            phys_velocity_t *vb = &shared->velocities[c->body_b];

            float inv_mass_a = shared->bodies[c->body_a].inv_mass;
            float inv_mass_b = shared->bodies[c->body_b].inv_mass;
            const phys_vec3_t *inv_i_a =
                &shared->bodies[c->body_a].inv_inertia_diag;
            const phys_vec3_t *inv_i_b =
                &shared->bodies[c->body_b].inv_inertia_diag;

            /* Solve normal row first (row 0). */
            solve_row(&c->rows[0], va, vb,
                      inv_mass_a, inv_i_a,
                      inv_mass_b, inv_i_b);

            /* Split impulse: solve position correction into
             * pseudo-velocities using the same constraint normal. */
            if (pseudo) {
                solve_position_row(
                    &c->rows[0],
                    &pseudo[c->body_a], &pseudo[c->body_b],
                    c->penetration, shared->slop, shared->inv_dt,
                    inv_mass_a, inv_i_a,
                    inv_mass_b, inv_i_b);
            }

            /* Coulomb friction cone: clamp tangent impulses to
             * ±friction * accumulated_normal_impulse. */
            float friction_limit = c->friction * c->rows[0].lambda;
            for (uint8_t r = 1; r < c->row_count; r++) {
                c->rows[r].lambda_min = -friction_limit;
                c->rows[r].lambda_max =  friction_limit;
                solve_row(&c->rows[r], va, vb,
                          inv_mass_a, inv_i_a,
                          inv_mass_b, inv_i_b);
            }
        }
    }
}

/* ── Job function ─────────────────────────────────────────────── */

/**
 * @brief Job function: solve a single island identified by batch->start.
 */
static void tgs_solve_island_job(void *data)
{
    phys_job_batch_t *batch = data;
    tgs_solve_shared_t *shared = batch->user_args;

    uint32_t island_idx = batch->start;
    const phys_island_t *island = &shared->islands->islands[island_idx];

    solve_island(shared, island);
}

/* ── Public API ───────────────────────────────────────────────── */

void phys_stage_tgs_solve_par(const phys_tgs_solve_args_t *args,
                               phys_job_context_t *ctx,
                               phys_frame_arena_t *arena)
{
    if (!args || !ctx || !arena || !args->islands) return;

    /* Initialize velocities from body state and pre-apply gravity
     * so the solver can counteract gravitational acceleration.
     * Each body's gravity impulse uses its tier-specific dt
     * (tick_dt / tier_substeps) so that bodies with fewer substeps
     * receive the correct per-substep gravity increment. */
    if (args->bodies && args->velocities) {
        for (uint32_t i = 0; i < args->body_count; i++) {
            args->velocities[i].linear  = args->bodies[i].linear_vel;
            args->velocities[i].angular = args->bodies[i].angular_vel;
            if (args->bodies[i].inv_mass > 0.0f &&
                !phys_body_is_sleeping(&args->bodies[i])) {
                float body_dt = args->dt;
                if (args->tier_substep_counts && args->tick_dt > 0.0f) {
                    uint8_t tier = args->bodies[i].tier;
                    uint32_t ts = args->tier_substep_counts[tier];
                    if (ts == 0) { ts = 1; }
                    body_dt = args->tick_dt / (float)ts;
                }
                args->velocities[i].linear = vec3_add(
                    args->velocities[i].linear,
                    vec3_scale(args->gravity, body_dt));
            }
        }
    }

    /* Zero pseudo-velocities if split impulse is active. */
    phys_velocity_t *pseudo = args->pseudo_velocities;
    if (pseudo) {
        for (uint32_t i = 0; i < args->body_count; i++) {
            pseudo[i] = (phys_velocity_t){{0,0,0},{0,0,0}};
        }
    }

    const float inv_dt = (args->dt > 0.0f) ? (1.0f / args->dt) : 0.0f;

    uint32_t island_count = args->islands->count;
    if (island_count == 0) return;

    /* Set up shared context for all island jobs. */
    tgs_solve_shared_t shared = {
        .islands          = args->islands,
        .constraints      = args->constraints,
        .bodies           = args->bodies,
        .velocities       = args->velocities,
        .pseudo_velocities = pseudo,
        .iterations       = args->iterations,
        .slop             = args->slop,
        .inv_dt           = inv_dt,
    };

    /* Allocate batch descriptors from the frame arena (one per island). */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, island_count * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) return;

    /* Dispatch one job per island: total_items = island_count, batch_size = 1. */
    phys_dispatch_stage(ctx, PHYS_STAGE_TGS_SOLVE,
                        tgs_solve_island_job, &shared,
                        island_count, 1, batches);

    /* Wait for all island jobs to complete. */
    phys_wait_stage(ctx, PHYS_STAGE_TGS_SOLVE);
}

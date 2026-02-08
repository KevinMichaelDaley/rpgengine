/**
 * @file tgs_solve_par.c
 * @brief Parallel TGS velocity solver — one job per island.
 *
 * Dispatches one job per island.  Each job runs the iterative
 * sequential impulse solver on a single island's constraints.
 * Islands access disjoint body indices, so no write contention
 * on the shared velocities array.
 *
 * Non-static functions: 1 (phys_stage_tgs_solve_par).
 */

#include "ferrum/physics/par/tgs_solve_par.h"

#include <stddef.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/math/vec3.h"

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
    uint32_t                  iterations;   /**< Solver iteration count. */
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

/* ── Solve a single island (static helper) ────────────────────── */

/**
 * @brief Solve one island: iterate over its constraints for the
 *        configured number of solver iterations.
 */
static void solve_island(const tgs_solve_shared_t *shared,
                          const phys_island_t *island)
{
    if (island->sleeping) return;

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

    /* Initialize velocities from body state (same as sequential). */
    if (args->bodies && args->velocities) {
        for (uint32_t i = 0; i < args->body_count; i++) {
            args->velocities[i].linear  = args->bodies[i].linear_vel;
            args->velocities[i].angular = args->bodies[i].angular_vel;
        }
    }

    uint32_t island_count = args->islands->count;
    if (island_count == 0) return;

    /* Set up shared context for all island jobs. */
    tgs_solve_shared_t shared = {
        .islands     = args->islands,
        .constraints = args->constraints,
        .bodies      = args->bodies,
        .velocities  = args->velocities,
        .iterations  = args->iterations,
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

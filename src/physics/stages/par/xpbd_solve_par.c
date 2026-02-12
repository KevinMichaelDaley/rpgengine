/**
 * @file xpbd_solve_par.c
 * @brief Parallel XPBD position solver implementation.
 *
 * Splits constraint range into batches of PHYS_XPBD_SOLVE_BATCH_SIZE,
 * dispatches each batch as a job that solves its constraint slice using
 * Jacobi iteration (reads from start-of-iteration body positions,
 * writes corrections to its own body slots).
 *
 * Non-static functions: 1 (phys_stage_xpbd_solve_par).
 */

#include "ferrum/physics/par/xpbd_solve_par.h"

#include <stddef.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/math/vec3.h"

/* ── Per-batch shared context ──────────────────────────────────── */

/**
 * @brief Shared context across all batches in a parallel XPBD solve.
 *
 * Each job receives a phys_job_batch_t whose user_args points here.
 * All jobs read from the same bodies array (Jacobi: read from
 * start-of-iteration positions) and write corrections back.
 */
typedef struct xpbd_solve_shared {
    phys_constraint_t *constraints;  /**< Constraint array (read/write). */
    phys_body_t       *bodies;       /**< Working body positions (read/write). */
    float              omega;        /**< Jacobi relaxation factor. */
    float              dt;           /**< Timestep in seconds. */
    float              compliance;   /**< XPBD compliance (α); 0 = stiff. */
} xpbd_solve_shared_t;

/* ── Static helpers ────────────────────────────────────────────── */

/**
 * @brief Solve a single constraint at the position level (Jacobi).
 *
 * Identical to the logic in xpbd_solve.c but extracted here to avoid
 * depending on static functions in another translation unit.
 */
static void solve_contact_position(phys_constraint_t *c,
                                   phys_body_t *bodies,
                                   float omega,
                                   float dt,
                                   float compliance)
{
    phys_body_t *ba = &bodies[c->body_a];
    phys_body_t *bb = &bodies[c->body_b];

    /* Only solve the normal row (row 0) for position correction. */
    phys_jacobian_row_t *row = &c->rows[0];

    float w_a = ba->inv_mass;
    float w_b = bb->inv_mass;

    /* Both bodies are static/kinematic — skip. */
    if (w_a + w_b < 1e-10f) return;

    /* Contact normal from Jacobian. */
    phys_vec3_t normal = row->J_vb;
    float n_len_sq = vec3_dot(normal, normal);
    if (n_len_sq < 1e-10f) return;

    /* Position error from bias term. */
    float C = -row->bias * dt;

    /* XPBD regularization: alpha_tilde = compliance / dt^2. */
    float alpha_tilde = (dt > 0.0f) ? compliance / (dt * dt) : 0.0f;
    float w_sum = w_a + w_b + alpha_tilde;

    float delta_lambda = (-C - alpha_tilde * row->lambda) / w_sum;

    /* Clamp: contacts only push apart (accumulated lambda >= 0). */
    float old_lambda = row->lambda;
    row->lambda = old_lambda + delta_lambda;
    if (row->lambda < 0.0f) row->lambda = 0.0f;
    delta_lambda = row->lambda - old_lambda;

    /* Apply position corrections along the contact normal. */
    phys_vec3_t correction_a = vec3_scale(normal, w_a * delta_lambda * omega);
    phys_vec3_t correction_b = vec3_scale(normal, w_b * delta_lambda * omega);
    ba->position = vec3_sub(ba->position, correction_a);
    bb->position = vec3_add(bb->position, correction_b);
}

/**
 * @brief Solve a joint constraint at the position level (all rows, bilateral).
 */
static void solve_joint_position(phys_constraint_t *c,
                                 phys_body_t *bodies,
                                 float omega,
                                 float dt,
                                 float compliance)
{
    phys_body_t *ba = &bodies[c->body_a];
    phys_body_t *bb = &bodies[c->body_b];

    float w_a = ba->inv_mass;
    float w_b = bb->inv_mass;

    if (w_a + w_b < 1e-10f) return;

    float alpha_tilde = (dt > 0.0f) ? compliance / (dt * dt) : 0.0f;
    float w_sum = w_a + w_b + alpha_tilde;

    for (uint8_t r = 0; r < c->row_count; ++r) {
        phys_jacobian_row_t *row = &c->rows[r];

        phys_vec3_t dir = row->J_vb;
        float dir_len_sq = vec3_dot(dir, dir);
        if (dir_len_sq < 1e-10f) continue;

        /* Joint bias holds raw position error (meters, signed). */
        float C = row->bias;
        float delta_lambda = (-C - alpha_tilde * row->lambda) / w_sum;

        float old_lambda = row->lambda;
        row->lambda = old_lambda + delta_lambda;
        if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
        if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
        delta_lambda = row->lambda - old_lambda;

        phys_vec3_t corr_a = vec3_scale(dir, w_a * delta_lambda * omega);
        phys_vec3_t corr_b = vec3_scale(dir, w_b * delta_lambda * omega);
        ba->position = vec3_sub(ba->position, corr_a);
        bb->position = vec3_add(bb->position, corr_b);

        /* Update bias to reflect reduced error after correction. */
        row->bias += (w_a + w_b) * delta_lambda * omega;
    }
}

/**
 * @brief Job function: solve constraints in [start, start+count).
 */
static void xpbd_batch_job(void *data)
{
    phys_job_batch_t *batch = data;
    xpbd_solve_shared_t *shared = batch->user_args;

    uint32_t end = batch->start + batch->count;
    for (uint32_t ci = batch->start; ci < end; ++ci) {
        phys_constraint_t *c = &shared->constraints[ci];
        if (c->is_joint) {
            solve_joint_position(c, shared->bodies,
                                 shared->omega, shared->dt,
                                 shared->compliance);
        } else {
            solve_contact_position(c, shared->bodies,
                                   shared->omega, shared->dt,
                                   shared->compliance);
        }
    }
}

/**
 * @brief Derive velocities from position deltas after solving.
 */
static void derive_velocities(const phys_body_t *bodies_in,
                              const phys_body_t *bodies_out,
                              phys_velocity_t *velocities,
                              uint32_t body_count,
                              float dt)
{
    float inv_dt = 1.0f / dt;
    for (uint32_t i = 0; i < body_count; i++) {
        phys_vec3_t dp = vec3_sub(bodies_out[i].position,
                                   bodies_in[i].position);
        velocities[i].linear  = vec3_scale(dp, inv_dt);
        velocities[i].angular = bodies_in[i].angular_vel;
    }
}

/* ── Public API ────────────────────────────────────────────────── */

void phys_stage_xpbd_solve_par(const phys_xpbd_solve_args_t *args,
                                phys_job_context_t *ctx,
                                phys_frame_arena_t *arena)
{
    if (!args || !ctx || !arena) {
        /* Fall back to sequential if no job context. */
        if (args && (!ctx || !arena)) {
            phys_stage_xpbd_solve(args);
        }
        return;
    }

    if (!args->constraints || !args->bodies_in || !args->bodies_out) {
        return;
    }
    if (args->body_count == 0 || args->constraint_count == 0) {
        /* Zero constraints: copy bodies through and derive velocities. */
        if (args->body_count > 0 && args->bodies_in && args->bodies_out) {
            memcpy(args->bodies_out, args->bodies_in,
                   args->body_count * sizeof(phys_body_t));
            if (args->velocities_out && args->dt > 0.0f) {
                derive_velocities(args->bodies_in, args->bodies_out,
                                  args->velocities_out, args->body_count,
                                  args->dt);
            }
        }
        return;
    }

    /* Step 1: Copy body state from input to output workspace. */
    memcpy(args->bodies_out, args->bodies_in,
           args->body_count * sizeof(phys_body_t));

    /* Set up shared context for constraint batches. */
    xpbd_solve_shared_t shared = {
        .constraints = args->constraints,
        .bodies      = args->bodies_out,
        .omega       = args->omega,
        .dt          = args->dt,
        .compliance  = args->compliance,
    };

    /* Calculate number of batches. */
    uint32_t batch_size  = PHYS_XPBD_SOLVE_BATCH_SIZE;
    uint32_t num_batches = (args->constraint_count + batch_size - 1) / batch_size;

    /* Allocate batch descriptors from the frame arena. */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) {
        phys_stage_xpbd_solve(args);
        return;
    }

    /* Step 2: Iterative Jacobi constraint projection via parallel batches. */
    for (uint32_t iter = 0; iter < args->iterations; iter++) {
        phys_dispatch_stage(ctx, PHYS_STAGE_XPBD_SOLVE,
                            xpbd_batch_job, &shared,
                            args->constraint_count, batch_size, batches);
        phys_wait_stage(ctx, PHYS_STAGE_XPBD_SOLVE);
    }

    /* Step 3: Derive velocities from position change. */
    if (args->velocities_out && args->dt > 0.0f) {
        derive_velocities(args->bodies_in, args->bodies_out,
                          args->velocities_out, args->body_count,
                          args->dt);
    }
}

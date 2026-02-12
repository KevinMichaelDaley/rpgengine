/**
 * @file xpbd_solve.c
 * @brief XPBD (Extended Position-Based Dynamics) position solver.
 *
 * Position-based solver for T2-T4 bodies.  Unconditionally stable,
 * does not require island decomposition.  Derives final velocities
 * from position deltas.
 *
 * 1 non-static function: phys_stage_xpbd_solve
 */

#include "ferrum/physics/xpbd_solve.h"

#include <stddef.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/math/vec3.h"

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * @brief Copy body state from input array to output array.
 */
static void copy_bodies(const phys_body_t *src, phys_body_t *dst,
                         uint32_t count)
{
    memcpy(dst, src, count * sizeof(phys_body_t));
}

/**
 * @brief Solve a single normal-row contact constraint at the position level.
 *
 * Computes a position-level correction using the XPBD formulation:
 *   delta_lambda = (-C - alpha_tilde * lambda) / (w_a + w_b + alpha_tilde)
 * where C is the remaining penetration projected along the contact normal,
 * and alpha_tilde = compliance / dt^2.
 *
 * @param c           Constraint to solve (lambda updated in row 0).
 * @param bodies      Working body array (positions modified in-place).
 * @param omega       Relaxation factor (0.5-1.0).
 * @param dt          Timestep in seconds.
 * @param compliance  XPBD compliance (α); 0 = perfectly stiff.
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

    /* Contact normal: J_vb = +normal (from A toward B). */
    phys_vec3_t normal = row->J_vb;
    float n_len_sq = vec3_dot(normal, normal);
    if (n_len_sq < 1e-10f) return;

    /* Evaluate current penetration from positions.
     * For a contact with normal pointing A→B, bodies should be separated
     * along the normal.  The constraint: C = dot(pa - pb, normal) + d
     * where d is the rest separation distance.
     *
     * We use the bias term to recover the original penetration:
     *   bias = (baumgarte/dt) * max(pen - slop, 0)
     * So C (position error) = bias * dt / baumgarte ≈ bias * dt
     * We approximate with a conservative factor to avoid over-correction. */
    float C = -row->bias * dt;

    /* Re-evaluate penetration from current positions on subsequent solves.
     * Project the body-to-body vector onto the normal and compare with
     * the original separation to find remaining penetration. */

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
 *
 * For each row, computes XPBD position correction along the row's
 * J_vb direction with bilateral lambda clamping (lambda_min..lambda_max).
 *
 * @param c           Joint constraint (is_joint != 0).
 * @param bodies      Working body array (positions modified in-place).
 * @param omega       Relaxation factor (0.5-1.0).
 * @param dt          Timestep in seconds.
 * @param compliance  XPBD compliance (α); 0 = perfectly stiff.
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

        /* Joint bias holds the raw position error (meters, signed).
         * Re-evaluate the constraint value from accumulated corrections:
         * each correction changes the error along this row's direction
         * by the total position delta projected onto J_vb. */
        float C = row->bias;
        float delta_lambda = (-C - alpha_tilde * row->lambda) / w_sum;

        /* Bilateral clamp using the row's lambda bounds. */
        float old_lambda = row->lambda;
        row->lambda = old_lambda + delta_lambda;
        if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
        if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
        delta_lambda = row->lambda - old_lambda;

        phys_vec3_t corr_a = vec3_scale(dir, w_a * delta_lambda * omega);
        phys_vec3_t corr_b = vec3_scale(dir, w_b * delta_lambda * omega);
        ba->position = vec3_sub(ba->position, corr_a);
        bb->position = vec3_add(bb->position, corr_b);

        /* Update bias to reflect reduced error after position correction.
         * The relative displacement change along the constraint direction
         * is (w_a + w_b) * delta_lambda * omega. */
        row->bias += (w_a + w_b) * delta_lambda * omega;
    }
}

/**
 * @brief Derive velocities from position deltas after solving.
 *
 * velocity = (pos_out - pos_in) / dt.  Angular velocity is preserved
 * from the input bodies (position solver does not change rotation).
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

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_xpbd_solve(const phys_xpbd_solve_args_t *args)
{
    if (!args || !args->constraints) return;
    if (!args->bodies_in || !args->bodies_out) return;
    if (args->body_count == 0 || args->constraint_count == 0) return;

    /* Step 1: Copy body state from input to output workspace. */
    copy_bodies(args->bodies_in, args->bodies_out, args->body_count);

    /* Step 2: Iterative position-level constraint projection. */
    for (uint32_t iter = 0; iter < args->iterations; iter++) {
        for (uint32_t ci = 0; ci < args->constraint_count; ci++) {
            phys_constraint_t *c = &args->constraints[ci];
            if (c->is_joint) {
                solve_joint_position(c, args->bodies_out,
                                     args->omega, args->dt,
                                     args->compliance);
            } else {
                solve_contact_position(c, args->bodies_out,
                                       args->omega, args->dt,
                                       args->compliance);
            }
        }
    }

    /* Step 3: Derive velocities from position change. */
    if (args->velocities_out && args->dt > 0.0f) {
        derive_velocities(args->bodies_in, args->bodies_out,
                          args->velocities_out, args->body_count,
                          args->dt);
    }
}

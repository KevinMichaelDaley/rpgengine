/**
 * @file tgs_solve_par.c
 * @brief Parallel TGS velocity solver with split impulse and graph coloring.
 *
 * Two dispatch strategies:
 *   1. Small islands (< color_threshold constraints): one job per island,
 *      sequential solve within each job.
 *   2. Large islands (>= color_threshold): graph-color the constraints,
 *      then for each solver iteration, dispatch same-color batches in
 *      parallel with a barrier between each color.  Same-color constraints
 *      share no bodies, so no write contention.
 *
 * Non-static functions: 1 (phys_stage_tgs_solve_par).
 */

#include "ferrum/physics/par/tgs_solve_par.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/constraint_color.h"
#include "ferrum/physics/constraint_rebuild.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/** Minimum penetration excess to correct (avoids micro-jitter). */
#define SPLIT_MIN_PHI 1e-6f

/** Position correction ERP: fraction of penetration resolved per substep.
 *  1.0 = full correction (aggressive, can oscillate).
 *  0.2–0.4 = typical for stable stacking/resting contacts.
 *  0.8 = aggressive, matches joint positional ERP for ragdolls. */
#define SPLIT_ERP 0.1f

/** Min constraints per island to use graph-colored parallel dispatch. */
#define COLOR_THRESHOLD 128

/** Speed (m/s) above which we start adding solver iterations. */
#define ADAPTIVE_SPEED_LOW  5.0f
/** Speed (m/s) at which we reach maximum solver iterations. */
#define ADAPTIVE_SPEED_HIGH 200.0f
/** Maximum multiplier on base iteration count for fast islands. */
#define ADAPTIVE_ITER_MULT  5

/** Successive over-relaxation factor for contacts.  Values > 1.0
 *  accelerate convergence; typical range 1.1–1.5. */
#define SOR_OMEGA 1.1f

/** Under-relaxation factor for joint constraints.  Joint chains couple
 *  many rows through shared hub bodies (e.g. root in ragdoll).  The
 *  critical omega for a 21-body humanoid ragdoll is ~0.82; above that
 *  the Gauss-Seidel iteration diverges.  0.8 is near-optimal. */
#define JOINT_SOR_OMEGA 0.8f

/* ── Internal: compute per-island iteration count ─────────────── */

/**
 * @brief Compute a per-island solver iteration count scaled by the
 *        maximum body speed in the island.
 *
 * @param island       The island to inspect.
 * @param bodies       Body array (read-only).
 * @param velocities   Solver velocity workspace (post-gravity).
 * @param base_iters   Configured base iteration count.
 * @return Iteration count for this island (>= base_iters).
 */
static uint32_t compute_island_iterations(
    const phys_island_t *island,
    const phys_body_t *bodies,
    const phys_velocity_t *velocities,
    uint32_t base_iters)
{
    float max_speed_sq = 0.0f;
    for (uint32_t bi = 0; bi < island->body_count; bi++) {
        uint32_t idx = island->body_indices[bi];
        if (bodies[idx].inv_mass == 0.0f) continue;
        phys_vec3_t v = velocities[idx].linear;
        float speed_sq = vec3_dot(v, v);
        if (speed_sq > max_speed_sq) {
            max_speed_sq = speed_sq;
        }
    }

    const float lo2 = ADAPTIVE_SPEED_LOW  * ADAPTIVE_SPEED_LOW;
    const float hi2 = ADAPTIVE_SPEED_HIGH * ADAPTIVE_SPEED_HIGH;

    if (max_speed_sq <= lo2) {
        /* Even at low speed, serial chains need enough iterations for
         * impulse to propagate root-to-tip.  Floor at the island body
         * count so a 40-body chain gets at least 40 iterations. */
        uint32_t chain_floor = island->body_count;
        return (chain_floor > base_iters) ? chain_floor : base_iters;
    }

    /* Scale down the adaptive multiplier for large islands to bound
     * worst-case cost (iterations × colors × barriers).  Small islands
     * are cheap regardless, so let them use the full multiplier. */
    uint32_t mult = ADAPTIVE_ITER_MULT;
    if (island->constraint_count > 512) {
        mult = 2;
    } else if (island->constraint_count > 256) {
        mult = (mult > 3) ? 3 : mult;
    }

    if (max_speed_sq >= hi2) {
        return base_iters * mult;
    }

    /* Sqrt ramp: aggressive at moderate speeds, plateaus at extremes. */
    float t = (max_speed_sq - lo2) / (hi2 - lo2);
    t = sqrtf(t);
    uint32_t extra = (uint32_t)(t * (float)(base_iters * (mult - 1)));
    uint32_t result = base_iters + extra;

    /* Floor: serial chains need body_count iterations minimum. */
    uint32_t chain_floor = island->body_count;
    return (result > chain_floor) ? result : chain_floor;
}

static bool island_routes_xpbd_(const phys_island_t *island,
                                const phys_constraint_t *constraints,
                                const phys_body_t *bodies)
{
    if (!island || !bodies) {
        return false;
    }

    if (!constraints) {
        return false;
    }

    for (uint32_t ci = 0; ci < island->constraint_count; ++ci) {
        if (constraints[island->constraint_indices[ci]].solver_mode ==
            PHYS_SOLVER_XPBD) {
            return true;
        }
    }

    return false;
}

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
    const phys_mat3_t        *inv_inertia_world; /**< World-space inverse inertia per body. */
    phys_velocity_t          *velocities;   /**< Solver velocity workspace. */
    phys_velocity_t          *pseudo_velocities; /**< Split-impulse workspace (may be NULL). */
    uint32_t                  iterations;   /**< Solver iteration count. */
    uint32_t                  body_count;   /**< Number of bodies. */
    float                     slop;         /**< Penetration slop threshold. */
    float                     inv_dt;       /**< 1 / substep dt. */
    float                     dt;           /**< Substep dt (s). */
    float                     tick_dt;      /**< Full tick dt (for per-body compliance). */
    const uint32_t           *tier_substep_counts; /**< Per-tier substep counts (may be NULL). */
    phys_body_t              *bodies_mut;   /**< Mutable bodies for coupled solver (may be NULL). */
    phys_mat3_t              *inv_inertia_world_mut; /**< Mutable inertia for coupled solver. */
    phys_joint_t             *joints;       /**< Joint array for Jacobian rebuild. */
    uint32_t                  joint_count;  /**< Number of joints. */
    const uint32_t           *constraint_joint_indices; /**< Maps constraint → joint. */
    uint8_t                  *skip_body;    /**< Per-body integrator skip flag. */
    const struct phys_manifold *manifolds;  /**< Manifolds for contact rebuild. */
    uint32_t                  manifold_count; /**< Number of manifolds. */
    float                     baumgarte;    /**< Baumgarte factor for contact rebuild. */
    phys_frame_arena_t       *frame_arena;  /**< Frame arena for temporary allocations. */
} tgs_solve_shared_t;

/**
 * @brief Context for a single colored-constraint job.
 *
 * batch->start indexes into color_constraint_indices.
 * batch->count is the number of constraints to solve.
 */
typedef struct tgs_color_shared {
    phys_constraint_t *constraints;        /**< Full constraint array. */
    const phys_body_t *bodies;             /**< Body array (read-only). */
    const phys_mat3_t *inv_inertia_world;  /**< World-space inverse inertia per body. */
    phys_velocity_t   *velocities;         /**< Solver velocity workspace. */
    phys_velocity_t   *pseudo_velocities;  /**< Split-impulse workspace (may be NULL). */
    float              slop;               /**< Penetration slop threshold. */
    float              inv_dt;             /**< 1 / substep dt. */
    float              tick_dt;            /**< Full tick dt (for per-body compliance). */
    const uint32_t    *tier_substep_counts;/**< Per-tier substep counts (may be NULL). */
    const uint32_t    *constraint_indices; /**< Global constraint indices for this color batch. */
    phys_body_t       *bodies_mut;         /**< Mutable bodies for coupled solver (may be NULL). */
} tgs_color_shared_t;

/* ── Solve a single Jacobian row (static helper) ──────────────── */

/**
 * @brief Solve one constraint row: compute impulse delta, clamp
 *        the accumulated lambda, and apply velocity corrections.
 */
static void solve_row(phys_jacobian_row_t *row,
                       phys_velocity_t *va,
                       phys_velocity_t *vb,
                       float inv_mass_a,
                       const phys_mat3_t *inv_i_a,
                       float inv_mass_b,
                       const phys_mat3_t *inv_i_b,
                       float dt,
                       float omega)
{
    float jv = vec3_dot(row->J_va, va->linear)
             + vec3_dot(row->J_wa, va->angular)
             + vec3_dot(row->J_vb, vb->linear)
             + vec3_dot(row->J_wb, vb->angular);

    /* Viscous damping: scale the velocity error by (1 + d*dt) so the
     * solver applies a stronger correction that opposes relative motion.
     * The dt factor keeps the damping force physically correct and
     * prevents instability at large damping coefficients.
     * d=10, dt=0.008 → factor=1.08 (stable, mildly damped). */
    float jv_damped = jv * (1.0f + row->damping * dt);
    float delta_lambda = (row->bias - jv_damped) * row->effective_mass;

    delta_lambda *= omega;

    float old_lambda = row->lambda;
    row->lambda = old_lambda + delta_lambda;
    if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
    if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
    delta_lambda = row->lambda - old_lambda;

    va->linear = vec3_add(va->linear,
                          vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    vb->linear = vec3_add(vb->linear,
                          vec3_scale(row->J_vb, inv_mass_b * delta_lambda));

    /* Apply angular velocity corrections (world-space inverse inertia). */
    phys_vec3_t ang_a = phys_mat3_mul_vec3(inv_i_a, row->J_wa);
    va->angular = vec3_add(va->angular, vec3_scale(ang_a, delta_lambda));

    phys_vec3_t ang_b = phys_mat3_mul_vec3(inv_i_b, row->J_wb);
    vb->angular = vec3_add(vb->angular, vec3_scale(ang_b, delta_lambda));
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
                                const phys_mat3_t *inv_i_a,
                                float inv_mass_b,
                                const phys_mat3_t *inv_i_b)
{
    float excess = penetration - slop;
    if (excess < SPLIT_MIN_PHI) { return; }

    float pos_bias = excess * inv_dt * SPLIT_ERP;

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

    phys_vec3_t ang_a = phys_mat3_mul_vec3(inv_i_a, row->J_wa);
    pva->angular = vec3_add(pva->angular, vec3_scale(ang_a, delta_lambda));

    phys_vec3_t ang_b = phys_mat3_mul_vec3(inv_i_b, row->J_wb);
    pvb->angular = vec3_add(pvb->angular, vec3_scale(ang_b, delta_lambda));
}

/**
 * @brief Solve one joint row's position error via split impulse.
 *
 * Bilateral clamping (joints can push and pull).  The row's bias
 * field holds the raw position error in meters.
 */
static void solve_joint_position_row(phys_jacobian_row_t *row,
                                      phys_velocity_t *pva,
                                      phys_velocity_t *pvb,
                                      float inv_dt,
                                      float inv_mass_a,
                                      const phys_mat3_t *inv_i_a,
                                      float inv_mass_b,
                                      const phys_mat3_t *inv_i_b)
{
    float error = row->bias;
    if (fabsf(error) < 1e-7f) { return; }

    /* Joint positional rows use moderate ERP to avoid Gauss-Seidel
     * divergence in long joint chains (ragdoll: 19 joints).
     * Pseudo-velocities are scaled by tier_substeps/max_substeps
     * before integration, so effective inv_dt matches body_dt.
     * Angular rows use softer ERP to prevent overshoot but still
     * converge positionally. */
    float erp = (row->flags & PHYS_ROW_FLAG_ANGULAR)
              ? 0.2f
              : 0.4f;
    float pos_bias = -error * inv_dt * erp;

    float jv = vec3_dot(row->J_va, pva->linear)
             + vec3_dot(row->J_wa, pva->angular)
             + vec3_dot(row->J_vb, pvb->linear)
             + vec3_dot(row->J_wb, pvb->angular);

    float delta_lambda = (pos_bias - jv) * row->effective_mass;

    float old_lambda = row->pseudo_lambda;
    row->pseudo_lambda = old_lambda + delta_lambda;
    delta_lambda = row->pseudo_lambda - old_lambda;

    if (fabsf(delta_lambda) < 1e-10f) { return; }

    pva->linear = vec3_add(pva->linear,
                           vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    pvb->linear = vec3_add(pvb->linear,
                           vec3_scale(row->J_vb, inv_mass_b * delta_lambda));

    phys_vec3_t ang_a = phys_mat3_mul_vec3(inv_i_a, row->J_wa);
    pva->angular = vec3_add(pva->angular, vec3_scale(ang_a, delta_lambda));

    phys_vec3_t ang_b = phys_mat3_mul_vec3(inv_i_b, row->J_wb);
    pvb->angular = vec3_add(pvb->angular, vec3_scale(ang_b, delta_lambda));
}

/* ── Temporary debug: dump full Jacobian for joint constraints ──── */

/** Global debug counter — decremented each call.  When > 0, dump rows. */
int g_tgs_debug_dump_counter = 0;

static void debug_dump_joint_rows(const phys_constraint_t *c,
                                  const phys_velocity_t *va,
                                  const phys_velocity_t *vb,
                                  uint32_t c_idx)
{
    if (g_tgs_debug_dump_counter <= 0) return;
    if (!c->is_joint) return;

    fprintf(stderr, "  c%u b%u→b%u rows=%u compliance=%.6f is_joint=%u\n",
            c_idx, c->body_a, c->body_b, c->row_count, c->compliance, c->is_joint);
    for (uint8_t r = 0; r < c->row_count; r++) {
        const phys_jacobian_row_t *row = &c->rows[r];
        const char *kind = (row->flags & PHYS_ROW_FLAG_ANGULAR) ? "ANG" : "POS";
        fprintf(stderr, "    r%u[%s] bias=%.6f eff_mass=%.6f λ=%.4f [%.1f,%.1f] damp=%.3f\n",
                r, kind, row->bias, row->effective_mass, row->lambda,
                row->lambda_min, row->lambda_max, row->damping);
        fprintf(stderr, "      J_va=(%.4f,%.4f,%.4f) J_wa=(%.4f,%.4f,%.4f)\n",
                row->J_va.x, row->J_va.y, row->J_va.z,
                row->J_wa.x, row->J_wa.y, row->J_wa.z);
        fprintf(stderr, "      J_vb=(%.4f,%.4f,%.4f) J_wb=(%.4f,%.4f,%.4f)\n",
                row->J_vb.x, row->J_vb.y, row->J_vb.z,
                row->J_wb.x, row->J_wb.y, row->J_wb.z);
    }
    fprintf(stderr, "    va_lin=(%.4f,%.4f,%.4f) va_ang=(%.4f,%.4f,%.4f)\n",
            va->linear.x, va->linear.y, va->linear.z,
            va->angular.x, va->angular.y, va->angular.z);
    fprintf(stderr, "    vb_lin=(%.4f,%.4f,%.4f) vb_ang=(%.4f,%.4f,%.4f)\n",
            vb->linear.x, vb->linear.y, vb->linear.z,
            vb->angular.x, vb->angular.y, vb->angular.z);
    g_tgs_debug_dump_counter--;
}

/* ── Solve a single constraint (used by colored job) ──────────── */

/**
 * @brief Solve a joint constraint using the coupled implicit method.
 *
 * Parallel-solver version.  See tgs_solve.c solve_joint_coupled() for
 * detailed documentation of the update equation.
 */
static void solve_joint_coupled_par(phys_constraint_t *c,
                                     phys_velocity_t *velocities,
                                     phys_body_t *bodies_mut,
                                     const phys_mat3_t *inv_inertia_world,
                                     float dt,
                                     float inv_dt)
{
    phys_velocity_t *va = &velocities[c->body_a];
    phys_velocity_t *vb = &velocities[c->body_b];

    float inv_mass_a = bodies_mut[c->body_a].inv_mass;
    float inv_mass_b = bodies_mut[c->body_b].inv_mass;

    phys_mat3_t fallback_a, fallback_b;
    const phys_mat3_t *inv_i_a;
    const phys_mat3_t *inv_i_b;
    if (inv_inertia_world) {
        inv_i_a = &inv_inertia_world[c->body_a];
        inv_i_b = &inv_inertia_world[c->body_b];
    } else {
        fallback_a = phys_mat3_inv_inertia_world(
            bodies_mut[c->body_a].orientation,
            bodies_mut[c->body_a].inv_inertia_diag);
        fallback_b = phys_mat3_inv_inertia_world(
            bodies_mut[c->body_b].orientation,
            bodies_mut[c->body_b].inv_inertia_diag);
        inv_i_a = &fallback_a;
        inv_i_b = &fallback_b;
    }

    float alpha_hard  = c->compliance;
    float alpha_drive = c->drive_compliance;
    float gamma = c->joint_damping;
    float gamma_over_h = gamma * inv_dt;

    for (uint8_t r = 0; r < c->row_count; r++) {
        phys_jacobian_row_t *row = &c->rows[r];

        float jv = vec3_dot(row->J_va, va->linear)
                 + vec3_dot(row->J_wa, va->angular)
                 + vec3_dot(row->J_vb, vb->linear)
                 + vec3_dot(row->J_wb, vb->angular);

        float C_i = row->bias;
        /* Position ERP: fraction of C/h to correct per substep.
         * Prevents oscillation when multiple constraints compete. */
        const float coupled_erp = 0.6f;

        /* Select compliance: drive rows use drive_compliance. */
        float alpha = (row->flags & PHYS_ROW_FLAG_DRIVE)
                    ? alpha_drive : alpha_hard;
        float alpha_over_h2 = alpha * inv_dt * inv_dt;

        float numerator = -(jv + coupled_erp * C_i * inv_dt
                            + alpha * inv_dt * row->lambda);

        float jmjt = (row->effective_mass > 1e-12f)
                    ? (1.0f / row->effective_mass)
                    : 1e12f;
        float denom = jmjt + alpha_over_h2 + gamma_over_h;
        float inv_denom = (denom > 1e-12f) ? (1.0f / denom) : 0.0f;

        float delta_lambda = numerator * inv_denom;

        float old_lambda = row->lambda;
        row->lambda = old_lambda + delta_lambda;
        if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
        if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
        delta_lambda = row->lambda - old_lambda;

        if (fabsf(delta_lambda) < 1e-12f) continue;

        phys_vec3_t dv_lin_a = vec3_scale(row->J_va,
                                           inv_mass_a * delta_lambda);
        phys_vec3_t dv_lin_b = vec3_scale(row->J_vb,
                                           inv_mass_b * delta_lambda);
        phys_vec3_t dv_ang_a = vec3_scale(
            phys_mat3_mul_vec3(inv_i_a, row->J_wa), delta_lambda);
        phys_vec3_t dv_ang_b = vec3_scale(
            phys_mat3_mul_vec3(inv_i_b, row->J_wb), delta_lambda);

        va->linear  = vec3_add(va->linear,  dv_lin_a);
        va->angular = vec3_add(va->angular, dv_ang_a);
        vb->linear  = vec3_add(vb->linear,  dv_lin_b);
        vb->angular = vec3_add(vb->angular, dv_ang_b);

        /* Coupled position update: accumulate position changes inline
         * so that inter-iteration Jacobian rebuilds use up-to-date
         * body positions.  These are TEMPORARY — the caller saves
         * and restores original positions around the solve loop. */
        phys_vec3_t dp_a = vec3_scale(dv_lin_a, dt);
        phys_vec3_t dp_b = vec3_scale(dv_lin_b, dt);
        bodies_mut[c->body_a].position =
            vec3_add(bodies_mut[c->body_a].position, dp_a);
        bodies_mut[c->body_b].position =
            vec3_add(bodies_mut[c->body_b].position, dp_b);

        /* Integrate orientation: q += 0.5 * dt * [0, ω] * q */
        phys_quat_t qa = bodies_mut[c->body_a].orientation;
        phys_quat_t qb = bodies_mut[c->body_b].orientation;

        phys_quat_t dqa = {
            0.5f * dt * (dv_ang_a.x * qa.w + dv_ang_a.y * qa.z - dv_ang_a.z * qa.y),
            0.5f * dt * (-dv_ang_a.x * qa.z + dv_ang_a.y * qa.w + dv_ang_a.z * qa.x),
            0.5f * dt * (dv_ang_a.x * qa.y - dv_ang_a.y * qa.x + dv_ang_a.z * qa.w),
            0.5f * dt * (-dv_ang_a.x * qa.x - dv_ang_a.y * qa.y - dv_ang_a.z * qa.z)
        };
        qa.x += dqa.x; qa.y += dqa.y; qa.z += dqa.z; qa.w += dqa.w;
        bodies_mut[c->body_a].orientation = quat_normalize_safe(qa, 1e-12f);

        phys_quat_t dqb = {
            0.5f * dt * (dv_ang_b.x * qb.w + dv_ang_b.y * qb.z - dv_ang_b.z * qb.y),
            0.5f * dt * (-dv_ang_b.x * qb.z + dv_ang_b.y * qb.w + dv_ang_b.z * qb.x),
            0.5f * dt * (dv_ang_b.x * qb.y - dv_ang_b.y * qb.x + dv_ang_b.z * qb.w),
            0.5f * dt * (-dv_ang_b.x * qb.x - dv_ang_b.y * qb.y - dv_ang_b.z * qb.z)
        };
        qb.x += dqb.x; qb.y += dqb.y; qb.z += dqb.z; qb.w += dqb.w;
        bodies_mut[c->body_b].orientation = quat_normalize_safe(qb, 1e-12f);
    }
}

/**
 * @brief Solve one constraint: all rows including friction cone
 *        and split-impulse position correction.
 */
static void solve_one_full(tgs_color_shared_t *shared, uint32_t c_idx)
{
    phys_constraint_t *c = &shared->constraints[c_idx];

    phys_velocity_t *va = &shared->velocities[c->body_a];
    phys_velocity_t *vb = &shared->velocities[c->body_b];

    float inv_mass_a = shared->bodies[c->body_a].inv_mass;
    float inv_mass_b = shared->bodies[c->body_b].inv_mass;

    phys_mat3_t fallback_a, fallback_b;
    const phys_mat3_t *inv_i_a;
    const phys_mat3_t *inv_i_b;
    if (shared->inv_inertia_world) {
        inv_i_a = &shared->inv_inertia_world[c->body_a];
        inv_i_b = &shared->inv_inertia_world[c->body_b];
    } else {
        fallback_a = phys_mat3_inv_inertia_world(
            shared->bodies[c->body_a].orientation,
            shared->bodies[c->body_a].inv_inertia_diag);
        fallback_b = phys_mat3_inv_inertia_world(
            shared->bodies[c->body_b].orientation,
            shared->bodies[c->body_b].inv_inertia_diag);
        inv_i_a = &fallback_a;
        inv_i_b = &fallback_b;
    }

    if (c->is_joint) {
        /* Coupled implicit solver: when bodies_mut is available, use the
         * coupled update that modifies position+velocity together. */
        if (shared->bodies_mut) {
            float dt = (shared->inv_dt > 0.0f)
                     ? (1.0f / shared->inv_dt) : 0.0f;
            solve_joint_coupled_par(c, shared->velocities,
                                    shared->bodies_mut,
                                    shared->inv_inertia_world,
                                    dt, shared->inv_dt);
            return;
        }

        /* Debug: dump full Jacobian before solving. */
        debug_dump_joint_rows(c, va, vb, c_idx);

        /* Fallback: standard TGS velocity-level solve with Baumgarte
         * leak + split-impulse position correction. */
        phys_vec3_t vel_a = va->linear;
        phys_vec3_t vel_b = vb->linear;
        float spd_a = vec3_dot(vel_a, vel_a);
        float spd_b = vec3_dot(vel_b, vel_b);
        float max_spd2 = spd_a > spd_b ? spd_a : spd_b;

        const float baumgarte_lo2 = 5.0f * 5.0f;
        const float baumgarte_hi2 = 60.0f * 60.0f;
        const float baumgarte_max = 0.6f;
        float baumgarte = 0.0f;
        if (max_spd2 > baumgarte_lo2) {
            float t = (max_spd2 - baumgarte_lo2)
                    / (baumgarte_hi2 - baumgarte_lo2);
            if (t > 1.0f) { t = 1.0f; }
            baumgarte = baumgarte_max * t;
        }

        float saved_bias[PHYS_MAX_CONSTRAINT_ROWS];
        float saved_eff_mass[PHYS_MAX_CONSTRAINT_ROWS];
        const float anim_softness = (c->is_joint == 1) ? 0.5f : 1.0f;

        for (uint8_t r = 0; r < c->row_count; r++) {
            saved_bias[r] = c->rows[r].bias;
            saved_eff_mass[r] = c->rows[r].effective_mass;
            c->rows[r].effective_mass *= anim_softness;
            float row_baumgarte = (c->rows[r].flags & PHYS_ROW_FLAG_ANGULAR)
                                ? baumgarte * 0.05f
                                : baumgarte;
            c->rows[r].bias = -saved_bias[r] * shared->inv_dt * row_baumgarte;
        }

        for (uint8_t r = 0; r < c->row_count; r++) {
            solve_row(&c->rows[r], va, vb,
                      inv_mass_a, inv_i_a, inv_mass_b, inv_i_b,
                      1.0f / shared->inv_dt, JOINT_SOR_OMEGA);
        }

        for (uint8_t r = 0; r < c->row_count; r++) {
            c->rows[r].bias = saved_bias[r];
            c->rows[r].effective_mass = saved_eff_mass[r];
        }
        if (shared->pseudo_velocities) {
            /* Compliance softening for position correction only.
             * In split-impulse TGS, compliance reduces the strength of
             * positional drift correction (elastic response) without
             * weakening the velocity-level constraint solve.
             *   m_soft = m_eff / (1 + α * m_eff * inv_dt²)
             * Use per-body effective inv_dt that accounts for tiered
             * substep scaling (pseudo_velocities are scaled by
             * tier_substeps/max_substeps before integration). */
            float eff_inv_dt = shared->inv_dt;
            if (shared->tier_substep_counts && shared->tick_dt > 0.0f) {
                uint8_t t_a = shared->bodies[c->body_a].tier;
                uint8_t t_b = shared->bodies[c->body_b].tier;
                uint32_t ts_a = shared->tier_substep_counts[t_a];
                uint32_t ts_b = shared->tier_substep_counts[t_b];
                uint32_t ts = (ts_a < ts_b) ? ts_a : ts_b;
                if (ts == 0) { ts = 1; }
                eff_inv_dt = (float)ts / shared->tick_dt;
            }
            const float comp_hard  = c->compliance * eff_inv_dt * eff_inv_dt;
            const float comp_drive = c->drive_compliance * eff_inv_dt * eff_inv_dt;
            for (uint8_t r = 0; r < c->row_count; r++) {
                float compliance_factor = (c->rows[r].flags & PHYS_ROW_FLAG_DRIVE)
                                        ? comp_drive : comp_hard;
                float m_save = c->rows[r].effective_mass;
                if (compliance_factor > 0.0f) {
                    float m = m_save / (1.0f + compliance_factor * m_save);
                    c->rows[r].effective_mass = m;
                }
                solve_joint_position_row(
                    &c->rows[r],
                    &shared->pseudo_velocities[c->body_a],
                    &shared->pseudo_velocities[c->body_b],
                    shared->inv_dt,
                    inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
                c->rows[r].effective_mass = m_save;
            }
        }
        return;
    }

    /* Normal row. */
    solve_row(&c->rows[0], va, vb,
              inv_mass_a, inv_i_a, inv_mass_b, inv_i_b,
              1.0f / shared->inv_dt, SOR_OMEGA);

    {
        /* Split impulse position correction. */
        if (shared->pseudo_velocities) {
            solve_position_row(
                &c->rows[0],
                &shared->pseudo_velocities[c->body_a],
                &shared->pseudo_velocities[c->body_b],
                c->penetration, shared->slop, shared->inv_dt,
                inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
        }

        /* Friction cone. */
        float friction_limit = c->friction * c->rows[0].lambda;
        for (uint8_t r = 1; r < c->row_count; r++) {
            c->rows[r].lambda_min = -friction_limit;
            c->rows[r].lambda_max =  friction_limit;
            solve_row(&c->rows[r], va, vb,
                      inv_mass_a, inv_i_a, inv_mass_b, inv_i_b,
                      1.0f / shared->inv_dt, SOR_OMEGA);
        }
    }
}

/**
 * @brief Job function: solve a batch of same-color constraints.
 *
 * batch->start is the offset into color_constraint_indices,
 * batch->count is how many constraints in this batch.
 */
static void tgs_color_batch_job(void *data)
{
    phys_job_batch_t *batch = data;
    tgs_color_shared_t *shared = batch->user_args;

    for (uint32_t i = 0; i < batch->count; ++i) {
        uint32_t c_idx = shared->constraint_indices[batch->start + i];
        solve_one_full(shared, c_idx);
    }
}

/* ── Solve a single island (static helper) ────────────────────── */

/**
 * @brief Check if an island uses the coupled implicit solver (TIER_ANIM).
 */
static bool island_is_coupled_(const phys_island_t *island,
                                const phys_body_t *bodies,
                                uint32_t body_count,
                                const phys_body_t *bodies_mut)
{
    if (!bodies_mut || !island || !bodies) return false;
    for (uint32_t b = 0; b < island->body_count; ++b) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        if (bodies[idx].tier == PHYS_TIER_ANIM && bodies[idx].inv_mass > 0.0f) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Final position projection for coupled islands (parallel version).
 *
 * After the iterative coupled solve, run FK propagation passes to snap
 * all joint anchors to exact coincidence, eliminating residual drift.
 */
static void coupled_position_projection_par_(
    const phys_island_t *island,
    phys_constraint_t *constraints,
    const uint32_t *constraint_joint_indices,
    phys_joint_t *joints,
    uint32_t joint_count,
    phys_body_t *bodies_mut,
    uint32_t body_count)
{
    if (!constraint_joint_indices || !joints || !bodies_mut) return;

    for (int pass = 0; pass < 5; ++pass) {
        uint32_t last_ji = UINT32_MAX;
        for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
            uint32_t c_idx = island->constraint_indices[ci];
            phys_constraint_t *c = &constraints[c_idx];
            if (!c->is_joint) continue;

            uint32_t ji = constraint_joint_indices[c_idx];
            if (ji >= joint_count) continue;
            if (ji == last_ji) continue;
            last_ji = ji;

            phys_joint_t *j = &joints[ji];
            uint32_t ba = j->body_a;
            uint32_t bb = j->body_b;
            if (ba >= body_count || bb >= body_count) continue;
            if (bodies_mut[bb].inv_mass <= 0.0f) continue;

            if (j->type == PHYS_JOINT_DISTANCE) continue;

            if (j->type == PHYS_JOINT_LOCK) {
                bodies_mut[bb].orientation = quat_normalize_safe(
                    quat_mul(bodies_mut[ba].orientation,
                              j->rest_relative_orient),
                    1e-12f);
            }

            /* Cone-twist: clamp child orientation to within limits. */
            if (j->type == PHYS_JOINT_CONE_TWIST && j->limit_axes) {
                phys_quat_t q_cur = quat_normalize_safe(
                    quat_mul(bodies_mut[bb].orientation,
                             quat_conjugate(bodies_mut[ba].orientation)),
                    1e-12f);
                phys_quat_t q_err = quat_normalize_safe(
                    quat_mul(quat_conjugate(j->rest_relative_orient), q_cur),
                    1e-12f);
                if (q_err.w < 0.0f) {
                    q_err.x = -q_err.x; q_err.y = -q_err.y;
                    q_err.z = -q_err.z; q_err.w = -q_err.w;
                }
                float ex = atan2f(2.0f*(q_err.w*q_err.x + q_err.y*q_err.z),
                                  1.0f - 2.0f*(q_err.x*q_err.x + q_err.y*q_err.y));
                float sy = 2.0f*(q_err.w*q_err.y - q_err.z*q_err.x);
                if (sy >  1.0f) sy =  1.0f;
                if (sy < -1.0f) sy = -1.0f;
                float ey = asinf(sy);
                float ez = atan2f(2.0f*(q_err.w*q_err.z + q_err.x*q_err.y),
                                  1.0f - 2.0f*(q_err.y*q_err.y + q_err.z*q_err.z));
                float angles[3] = {ex, ey, ez};
                bool clamped = false;
                for (int ax = 0; ax < 3; ++ax) {
                    if (!(j->limit_axes & (1u << ax))) continue;
                    if (angles[ax] < j->limit_min[ax]) {
                        angles[ax] = j->limit_min[ax]; clamped = true;
                    } else if (angles[ax] > j->limit_max[ax]) {
                        angles[ax] = j->limit_max[ax]; clamped = true;
                    }
                }
                if (clamped) {
                    phys_quat_t q_clamped = quat_from_euler(
                        angles[0], angles[1], angles[2]);
                    bodies_mut[bb].orientation = quat_normalize_safe(
                        quat_mul(bodies_mut[ba].orientation,
                            quat_mul(j->rest_relative_orient, q_clamped)),
                        1e-12f);
                }
            }

            phys_vec3_t wa = vec3_add(bodies_mut[ba].position,
                quat_rotate_vec3(bodies_mut[ba].orientation,
                                  j->local_anchor_a));
            phys_vec3_t child_anchor_world = quat_rotate_vec3(
                bodies_mut[bb].orientation, j->local_anchor_b);
            bodies_mut[bb].position = vec3_sub(wa, child_anchor_world);
        }
    }
}

/**
 * @brief Propagate position/orientation corrections through the joint
 *        hierarchy (FK pass) so children follow parent corrections.
 */
static void propagate_coupled_anchors_par_(
    const phys_island_t *island,
    phys_constraint_t *constraints,
    const uint32_t *constraint_joint_indices,
    phys_joint_t *joints,
    uint32_t joint_count,
    phys_body_t *bodies_mut,
    uint32_t body_count)
{
    if (!constraint_joint_indices || !joints || !bodies_mut) return;

    uint32_t last_ji = UINT32_MAX;
    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t c_idx = island->constraint_indices[ci];
        phys_constraint_t *c = &constraints[c_idx];
        if (!c->is_joint) continue;

        uint32_t ji = constraint_joint_indices[c_idx];
        if (ji >= joint_count) continue;
        if (ji == last_ji) continue;
        last_ji = ji;

        phys_joint_t *j = &joints[ji];
        uint32_t ba = j->body_a;
        uint32_t bb = j->body_b;
        if (ba >= body_count || bb >= body_count) continue;
        if (bodies_mut[bb].inv_mass <= 0.0f) continue;

        if (j->type == PHYS_JOINT_DISTANCE) continue;

        if (j->type == PHYS_JOINT_LOCK) {
            bodies_mut[bb].orientation = quat_normalize_safe(
                quat_mul(bodies_mut[ba].orientation,
                          j->rest_relative_orient),
                1e-12f);
        }

        phys_vec3_t wa = vec3_add(bodies_mut[ba].position,
            quat_rotate_vec3(bodies_mut[ba].orientation,
                              j->local_anchor_a));
        phys_vec3_t child_anchor_world = quat_rotate_vec3(
            bodies_mut[bb].orientation, j->local_anchor_b);
        bodies_mut[bb].position = vec3_sub(wa, child_anchor_world);
    }
}

/**
 * @brief Recompute world-space inverse inertia for island bodies.
 */
static void recompute_island_inertia_par_(const phys_island_t *island,
                                           const phys_body_t *bodies,
                                           phys_mat3_t *inv_inertia_world,
                                           uint32_t body_count)
{
    if (!inv_inertia_world) return;
    for (uint32_t b = 0; b < island->body_count; ++b) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        if (bodies[idx].inv_mass <= 0.0f) continue;
        inv_inertia_world[idx] = phys_mat3_inv_inertia_world(
            bodies[idx].orientation, bodies[idx].inv_inertia_diag);
    }
}

/**
 * @brief Solve one island: iterate over its constraints for the
 *        configured number of solver iterations.
 *
 * For coupled (TIER_ANIM) islands, adds pre-integration predict,
 * inter-iteration Jacobian rebuild for all constraints, and
 * velocity writeback.
 */
static void solve_island(const tgs_solve_shared_t *shared,
                          const phys_island_t *island)
{
    if (island->sleeping || island->skip) return;

    /* Skip XPBD islands — they are solved by the XPBD position solver. */
    if (island->constraint_count > 0 &&
        island_routes_xpbd_(island, shared->constraints, shared->bodies)) {
        return;
    }

    /* Adaptive iteration count based on island body velocity. */
    uint32_t iters = compute_island_iterations(
        island, shared->bodies, shared->velocities, shared->iterations);

    /* Check if this is a coupled (TIER_ANIM) island. */
    bool coupled = island_is_coupled_(island, shared->bodies,
                                       shared->body_count, shared->bodies_mut);

    /* Build rebuild args for coupled islands. */
    phys_constraint_rebuild_args_t rebuild_args;
    if (coupled) {
        rebuild_args = (phys_constraint_rebuild_args_t){
            .constraints             = shared->constraints,
            .constraint_count        = UINT32_MAX,
            .constraint_joint_indices = shared->constraint_joint_indices,
            .joints                  = shared->joints,
            .joint_count             = shared->joint_count,
            .bodies                  = shared->bodies_mut,
            .body_count              = shared->body_count,
            .manifolds               = shared->manifolds,
            .manifold_count          = shared->manifold_count,
            .inv_inertia_world       = shared->inv_inertia_world_mut,
            .dt                      = shared->dt,
            .baumgarte               = shared->baumgarte,
            .slop                    = shared->slop,
        };
    }

    for (uint32_t iter = 0; iter < iters; iter++) {
        /* Coupled islands: FK propagation then Jacobian rebuild.
         * Propagate parent corrections to children, then rebuild
         * world-space anchors and constraint data. */
        if (coupled) {
            propagate_coupled_anchors_par_(
                island, shared->constraints,
                shared->constraint_joint_indices,
                shared->joints, shared->joint_count,
                shared->bodies_mut, shared->body_count);
            recompute_island_inertia_par_(island, shared->bodies_mut,
                                           shared->inv_inertia_world_mut,
                                           shared->body_count);
            phys_rebuild_island_all_constraints(island, &rebuild_args);
        }

        for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
            uint32_t c_idx = island->constraint_indices[ci];
            solve_one_full(&(tgs_color_shared_t){
                .constraints       = shared->constraints,
                .bodies            = shared->bodies,
                .inv_inertia_world = shared->inv_inertia_world,
                .velocities        = shared->velocities,
                .pseudo_velocities = shared->pseudo_velocities,
                .slop              = shared->slop,
                .inv_dt            = shared->inv_dt,
                .tick_dt           = shared->tick_dt,
                .tier_substep_counts = shared->tier_substep_counts,
                .bodies_mut        = shared->bodies_mut,
            }, c_idx);
        }
    }

    /* Coupled islands: the solver wrote final positions directly.
     * Run position projection, write back velocities, mark skip_body. */
    if (coupled) {
        coupled_position_projection_par_(
            island, shared->constraints,
            shared->constraint_joint_indices,
            shared->joints, shared->joint_count,
            shared->bodies_mut, shared->body_count);

        for (uint32_t b = 0; b < island->body_count; ++b) {
            uint32_t idx = island->body_indices[b];
            if (idx >= shared->body_count) continue;
            if (shared->bodies_mut[idx].inv_mass <= 0.0f) continue;
            shared->bodies_mut[idx].linear_vel = shared->velocities[idx].linear;
            shared->bodies_mut[idx].angular_vel = shared->velocities[idx].angular;
            if (shared->skip_body) {
                shared->skip_body[idx] = 1;
            }
        }
    }
}

/* ── Job function: solve a range of small islands ─────────────── */

/**
 * @brief Job function: solve multiple islands in one fiber.
 *
 * batch->start is the first index into small_island_indices[],
 * batch->count is how many islands to solve.
 * user_args points to a tgs_island_batch_ctx_t.
 */
typedef struct tgs_island_batch_ctx {
    const tgs_solve_shared_t *shared;
    const uint32_t           *island_indices; /**< Array of island indices. */
} tgs_island_batch_ctx_t;

static void tgs_solve_island_batch_job(void *data)
{
    phys_job_batch_t *batch = data;
    tgs_island_batch_ctx_t *bctx = batch->user_args;

    for (uint32_t i = 0; i < batch->count; ++i) {
        uint32_t island_idx = bctx->island_indices[batch->start + i];
        const phys_island_t *island =
            &bctx->shared->islands->islands[island_idx];
        solve_island(bctx->shared, island);
    }
}

/* ── Graph-colored parallel solve for a single large island ───── */

/**
 * @brief Solve a large island using graph coloring for parallelism.
 *
 * Colors the island's constraints so same-color constraints share no
 * bodies.  For each solver iteration, dispatches each color batch as
 * parallel jobs with a barrier between colors.
 *
 * @return true if coloring succeeded, false to fall back to sequential.
 */
static bool solve_island_colored_par(const tgs_solve_shared_t *shared,
                                      const phys_island_t *island,
                                      phys_job_context_t *ctx,
                                      phys_frame_arena_t *arena)
{
    uint32_t n = island->constraint_count;
    if (n == 0) { return true; }

    /* Skip XPBD islands — they are solved by the XPBD position solver. */
    if (island_routes_xpbd_(island, shared->constraints, shared->bodies)) {
        return true;
    }

    /* Build contiguous constraint copy for coloring (coloring needs
     * contiguous body_a/body_b indexing). */
    phys_constraint_t *local = phys_frame_arena_alloc(
        arena, n * sizeof(phys_constraint_t), _Alignof(phys_constraint_t));
    if (!local) { return false; }

    for (uint32_t ci = 0; ci < n; ++ci) {
        local[ci] = shared->constraints[island->constraint_indices[ci]];
    }

    /* Color the local constraints. */
    phys_color_result_t coloring;
    if (phys_constraint_color(local, n, shared->islands->uf_size,
                               arena, &coloring) != 0) {
        return false;
    }

    /* Build per-color index lists (global constraint indices grouped
     * by color for dispatch). */
    uint32_t *color_counts = phys_frame_arena_alloc(
        arena, coloring.num_colors * sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t *color_offsets = phys_frame_arena_alloc(
        arena, coloring.num_colors * sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t *sorted_indices = phys_frame_arena_alloc(
        arena, n * sizeof(uint32_t), _Alignof(uint32_t));
    if (!color_counts || !color_offsets || !sorted_indices) { return false; }

    memset(color_counts, 0, coloring.num_colors * sizeof(uint32_t));
    for (uint32_t ci = 0; ci < n; ++ci) {
        color_counts[coloring.colors[ci]]++;
    }

    /* Prefix sum for offsets. */
    color_offsets[0] = 0;
    for (uint32_t c = 1; c < coloring.num_colors; ++c) {
        color_offsets[c] = color_offsets[c - 1] + color_counts[c - 1];
    }

    /* Fill sorted_indices: global constraint index grouped by color. */
    uint32_t *fill = phys_frame_arena_alloc(
        arena, coloring.num_colors * sizeof(uint32_t), _Alignof(uint32_t));
    if (!fill) { return false; }
    memcpy(fill, color_offsets, coloring.num_colors * sizeof(uint32_t));

    for (uint32_t ci = 0; ci < n; ++ci) {
        uint32_t color = coloring.colors[ci];
        sorted_indices[fill[color]++] = island->constraint_indices[ci];
    }

    /* Allocate batch array (reused across colors/iterations). */
    uint32_t max_batch_count = n; /* upper bound */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, max_batch_count * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) { return false; }

    /* Shared context for colored constraint jobs. */
    tgs_color_shared_t color_shared = {
        .constraints        = shared->constraints,
        .bodies             = shared->bodies,
        .inv_inertia_world  = shared->inv_inertia_world,
        .velocities         = shared->velocities,
        .pseudo_velocities  = shared->pseudo_velocities,
        .slop               = shared->slop,
        .inv_dt             = shared->inv_dt,
        .tick_dt            = shared->tick_dt,
        .tier_substep_counts = shared->tier_substep_counts,
        .constraint_indices = sorted_indices,
        .bodies_mut         = shared->bodies_mut,
    };

    /* Adaptive iteration count based on island body velocity. */
    uint32_t iters = compute_island_iterations(
        island, shared->bodies, shared->velocities, shared->iterations);

    /* Solve: for each iteration, for each color, dispatch + barrier. */
    for (uint32_t iter = 0; iter < iters; ++iter) {
        for (uint32_t color = 0; color < coloring.num_colors; ++color) {
            uint32_t count = color_counts[color];
            if (count == 0) { continue; }

            uint32_t offset = color_offsets[color];

            /* Dynamic batch size for per-color dispatch. Inline
             * threshold matches the serial fast path (1 batch). */
            uint32_t batch_size = phys_batch_size(ctx, count, 8, 0);

            /* If only one batch would result, solve inline. */
            uint32_t num_batches = (count + batch_size - 1) / batch_size;
            if (num_batches <= 1) {
                for (uint32_t i = 0; i < count; ++i) {
                    solve_one_full(&color_shared,
                                   sorted_indices[offset + i]);
                }
                continue;
            }

            /* Reset counter for this dispatch. */
            job_counter_destroy(&ctx->counters[PHYS_STAGE_TGS_SOLVE]);
            job_counter_init(&ctx->counters[PHYS_STAGE_TGS_SOLVE], 0);

            uint32_t remaining = count;
            for (uint32_t b = 0; b < num_batches; ++b) {
                uint32_t bc = (remaining < batch_size) ? remaining : batch_size;
                batches[b].user_args  = &color_shared;
                batches[b].start      = offset + b * batch_size;
                batches[b].count      = bc;
                batches[b].batch_idx  = b;

                job_dispatch_named(ctx->job_sys,
                                   tgs_color_batch_job, &batches[b], 0,
                                   &ctx->counters[PHYS_STAGE_TGS_SOLVE],
                                   "phys:tgs_color");
                remaining -= bc;
            }

            /* Barrier: wait for all same-color jobs before next color. */
            job_wait_counter(&ctx->counters[PHYS_STAGE_TGS_SOLVE], 128);
        }
    }

    return true;
}

/* ── Nonlinear joint position projection ──────────────────────────── */

/** Minimum anchor error (meters) to trigger nonlinear projection. */
#define NL_PROJ_MIN_ERROR 0.01f
/** Number of nonlinear projection passes after TGS iterations. */
#define NL_PROJ_PASSES 4
/** Fraction of error corrected per nonlinear projection pass. */
#define NL_PROJ_FRACTION 0.8f

/**
 * @brief Integrate a quaternion by an angular velocity over dt.
 */
static phys_quat_t tgs_par_quat_integrate(phys_quat_t q, phys_vec3_t w,
                                            float dt) {
    phys_quat_t omega_q = { w.x, w.y, w.z, 0.0f };
    phys_quat_t dq = quat_mul(omega_q, q);
    float half_dt = 0.5f * dt;
    phys_quat_t result = {
        q.x + dq.x * half_dt,
        q.y + dq.y * half_dt,
        q.z + dq.z * half_dt,
        q.w + dq.w * half_dt,
    };
    return quat_normalize_safe(result, 1e-8f);
}

/**
 * @brief Nonlinear position projection for joints after TGS iterations.
 *
 * Recomputes world anchors from predicted body state (pos + pseudo*dt)
 * and applies corrections to pseudo-velocities that account for lever
 * arm rotation.  See tgs_solve.c project_joints_nonlinear for details.
 */
static void tgs_project_joints_nonlinear(const phys_joint_t *joints,
                                          uint32_t joint_count,
                                          const struct phys_body *bodies,
                                          phys_velocity_t *pseudo,
                                          uint32_t body_count,
                                          float dt)
{
    if (!joints || joint_count == 0 || !pseudo || dt <= 0.0f) return;

    const float inv_dt = 1.0f / dt;

    for (uint32_t pass = 0; pass < NL_PROJ_PASSES; pass++) {
        for (uint32_t ji = 0; ji < joint_count; ji++) {
            const phys_joint_t *j = &joints[ji];
            if (j->body_a >= body_count || j->body_b >= body_count) continue;

            const phys_body_t *ba = &bodies[j->body_a];
            const phys_body_t *bb = &bodies[j->body_b];

            phys_vec3_t pos_a = vec3_add(ba->position,
                vec3_scale(pseudo[j->body_a].linear, dt));
            phys_vec3_t pos_b = vec3_add(bb->position,
                vec3_scale(pseudo[j->body_b].linear, dt));

            phys_quat_t ori_a = tgs_par_quat_integrate(
                ba->orientation, pseudo[j->body_a].angular, dt);
            phys_quat_t ori_b = tgs_par_quat_integrate(
                bb->orientation, pseudo[j->body_b].angular, dt);

            phys_vec3_t rA = quat_rotate_vec3(ori_a, j->local_anchor_a);
            phys_vec3_t rB = quat_rotate_vec3(ori_b, j->local_anchor_b);
            phys_vec3_t wa = vec3_add(pos_a, rA);
            phys_vec3_t wb = vec3_add(pos_b, rB);

            phys_vec3_t error = vec3_sub(wb, wa);

            if (j->type == PHYS_JOINT_DISTANCE) {
                float dist = vec3_magnitude(error);
                if (dist < 1e-7f) continue;
                float scalar_err = dist - j->rest_length;
                if (fabsf(scalar_err) < NL_PROJ_MIN_ERROR) continue;
                phys_vec3_t dir = vec3_scale(error, 1.0f / dist);
                error = vec3_scale(dir, scalar_err);
            } else {
                float err_mag = vec3_magnitude(error);
                if (err_mag < NL_PROJ_MIN_ERROR) continue;
            }

            float im_a = ba->inv_mass;
            float im_b = bb->inv_mass;
            float im_total = im_a + im_b;
            if (im_total < 1e-12f) continue;

            float frac_a = im_a / im_total;
            float frac_b = im_b / im_total;
            phys_vec3_t correction = vec3_scale(error, NL_PROJ_FRACTION);

            if (im_a > 0.0f) {
                pseudo[j->body_a].linear = vec3_add(
                    pseudo[j->body_a].linear,
                    vec3_scale(correction, frac_a * inv_dt));
            }
            if (im_b > 0.0f) {
                pseudo[j->body_b].linear = vec3_sub(
                    pseudo[j->body_b].linear,
                    vec3_scale(correction, frac_b * inv_dt));
            }

            float rA_len2 = vec3_dot(rA, rA);
            if (im_a > 0.0f && rA_len2 > 1e-6f) {
                phys_vec3_t r_cross_e = vec3_cross(rA, correction);
                phys_vec3_t ang_corr = vec3_scale(r_cross_e,
                    frac_a * inv_dt / rA_len2);
                pseudo[j->body_a].angular = vec3_add(
                    pseudo[j->body_a].angular, ang_corr);
            }

            float rB_len2 = vec3_dot(rB, rB);
            if (im_b > 0.0f && rB_len2 > 1e-6f) {
                phys_vec3_t r_cross_e = vec3_cross(rB, correction);
                phys_vec3_t ang_corr = vec3_scale(r_cross_e,
                    -frac_b * inv_dt / rB_len2);
                pseudo[j->body_b].angular = vec3_add(
                    pseudo[j->body_b].angular, ang_corr);
            }
        }
    }
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
                !phys_body_is_sleeping(&args->bodies[i]) &&
                !(args->bodies[i].flags & PHYS_BODY_FLAG_NO_GRAVITY)) {
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
        .islands           = args->islands,
        .constraints       = args->constraints,
        .bodies            = args->bodies,
        .inv_inertia_world = args->inv_inertia_world,
        .velocities        = args->velocities,
        .pseudo_velocities = pseudo,
        .iterations        = args->iterations,
        .body_count        = args->body_count,
        .slop              = args->slop,
        .inv_dt            = inv_dt,
        .dt                = args->dt,
        .tick_dt           = args->tick_dt,
        .tier_substep_counts = args->tier_substep_counts,
        .bodies_mut        = args->bodies_mut,
        .inv_inertia_world_mut = args->inv_inertia_world_mut,
        .joints            = args->joints,
        .joint_count       = args->joint_count,
        .constraint_joint_indices = args->constraint_joint_indices,
        .skip_body         = args->skip_body,
        .manifolds         = args->manifolds,
        .manifold_count    = args->manifold_count,
        .baumgarte         = args->baumgarte,
        .frame_arena       = arena,
    };

    /* Separate large islands (colored parallel) from small (one-job). */
    uint32_t threshold = args->island_color_threshold;
    if (threshold == 0) { threshold = COLOR_THRESHOLD; }

    /* First pass: solve large islands with colored parallel dispatch.
     * Track which islands are small for the second pass. */
    uint32_t small_count = 0;
    for (uint32_t i = 0; i < island_count; ++i) {
        const phys_island_t *island = &args->islands->islands[i];
        if (island->constraint_count >= threshold) {
            /* Large island: colored parallel solve. */
            if (!solve_island_colored_par(&shared, island, ctx, arena)) {
                /* Coloring failed — fall back to sequential for this island. */
                solve_island(&shared, island);
            }
        } else {
            small_count++;
        }
    }

    /* Second pass: batch small islands into fewer fiber dispatches. */
    if (small_count > 0) {
        /* Collect small island indices into a contiguous array. */
        uint32_t *small_indices = phys_frame_arena_alloc(
            arena, small_count * sizeof(uint32_t), _Alignof(uint32_t));
        if (!small_indices) {
            /* Arena exhausted — solve remaining islands inline. */
            for (uint32_t i = 0; i < island_count; ++i) {
                const phys_island_t *island = &args->islands->islands[i];
                if (island->constraint_count < threshold) {
                    solve_island(&shared, island);
                }
            }
            return;
        }

        uint32_t si = 0;
        for (uint32_t i = 0; i < island_count; ++i) {
            if (args->islands->islands[i].constraint_count < threshold) {
                small_indices[si++] = i;
            }
        }

        /* Batch context shared by all small-island jobs. */
        tgs_island_batch_ctx_t bctx = {
            .shared         = &shared,
            .island_indices = small_indices,
        };

        /* Dynamic batch size — target 2× workers, floor at 8 islands. */
        uint32_t batch_size = phys_batch_size(ctx, small_count, 8, 0);
        uint32_t num_batches = (small_count + batch_size - 1) / batch_size;
        phys_job_batch_t *small_batches = phys_frame_arena_alloc(
            arena, num_batches * sizeof(phys_job_batch_t),
            _Alignof(phys_job_batch_t));
        if (!small_batches) {
            /* Arena exhausted — solve inline. */
            for (uint32_t i = 0; i < small_count; ++i) {
                const phys_island_t *island =
                    &args->islands->islands[small_indices[i]];
                solve_island(&shared, island);
            }
            return;
        }

        phys_dispatch_stage(ctx, PHYS_STAGE_TGS_SOLVE,
                            tgs_solve_island_batch_job, &bctx,
                            small_count, batch_size, small_batches);
        phys_wait_stage(ctx, PHYS_STAGE_TGS_SOLVE);
    }

    /* Nonlinear joint position projection: after all TGS iterations,
     * recompute world anchors from predicted body state and apply
     * corrections that account for how rotation affects lever arms.
     * Runs single-threaded since it's a small number of joints. */
    if (pseudo && args->joints && args->joint_count > 0) {
        tgs_project_joints_nonlinear(args->joints, args->joint_count,
                                      args->bodies, pseudo,
                                      args->body_count, args->dt);
    }
}

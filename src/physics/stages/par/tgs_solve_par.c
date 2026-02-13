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
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/constraint_color.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/** Minimum penetration excess to correct (avoids micro-jitter). */
#define SPLIT_MIN_PHI 1e-6f

/** Min constraints per island to use graph-colored parallel dispatch. */
#define COLOR_THRESHOLD 128

/** Speed (m/s) above which we start adding solver iterations. */
#define ADAPTIVE_SPEED_LOW  5.0f
/** Speed (m/s) at which we reach maximum solver iterations. */
#define ADAPTIVE_SPEED_HIGH 200.0f
/** Maximum multiplier on base iteration count for fast islands. */
#define ADAPTIVE_ITER_MULT  5

/** Successive over-relaxation factor.  Values > 1.0 accelerate
 *  convergence; typical range 1.1–1.5.  Too high causes oscillation. */
#define SOR_OMEGA 1.1f

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
        return base_iters;
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
    return base_iters + extra;
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
    phys_velocity_t          *velocities;   /**< Solver velocity workspace. */
    phys_velocity_t          *pseudo_velocities; /**< Split-impulse workspace (may be NULL). */
    uint32_t                  iterations;   /**< Solver iteration count. */
    float                     slop;         /**< Penetration slop threshold. */
    float                     inv_dt;       /**< 1 / substep dt. */
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
    phys_velocity_t   *velocities;         /**< Solver velocity workspace. */
    phys_velocity_t   *pseudo_velocities;  /**< Split-impulse workspace (may be NULL). */
    float              slop;               /**< Penetration slop threshold. */
    float              inv_dt;             /**< 1 / substep dt. */
    const uint32_t    *constraint_indices; /**< Global constraint indices for this color batch. */
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
                       const phys_vec3_t *inv_i_a,
                       float inv_mass_b,
                       const phys_vec3_t *inv_i_b)
{
    /* Compute J·v (relative velocity along the constraint direction). */
    float jv = vec3_dot(row->J_va, va->linear)
             + vec3_dot(row->J_wa, va->angular)
             + vec3_dot(row->J_vb, vb->linear)
             + vec3_dot(row->J_wb, vb->angular);

    /* Impulse delta from constraint violation.
     * Linear viscous damping: opposes relative velocity along the
     * constraint axis proportional to speed. */
    float jv_damped = jv * (1.0f + row->damping);
    float delta_lambda = (row->bias - jv_damped) * row->effective_mass;

    /* Successive over-relaxation: scale impulse to accelerate convergence. */
    delta_lambda *= SOR_OMEGA;

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
                                      const phys_vec3_t *inv_i_a,
                                      float inv_mass_b,
                                      const phys_vec3_t *inv_i_b)
{
    float error = row->bias;
    if (fabsf(error) < 1e-7f) { return; }

    float pos_bias = -error * inv_dt;

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

    pva->angular.x += inv_i_a->x * row->J_wa.x * delta_lambda;
    pva->angular.y += inv_i_a->y * row->J_wa.y * delta_lambda;
    pva->angular.z += inv_i_a->z * row->J_wa.z * delta_lambda;

    pvb->angular.x += inv_i_b->x * row->J_wb.x * delta_lambda;
    pvb->angular.y += inv_i_b->y * row->J_wb.y * delta_lambda;
    pvb->angular.z += inv_i_b->z * row->J_wb.z * delta_lambda;
}

/* ── Solve a single constraint (used by colored job) ──────────── */

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
    const phys_vec3_t *inv_i_a = &shared->bodies[c->body_a].inv_inertia_diag;
    const phys_vec3_t *inv_i_b = &shared->bodies[c->body_b].inv_inertia_diag;

    if (c->is_joint) {
        /* Joint: velocity-level solve with speed-dependent Baumgarte
         * leak, then split-impulse position correction. */
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
        for (uint8_t r = 0; r < c->row_count; r++) {
            saved_bias[r] = c->rows[r].bias;
            c->rows[r].bias = -saved_bias[r] * shared->inv_dt * baumgarte;
        }

        for (uint8_t r = 0; r < c->row_count; r++) {
            solve_row(&c->rows[r], va, vb,
                      inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
        }

        for (uint8_t r = 0; r < c->row_count; r++) {
            c->rows[r].bias = saved_bias[r];
        }
        if (shared->pseudo_velocities) {
            for (uint8_t r = 0; r < c->row_count; r++) {
                solve_joint_position_row(
                    &c->rows[r],
                    &shared->pseudo_velocities[c->body_a],
                    &shared->pseudo_velocities[c->body_b],
                    shared->inv_dt,
                    inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
            }
        }
        return;
    }

    /* Normal row. */
    solve_row(&c->rows[0], va, vb,
              inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);

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
                      inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
        }
    }
}

/* ── Job function for colored constraint batch ───────────────── */

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
 * @brief Solve one island: iterate over its constraints for the
 *        configured number of solver iterations.
 */
static void solve_island(const tgs_solve_shared_t *shared,
                          const phys_island_t *island)
{
    if (island->sleeping || island->skip) return;

    /* Adaptive iteration count based on island body velocity. */
    uint32_t iters = compute_island_iterations(
        island, shared->bodies, shared->velocities, shared->iterations);

    phys_velocity_t *pseudo = shared->pseudo_velocities;

    for (uint32_t iter = 0; iter < iters; iter++) {
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

            if (c->is_joint) {
                /* Joint: velocity-level solve with bias=0, then
                 * split-impulse position correction per row. */
                float saved_bias[PHYS_MAX_CONSTRAINT_ROWS];
                for (uint8_t r = 0; r < c->row_count; r++) {
                    saved_bias[r] = c->rows[r].bias;
                    c->rows[r].bias = 0.0f;
                }

                for (uint8_t r = 0; r < c->row_count; r++) {
                    solve_row(&c->rows[r], va, vb,
                              inv_mass_a, inv_i_a,
                              inv_mass_b, inv_i_b);
                }

                for (uint8_t r = 0; r < c->row_count; r++) {
                    c->rows[r].bias = saved_bias[r];
                }
                if (pseudo) {
                    for (uint8_t r = 0; r < c->row_count; r++) {
                        solve_joint_position_row(
                            &c->rows[r],
                            &pseudo[c->body_a], &pseudo[c->body_b],
                            shared->inv_dt,
                            inv_mass_a, inv_i_a,
                            inv_mass_b, inv_i_b);
                    }
                }
            } else {
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
        .constraints      = shared->constraints,
        .bodies           = shared->bodies,
        .velocities       = shared->velocities,
        .pseudo_velocities = shared->pseudo_velocities,
        .slop             = shared->slop,
        .inv_dt           = shared->inv_dt,
        .constraint_indices = sorted_indices,
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
 * @brief Rotate a vector by a quaternion: q * v * q^-1.
 */
static phys_vec3_t tgs_par_quat_rotate(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t qv = {q.x, q.y, q.z};
    phys_vec3_t t = vec3_scale(vec3_cross(qv, v), 2.0f);
    return vec3_add(vec3_add(v, vec3_scale(t, q.w)), vec3_cross(qv, t));
}

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

            phys_vec3_t rA = tgs_par_quat_rotate(ori_a, j->local_anchor_a);
            phys_vec3_t rB = tgs_par_quat_rotate(ori_b, j->local_anchor_b);
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

/**
 * @file stabilization_par.c
 * @brief Parallel stabilization hint computation.
 *
 * Splits the manifold range into batches of PHYS_STABILIZATION_BATCH_SIZE,
 * dispatches each batch as a job.  Each job reads body state (read-only)
 * and writes to its own disjoint slice of hints_out — no merge needed.
 *
 * Non-static functions: 1 (phys_stage_stabilization_par).
 */

#include "ferrum/physics/par/stabilization_par.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"

/* ── Per-dispatch shared context ────────────────────────────────── */

/**
 * @brief Shared context passed to every stabilization batch job.
 *
 * Points to the caller-provided stabilization args so each job can
 * access the manifold array, body array, threshold, and output buffer.
 */
typedef struct stab_par_shared {
    const phys_stabilization_args_t *args; /**< Borrowed stabilization args. */
} stab_par_shared_t;

/* ── Job function ───────────────────────────────────────────────── */

/**
 * @brief Job function: compute stabilization hints for manifolds
 *        [start, start+count).
 *
 * Reads body state via args->bodies (read-only) and writes to
 * args->hints_out[start..start+count).  Each job's output range is
 * disjoint so no synchronization is required.
 */
static void stabilization_batch_job(void *data) {
    phys_job_batch_t *batch = data;
    stab_par_shared_t *shared = batch->user_args;
    const phys_stabilization_args_t *args = shared->args;

    const float threshold = args->resting_velocity_threshold;
    const float threshold_sq = threshold * threshold;

    uint32_t end = batch->start + batch->count;
    for (uint32_t i = batch->start; i < end; ++i) {
        const phys_manifold_t *m = &args->manifolds[i];
        phys_stab_hint_t *hint = &args->hints_out[i];

        const phys_body_t *body_a = &args->bodies[m->body_a];
        const phys_body_t *body_b = &args->bodies[m->body_b];

        /* Determine tier: use the higher tier (lower fidelity) body. */
        uint8_t effective_tier = body_a->tier > body_b->tier
                                     ? body_a->tier
                                     : body_b->tier;

        /* Look up per-tier stabilization factors. */
        float tier_friction_boost;
        float tier_velocity_damping;
        phys_tier_stabilization_params((phys_tier_t)effective_tier,
                                       &tier_friction_boost,
                                       &tier_velocity_damping);

        hint->friction_boost   = tier_friction_boost;
        hint->velocity_damping = tier_velocity_damping;

        /* Default to active (no resting boost). */
        hint->friction_scale    = 1.0f;
        hint->restitution_scale = 1.0f;

        if (m->point_count == 0) {
            continue;
        }
        const phys_contact_point_t *cp = &m->points[0];

        /* Lever arms from body centers to contact point. */
        vec3_t r_a = vec3_sub(cp->point_world, body_a->position);
        vec3_t r_b = vec3_sub(cp->point_world, body_b->position);

        /* Velocity at contact point for each body:
         * v = linear_vel + cross(angular_vel, r) */
        vec3_t v_a = vec3_add(body_a->linear_vel,
                              vec3_cross(body_a->angular_vel, r_a));
        vec3_t v_b = vec3_add(body_b->linear_vel,
                              vec3_cross(body_b->angular_vel, r_b));

        /* Relative velocity (A relative to B). */
        vec3_t v_rel = vec3_sub(v_a, v_b);

        /* Normal component of relative velocity. */
        float v_n = vec3_dot(v_rel, cp->normal);

        /* Tangential component squared:
         * |v_t|^2 = |v_rel|^2 - v_n^2 */
        float v_rel_sq = vec3_dot(v_rel, v_rel);
        float v_t_sq = v_rel_sq - v_n * v_n;

        /* Guard against floating-point noise producing negative values. */
        if (v_t_sq < 0.0f) {
            v_t_sq = 0.0f;
        }

        /* Classify as resting if both normal and tangential speeds
         * are below the threshold.  Apply tier friction boost. */
        if (fabsf(v_n) < threshold && v_t_sq < threshold_sq) {
            hint->friction_scale    = 3.0f * tier_friction_boost;
            hint->restitution_scale = 0.0f;
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_stabilization_par(const phys_stabilization_args_t *args,
                                   phys_job_context_t *ctx) {
    if (!args || !ctx) {
        return;
    }
    if (!args->manifolds || !args->hints_out || !args->bodies) {
        return;
    }
    if (args->manifold_count == 0) {
        return;
    }

    uint32_t total = args->manifold_count;
    uint32_t batch_size = PHYS_STABILIZATION_BATCH_SIZE;
    uint32_t num_batches = (total + batch_size - 1) / batch_size;

    /* Shared context lives on the stack — valid until wait completes. */
    stab_par_shared_t shared = {
        .args = args,
    };

    /* Batch descriptors on the stack (max ~4 for 200 manifolds).
     * For very large counts this should use arena allocation, but
     * physics manifold counts are bounded in practice. */
    phys_job_batch_t batches[num_batches];

    phys_dispatch_stage(ctx, PHYS_STAGE_STABILIZATION,
                        stabilization_batch_job, &shared,
                        total, batch_size, batches);

    phys_wait_stage(ctx, PHYS_STAGE_STABILIZATION);
}

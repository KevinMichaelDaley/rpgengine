/**
 * @file stabilization_par.c
 * @brief Parallel stabilization hint computation.
 *
 * Splits the manifold range into batches of PHYS_STABILIZATION_BATCH_SIZE,
 * dispatches each batch as a job.  Each job reads body state (read-only)
 * and writes to its own disjoint slice of hints_out — no merge needed.
 *
 * Box contacts on edges or corners are never classified as resting
 * because those configurations are inherently unstable.
 *
 * Non-static functions: 1 (phys_stage_stabilization_par).
 */

#include "ferrum/physics/par/stabilization_par.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"

/* ── Shared unstable-box helper (same logic as stabilization.c) ─── */

/** Tolerance: coordinate is "at boundary" when |coord| >= he * (1 - TOL). */
#define FACE_TOL 0.05f

/**
 * @brief Check if a box contact is on an edge or corner (unstable).
 *
 * Returns true if ≥2 of the local-space coordinates are near the
 * half-extent boundary.
 */
static int box_contact_unstable_(phys_vec3_t local, phys_vec3_t half_ext)
{
    int boundary_count = 0;
    const float coords[3] = { local.x, local.y, local.z };
    const float extents[3] = { half_ext.x, half_ext.y, half_ext.z };

    for (int a = 0; a < 3; ++a) {
        if (extents[a] <= 0.0f) {
            continue;
        }
        float limit = extents[a] * (1.0f - FACE_TOL);
        if (fabsf(coords[a]) >= limit) {
            boundary_count++;
        }
    }
    return boundary_count >= 2;
}

/**
 * @brief Check if a manifold contact is on an unstable box support.
 */
static int contact_on_unstable_box_(const phys_stabilization_args_t *args,
                                    const phys_manifold_t *m,
                                    const phys_contact_point_t *cp)
{
    if (!args->colliders || !args->boxes) {
        return 0;
    }

    const phys_collider_t *ca = &args->colliders[m->body_a];
    if (ca->type == PHYS_SHAPE_BOX) {
        phys_vec3_t he = args->boxes[ca->shape_index].half_extents;
        if (box_contact_unstable_(cp->local_a, he)) {
            return 1;
        }
    }

    const phys_collider_t *cb = &args->colliders[m->body_b];
    if (cb->type == PHYS_SHAPE_BOX) {
        phys_vec3_t he = args->boxes[cb->shape_index].half_extents;
        if (box_contact_unstable_(cp->local_b, he)) {
            return 1;
        }
    }

    return 0;
}

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
         * are below the threshold.  Apply tier friction boost.
         *
         * Exception: box contacts on edges or corners are inherently
         * unstable and must not receive resting treatment. */
        if (fabsf(v_n) < threshold && v_t_sq < threshold_sq) {
            if (!contact_on_unstable_box_(args, m, cp)) {
                hint->friction_scale    = 3.0f * tier_friction_boost;
                hint->restitution_scale = 0.0f;
            }
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_stabilization_par(const phys_stabilization_args_t *args,
                                   phys_job_context_t *ctx,
                                   phys_frame_arena_t *arena) {
    if (!args || !ctx || !arena) {
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

    /* Allocate batch descriptors from the frame arena. */
    phys_job_batch_t *batches = phys_frame_arena_alloc(
        arena, num_batches * sizeof(phys_job_batch_t),
        _Alignof(phys_job_batch_t));
    if (!batches) return;

    phys_dispatch_stage(ctx, PHYS_STAGE_STABILIZATION,
                        stabilization_batch_job, &shared,
                        total, batch_size, batches);

    phys_wait_stage(ctx, PHYS_STAGE_STABILIZATION);
}

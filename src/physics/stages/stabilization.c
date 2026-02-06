/**
 * @file stabilization.c
 * @brief Stage 8: Stabilization Hints.
 *
 * Classifies each manifold's contact as resting or active based on
 * relative velocity at the first contact point, and writes per-manifold
 * friction/restitution scale hints.
 */

#include "ferrum/physics/stabilization.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"

void phys_stage_stabilization(const phys_stabilization_args_t *args)
{
    if (!args || !args->manifolds || !args->hints_out || !args->bodies) {
        return;
    }

    const float threshold = args->resting_velocity_threshold;
    const float threshold_sq = threshold * threshold;

    for (uint32_t i = 0; i < args->manifold_count; ++i) {
        const phys_manifold_t *m = &args->manifolds[i];
        phys_stab_hint_t *hint = &args->hints_out[i];

        /* Default to active. */
        hint->friction_scale    = 1.0f;
        hint->restitution_scale = 1.0f;

        if (m->point_count == 0) {
            continue;
        }

        const phys_body_t *body_a = &args->bodies[m->body_a];
        const phys_body_t *body_b = &args->bodies[m->body_b];
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
         * are below the threshold. */
        if (fabsf(v_n) < threshold && v_t_sq < threshold_sq) {
            hint->friction_scale    = 3.0f;
            hint->restitution_scale = 0.0f;
        }
    }
}

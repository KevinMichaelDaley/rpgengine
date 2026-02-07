/**
 * @file velocity_sync.c
 * @brief Constraint-normal velocity replacement after position projection.
 *
 * After position projection corrects penetration, the body velocities
 * must be updated to reflect the corrected geometry.  Rather than naively
 * setting v = delta_q / dt (which destroys tangential friction velocity),
 * we replace only the constraint-normal component of each body's velocity
 * with the normal component implied by the position correction.
 *
 * See ref/sparse_stabilization.tex Section 5b.
 *
 * Non-static functions (1):
 *   1. phys_velocity_sync_normals
 */

#include "ferrum/physics/velocity_sync.h"

#include <stddef.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/math/vec3.h"

void phys_velocity_sync_normals(
    const phys_velocity_sync_args_t *args)
{
    if (!args || !args->island || !args->constraints ||
        !args->bodies || !args->position_deltas) {
        return;
    }

    const phys_island_t *island = args->island;
    const float dt = args->dt;
    if (dt <= 0.0f) { return; }
    if (island->sleeping) { return; }

    const float inv_dt = 1.0f / dt;

    /* Iterate constraints once (O(nc)) instead of scanning all
     * constraints per body (O(bodies × constraints)).
     *
     * For each constraint, compute the correction velocity along the
     * contact normal for both bodies, and replace that component. */
    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t con_idx = island->constraint_indices[ci];
        const phys_constraint_t *c = &args->constraints[con_idx];

        /* Extract the contact normal from the normal row's J_vb. */
        phys_vec3_t normal = c->rows[0].J_vb;

        /* Process body A (if dynamic). */
        uint32_t idx_a = c->body_a;
        phys_body_t *body_a = &args->bodies[idx_a];
        if (body_a->inv_mass > 0.0f) {
            phys_vec3_t v_corr_a = vec3_scale(args->position_deltas[idx_a],
                                               inv_dt);
            float corr_mag_sq = vec3_dot(v_corr_a, v_corr_a);
            if (corr_mag_sq > 1e-12f) {
                /* Remove old normal component, add correction normal component. */
                float v_along_n = vec3_dot(body_a->linear_vel, normal);
                body_a->linear_vel = vec3_sub(body_a->linear_vel,
                                               vec3_scale(normal, v_along_n));
                float v_corr_along_n = vec3_dot(v_corr_a, normal);
                body_a->linear_vel = vec3_add(body_a->linear_vel,
                                               vec3_scale(normal, v_corr_along_n));
            }
        }

        /* Process body B (if dynamic). */
        uint32_t idx_b = c->body_b;
        phys_body_t *body_b = &args->bodies[idx_b];
        if (body_b->inv_mass > 0.0f) {
            phys_vec3_t v_corr_b = vec3_scale(args->position_deltas[idx_b],
                                               inv_dt);
            float corr_mag_sq = vec3_dot(v_corr_b, v_corr_b);
            if (corr_mag_sq > 1e-12f) {
                float v_along_n = vec3_dot(body_b->linear_vel, normal);
                body_b->linear_vel = vec3_sub(body_b->linear_vel,
                                               vec3_scale(normal, v_along_n));
                float v_corr_along_n = vec3_dot(v_corr_b, normal);
                body_b->linear_vel = vec3_add(body_b->linear_vel,
                                               vec3_scale(normal, v_corr_along_n));
            }
        }
    }
}

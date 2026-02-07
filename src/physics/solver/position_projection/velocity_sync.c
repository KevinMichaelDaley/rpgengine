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

    /* For each body in the island, scan all constraints in the island
     * to find contact normals touching this body.  For each such normal,
     * replace the normal component of velocity with the correction's
     * normal component. */
    for (uint32_t bi = 0; bi < island->body_count; bi++) {
        uint32_t idx = island->body_indices[bi];
        phys_body_t *body = &args->bodies[idx];

        /* Skip static/kinematic bodies (inv_mass == 0). */
        if (body->inv_mass == 0.0f) { continue; }

        /* Correction velocity for this body. */
        phys_vec3_t v_corr = vec3_scale(args->position_deltas[idx], inv_dt);

        /* Check if there's any correction at all. */
        float corr_mag_sq = vec3_dot(v_corr, v_corr);
        if (corr_mag_sq < 1e-12f) { continue; }

        /* Scan island constraints for normals touching this body.
         * For each constraint, the contact normal is rows[0].J_vb
         * (the constraint direction — see constraint_build.c). */
        for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
            uint32_t con_idx = island->constraint_indices[ci];
            const phys_constraint_t *c = &args->constraints[con_idx];

            /* Does this constraint touch our body? */
            if (c->body_a != idx && c->body_b != idx) { continue; }

            /* Extract the contact normal from the normal row's J_vb. */
            phys_vec3_t normal = c->rows[0].J_vb;

            /* Remove the old normal component of velocity. */
            float v_along_n = vec3_dot(body->linear_vel, normal);
            body->linear_vel = vec3_sub(body->linear_vel,
                                        vec3_scale(normal, v_along_n));

            /* Add the correction's normal component. */
            float v_corr_along_n = vec3_dot(v_corr, normal);
            body->linear_vel = vec3_add(body->linear_vel,
                                        vec3_scale(normal, v_corr_along_n));
        }
    }
}

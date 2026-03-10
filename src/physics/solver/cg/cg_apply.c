/**
 * @file cg_apply.c
 * @brief Apply CG solution to velocities and predict body poses.
 *
 * The CG solver produces an impulse vector λ.  This module:
 *   1. Accumulates λ into constraint row lambdas.
 *   2. Applies Δv = M⁻¹ · Jᵀ · λ to velocities.
 *
 * Position prediction (integrating the full velocity into predicted
 * positions) is handled by phys_cg_predict_positions, called after
 * cg_apply each iteration.  That function does the authoritative
 * x = x_init + v_total * dt integration from saved initial poses.
 *
 * 2 non-static functions: phys_cg_apply, phys_cg_predict_positions.
 */

#include "ferrum/physics/solver/cg_solve.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"  /* mat4_t for predict_positions */

#include <math.h>
#include <stddef.h>

void phys_cg_apply(const cg_system_t *sys,
                   const phys_island_t *island,
                   phys_constraint_t *constraints,
                   phys_body_t *bodies_mut,
                   const phys_mat3_t *inv_inertia_world,
                   phys_velocity_t *velocities,
                   uint32_t body_count,
                   float dt,
                   const phys_joint_t *joints,
                   uint32_t joint_count,
                   const uint32_t *constraint_joint_indices)
{
    (void)dt; (void)joints; (void)joint_count;
    (void)constraint_joint_indices;

    if (!sys || !constraints || !bodies_mut || !velocities ||
        sys->n == 0) {
        return;
    }

    /* Accumulate Δλ into constraint row lambdas for XPBD compliance
     * feedback on subsequent iterations/substeps. */
    for (uint32_t i = 0; i < sys->n; i++) {
        uint32_t c_idx = sys->row_constraint[i];
        uint8_t r_idx = sys->row_sub[i];
        constraints[c_idx].rows[r_idx].lambda += sys->lambda[i];
    }

    /* Apply Δv = M⁻¹ · Jᵀ · Δλ per body.
     *
     * Only velocity is updated here.  Position/orientation prediction
     * is handled by phys_cg_predict_positions() which integrates the
     * full velocity (pre-existing + all solver corrections) from saved
     * initial poses: x = x_init + v_total * dt.  This must be called
     * after cg_apply every iteration. */
    for (uint32_t b = 0; b < island->body_count; b++) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        if (bodies_mut[idx].inv_mass <= 0.0f) continue;

        float inv_mass = bodies_mut[idx].inv_mass;
        const phys_mat3_t *inv_I = inv_inertia_world
            ? &inv_inertia_world[idx] : NULL;
        if (!inv_I) continue;

        phys_vec3_t dv_lin = {0.0f, 0.0f, 0.0f};
        phys_vec3_t dv_ang = {0.0f, 0.0f, 0.0f};

        for (uint32_t i = 0; i < sys->n; i++) {
            float dlam = sys->lambda[i];
            if (fabsf(dlam) < 1e-20f) continue;

            uint32_t c_idx = sys->row_constraint[i];
            const phys_constraint_t *c = &constraints[c_idx];
            const phys_jacobian_row_t *row = &c->rows[sys->row_sub[i]];

            /* Determine which side of the constraint this body is on. */
            int side = -1;
            if (c->body_a == idx) side = 0;
            else if (c->body_b == idx) side = 1;
            else continue;

            /* Compute velocity change from this row. */
            phys_vec3_t row_dv_lin, row_dv_ang;
            if (side == 0) {
                row_dv_lin = vec3_scale(row->J_va, inv_mass * dlam);
                row_dv_ang = vec3_scale(
                    phys_mat3_mul_vec3(inv_I, row->J_wa), dlam);
            } else {
                row_dv_lin = vec3_scale(row->J_vb, inv_mass * dlam);
                row_dv_ang = vec3_scale(
                    phys_mat3_mul_vec3(inv_I, row->J_wb), dlam);
            }

            dv_lin = vec3_add(dv_lin, row_dv_lin);
            dv_ang = vec3_add(dv_ang, row_dv_ang);
        }

        velocities[idx].linear = vec3_add(velocities[idx].linear, dv_lin);
        velocities[idx].angular = vec3_add(velocities[idx].angular, dv_ang);
    }
}

void phys_cg_predict_positions(const phys_island_t *island,
                               phys_body_t *bodies_mut,
                               const phys_velocity_t *velocities,
                               const phys_vec3_t *initial_positions,
                               const phys_quat_t *initial_orientations,
                               uint32_t body_count,
                               float dt)
{
    if (!island || !bodies_mut || !velocities) return;

    for (uint32_t b = 0; b < island->body_count; b++) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        if (bodies_mut[idx].inv_mass <= 0.0f) continue;

        /* Predicted position: x_init + v_total * dt.
         * This uses the FULL current velocity (including all CG
         * corrections and pre-existing velocity from gravity etc.)
         * to predict where the body will be at end-of-substep. */
        phys_vec3_t v = velocities[idx].linear;
        bodies_mut[idx].position = (phys_vec3_t){
            initial_positions[idx].x + v.x * dt,
            initial_positions[idx].y + v.y * dt,
            initial_positions[idx].z + v.z * dt
        };

        /* Predicted orientation: integrate full angular velocity
         * using the exponential map for exact SO(3) integration. */
        phys_vec3_t w = velocities[idx].angular;
        float wx = w.x * dt, wy = w.y * dt, wz = w.z * dt;
        float theta = sqrtf(wx * wx + wy * wy + wz * wz);
        phys_quat_t dq;
        if (theta > 1e-8f) {
            float half_theta = 0.5f * theta;
            float s = sinf(half_theta) / theta;
            dq.w = cosf(half_theta);
            dq.x = s * wx; dq.y = s * wy; dq.z = s * wz;
        } else {
            dq.w = 1.0f;
            dq.x = 0.5f * wx; dq.y = 0.5f * wy; dq.z = 0.5f * wz;
        }
        bodies_mut[idx].orientation = quat_normalize_safe(
            quat_mul(dq, initial_orientations[idx]), 1e-12f);

        /* Update world_transform from predicted pose. */
        mat4_t rot;
        quat_to_mat4(bodies_mut[idx].orientation, &rot);
        rot.m[12] = bodies_mut[idx].position.x;
        rot.m[13] = bodies_mut[idx].position.y;
        rot.m[14] = bodies_mut[idx].position.z;
        bodies_mut[idx].world_transform = rot;
    }
}

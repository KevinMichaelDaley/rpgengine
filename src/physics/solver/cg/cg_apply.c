/**
 * @file cg_apply.c
 * @brief Apply CG solution to velocities and body positions.
 *
 * The CG solver produces an incremental impulse vector Δλ (lambda was
 * zeroed before solve).  Apply Δv = M⁻¹·Jᵀ·Δλ to each body and
 * accumulate Δλ into constraint row lambdas for compliance feedback.
 *
 * 1 non-static function: phys_cg_apply.
 */

#include "ferrum/physics/solver/cg_solve.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stddef.h>

/**
 * @brief Integrate orientation using the exponential map.
 */
static phys_quat_t expmap_integrate(phys_quat_t q, phys_vec3_t dw, float dt)
{
    float wx = dw.x * dt, wy = dw.y * dt, wz = dw.z * dt;
    float theta = sqrtf(wx * wx + wy * wy + wz * wz);

    phys_quat_t dq;
    if (theta > 1e-8f) {
        float half = 0.5f * theta;
        float s = sinf(half) / theta;
        dq.w = cosf(half);
        dq.x = s * wx; dq.y = s * wy; dq.z = s * wz;
    } else {
        dq.w = 1.0f;
        dq.x = 0.5f * wx; dq.y = 0.5f * wy; dq.z = 0.5f * wz;
    }

    return quat_normalize_safe(quat_mul(dq, q), 1e-12f);
}

void phys_cg_apply(const cg_system_t *sys,
                   const phys_island_t *island,
                   phys_constraint_t *constraints,
                   phys_body_t *bodies_mut,
                   const phys_mat3_t *inv_inertia_world,
                   phys_velocity_t *velocities,
                   uint32_t body_count,
                   float dt)
{
    if (!sys || !constraints || !bodies_mut || !velocities ||
        sys->n == 0) {
        return;
    }

    /* The CG solver solved for incremental Δλ (lambda was zeroed
     * before solve by cg_assemble).  Accumulate Δλ into constraint
     * row lambdas for XPBD compliance feedback on subsequent
     * iterations/substeps. */
    for (uint32_t i = 0; i < sys->n; i++) {
        uint32_t c_idx = sys->row_constraint[i];
        uint8_t r_idx = sys->row_sub[i];
        constraints[c_idx].rows[r_idx].lambda += sys->lambda[i];
    }

    /* Apply Δv = M⁻¹ · Jᵀ · Δλ per body. */
    for (uint32_t b = 0; b < island->body_count; b++) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        if (bodies_mut[idx].inv_mass <= 0.0f) continue;

        phys_vec3_t dv_lin = {0.0f, 0.0f, 0.0f};
        phys_vec3_t dv_ang = {0.0f, 0.0f, 0.0f};
        float inv_mass = bodies_mut[idx].inv_mass;
        const phys_mat3_t *inv_I = inv_inertia_world
            ? &inv_inertia_world[idx] : NULL;
        if (!inv_I) continue;

        /* Sum contributions from all CG rows that reference this body. */
        for (uint32_t i = 0; i < sys->n; i++) {
            float dlam = sys->lambda[i];
            if (fabsf(dlam) < 1e-20f) continue;

            uint32_t c_idx = sys->row_constraint[i];
            const phys_constraint_t *c = &constraints[c_idx];
            const phys_jacobian_row_t *row = &c->rows[sys->row_sub[i]];

            if (c->body_a == idx) {
                dv_lin = vec3_add(dv_lin,
                    vec3_scale(row->J_va, inv_mass * dlam));
                phys_vec3_t ai = phys_mat3_mul_vec3(inv_I, row->J_wa);
                dv_ang = vec3_add(dv_ang, vec3_scale(ai, dlam));
            }
            if (c->body_b == idx) {
                dv_lin = vec3_add(dv_lin,
                    vec3_scale(row->J_vb, inv_mass * dlam));
                phys_vec3_t ai = phys_mat3_mul_vec3(inv_I, row->J_wb);
                dv_ang = vec3_add(dv_ang, vec3_scale(ai, dlam));
            }
        }

        /* Update velocities. */
        velocities[idx].linear = vec3_add(velocities[idx].linear, dv_lin);
        velocities[idx].angular = vec3_add(velocities[idx].angular, dv_ang);

        /* Coupled position update via exponential map. */
        bodies_mut[idx].position = vec3_add(
            bodies_mut[idx].position, vec3_scale(dv_lin, dt));
        bodies_mut[idx].orientation = expmap_integrate(
            bodies_mut[idx].orientation, dv_ang, dt);
    }
}

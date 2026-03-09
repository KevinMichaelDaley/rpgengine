/**
 * @file cg_apply.c
 * @brief Apply CG solution to velocities and body poses.
 *
 * The CG solver produces an incremental impulse vector Δλ.  Each row's
 * impulse is converted to a per-body 4×4 rigid transform (rotation
 * about the constraint's anchor point + translation).  Per-row
 * transforms are composed via matrix multiplication, then the final
 * composed transform is applied to the body's position and orientation.
 *
 * This "symplectic" 4×4 composition is essential because different
 * constraint rows rotate the body about different anchor points.
 * Summing angular velocities and applying one rotation loses the
 * anchor-point information and rotates about the body center instead.
 *
 * 1 non-static function: phys_cg_apply.
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
#include "ferrum/math/mat4.h"

#include <math.h>
#include <stddef.h>

/**
 * @brief Build a 4×4 rotation matrix from a rotation vector θ.
 *
 * Uses Rodrigues' formula: R = I + sin(θ)/θ · [θ×] + (1-cos(θ))/θ² · [θ×]²
 * Result is a column-major mat4_t with identity 4th row/column.
 */
static mat4_t rotvec_to_mat4(phys_vec3_t rv)
{
    float theta2 = rv.x * rv.x + rv.y * rv.y + rv.z * rv.z;
    mat4_t R = mat4_identity();

    if (theta2 < 1e-14f) return R;

    float theta = sqrtf(theta2);
    float s = sinf(theta) / theta;
    float c = (1.0f - cosf(theta)) / theta2;

    /* [θ×] matrix elements. */
    float x = rv.x, y = rv.y, z = rv.z;

    /* R = I + s·[θ×] + c·[θ×]²
     * [θ×] = [[0, -z, y], [z, 0, -x], [-y, x, 0]]
     * [θ×]² = [[-(y²+z²), xy, xz], [xy, -(x²+z²), yz], [xz, yz, -(x²+y²)]] */
    R.m[0]  = 1.0f + c * (-(y*y + z*z));
    R.m[1]  = s * z + c * (x * y);
    R.m[2]  = -s * y + c * (x * z);

    R.m[4]  = -s * z + c * (x * y);
    R.m[5]  = 1.0f + c * (-(x*x + z*z));
    R.m[6]  = s * x + c * (y * z);

    R.m[8]  = s * y + c * (x * z);
    R.m[9]  = -s * x + c * (y * z);
    R.m[10] = 1.0f + c * (-(x*x + y*y));

    return R;
}

/**
 * @brief Build a 4×4 rigid transform: rotation about a pivot point,
 *        plus additional translation.
 *
 * T = Translate(pivot + trans) · Rotate(rot_vec) · Translate(-pivot)
 *
 * This rotates a point about `pivot` by the angle/axis encoded in
 * `rot_vec`, then translates by `trans`.
 *
 * @param rot_vec  Rotation vector (axis × angle).
 * @param pivot    Point to rotate about (world space).
 * @param trans    Additional translation (e.g. from linear impulse).
 * @return         4×4 rigid transform (column-major).
 */
static mat4_t build_pivot_transform(phys_vec3_t rot_vec,
                                     phys_vec3_t pivot,
                                     phys_vec3_t trans)
{
    mat4_t R = rotvec_to_mat4(rot_vec);

    /* T(-pivot): negate pivot. */
    float npx = -pivot.x, npy = -pivot.y, npz = -pivot.z;

    /* R * T(-pivot): multiply R by translation (-pivot).
     * Column-major: translation column [12,13,14] becomes
     * R * (-pivot) = R's columns dotted with -pivot. */
    float tx = R.m[0]*npx + R.m[4]*npy + R.m[8]*npz;
    float ty = R.m[1]*npx + R.m[5]*npy + R.m[9]*npz;
    float tz = R.m[2]*npx + R.m[6]*npy + R.m[10]*npz;

    /* T(pivot + trans) * [R * T(-pivot)]:
     * Just add (pivot + trans) to the translation column. */
    R.m[12] = tx + pivot.x + trans.x;
    R.m[13] = ty + pivot.y + trans.y;
    R.m[14] = tz + pivot.z + trans.z;

    return R;
}

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

    /* Apply Δv = M⁻¹ · Jᵀ · Δλ per body, composing per-row 4×4
     * rigid transforms.
     *
     * Each constraint row's impulse produces a rigid displacement
     * of the body.  For angular rows, this is a rotation about the
     * joint's anchor point — NOT the body center.  For positional
     * rows, it's a translation + small rotation about the anchor.
     *
     * We compose these per-row 4×4 transforms via matrix
     * multiplication, then apply the final transform to the body
     * pose.  This preserves the anchor-point information that would
     * be lost by summing angular velocities into a single rotation
     * about the body center. */
    for (uint32_t b = 0; b < island->body_count; b++) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        if (bodies_mut[idx].inv_mass <= 0.0f) continue;

        float inv_mass = bodies_mut[idx].inv_mass;
        const phys_mat3_t *inv_I = inv_inertia_world
            ? &inv_inertia_world[idx] : NULL;
        if (!inv_I) continue;

        /* Accumulated 4×4 transform and velocity delta. */
        mat4_t T_accum = mat4_identity();
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

            /* Accumulate velocity deltas (additive). */
            dv_lin = vec3_add(dv_lin, row_dv_lin);
            dv_ang = vec3_add(dv_ang, row_dv_ang);

            /* Build per-row 4×4 transform.
             *
             * Determine the pivot (anchor) for this row's rotation.
             * For joint constraints, the anchor is the joint's world-
             * space socket position.  For contacts, use the body
             * center (no off-center rotation needed). */
            phys_vec3_t pivot = bodies_mut[idx].position;

            if (c->is_joint && constraint_joint_indices && joints) {
                uint32_t ji = constraint_joint_indices[c_idx];
                if (ji < joint_count) {
                    const phys_joint_t *j = &joints[ji];
                    phys_vec3_t local_anchor = (side == 0)
                        ? j->local_anchor_a : j->local_anchor_b;
                    pivot = vec3_add(
                        bodies_mut[idx].position,
                        quat_rotate_vec3(bodies_mut[idx].orientation,
                                         local_anchor));
                }
            }

            /* Rotation vector for this row. */
            phys_vec3_t row_rot = vec3_scale(row_dv_ang, dt);

            /* Translation from linear impulse. */
            phys_vec3_t row_trans = vec3_scale(row_dv_lin, dt);

            /* Build 4×4: rotation about pivot + translation. */
            mat4_t T_row = build_pivot_transform(row_rot, pivot,
                                                  row_trans);

            /* Compose: T_accum = T_row * T_accum.
             * Left-multiply so later rows act on top of earlier ones. */
            T_accum = mat4_mul(T_row, T_accum);
        }

        /* Update velocities (additive, as usual). */
        velocities[idx].linear = vec3_add(velocities[idx].linear, dv_lin);
        velocities[idx].angular = vec3_add(velocities[idx].angular, dv_ang);

        /* Apply the composed transform to the body pose.
         *
         * Compose T_accum directly with body->world_transform.
         * This is the authoritative rendering state — no lossy
         * mat4→quat→mat4 roundtrip.  The world_transform was
         * initialized from position+orientation before the first
         * CG iteration. */
        bodies_mut[idx].world_transform = mat4_mul(
            T_accum, bodies_mut[idx].world_transform);

        /* Extract position from the composed world transform. */
        bodies_mut[idx].position = (phys_vec3_t){
            bodies_mut[idx].world_transform.m[12],
            bodies_mut[idx].world_transform.m[13],
            bodies_mut[idx].world_transform.m[14]
        };

        /* Decompose orientation for the solver's next Jacobian rebuild.
         * This quat is only used internally by the constraint builder;
         * rendering reads world_transform directly, avoiding the
         * roundtrip error. */
        mat4_t rot_only = bodies_mut[idx].world_transform;
        rot_only.m[12] = 0.0f;
        rot_only.m[13] = 0.0f;
        rot_only.m[14] = 0.0f;
        bodies_mut[idx].orientation = quat_normalize_safe(
            quat_from_mat4(&rot_only), 1e-12f);
    }
}

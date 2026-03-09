/**
 * @file constraint_mass.c
 * @brief Compute effective mass for a Jacobian row.
 */

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"

float phys_compute_effective_mass(
    const phys_jacobian_row_t *row,
    float inv_mass_a, const phys_mat3_t *inv_inertia_a,
    float inv_mass_b, const phys_mat3_t *inv_inertia_b)
{
    if (!row || !inv_inertia_a || !inv_inertia_b) { return 0.0f; }

    /* W = J · M⁻¹ · Jᵀ
     *   = inv_mass_a · |J_va|² + J_wa · (I_a⁻¹ · J_wa)
     *   + inv_mass_b · |J_vb|² + J_wb · (I_b⁻¹ · J_wb)
     *
     * The linear terms must use |J_v|², not just inv_mass, because
     * angular rows with lever-arm cross products have J_va = -cross(r, axis)
     * whose magnitude depends on the lever arm length. */
    phys_vec3_t iIa_Jwa = phys_mat3_mul_vec3(inv_inertia_a, row->J_wa);
    phys_vec3_t iIb_Jwb = phys_mat3_mul_vec3(inv_inertia_b, row->J_wb);

    float denom = inv_mass_a * vec3_dot(row->J_va, row->J_va)
                + inv_mass_b * vec3_dot(row->J_vb, row->J_vb)
                + vec3_dot(row->J_wa, iIa_Jwa)
                + vec3_dot(row->J_wb, iIb_Jwb);

    if (denom <= 0.0f) { return 0.0f; }

    return 1.0f / denom;
}

/**
 * @file constraint_mass.c
 * @brief Compute effective mass for a Jacobian row.
 */

#include "ferrum/physics/constraint.h"
#include "ferrum/math/vec3.h"

float phys_compute_effective_mass(
    const phys_jacobian_row_t *row,
    float inv_mass_a, const phys_vec3_t *inv_inertia_a,
    float inv_mass_b, const phys_vec3_t *inv_inertia_b)
{
    if (!row || !inv_inertia_a || !inv_inertia_b) { return 0.0f; }

    /* Diagonal inverse inertia applied component-wise:
     * inv_I * J_w = { inv_I.x * J_w.x, inv_I.y * J_w.y, inv_I.z * J_w.z } */
    phys_vec3_t iIa_Jwa = {
        inv_inertia_a->x * row->J_wa.x,
        inv_inertia_a->y * row->J_wa.y,
        inv_inertia_a->z * row->J_wa.z
    };
    phys_vec3_t iIb_Jwb = {
        inv_inertia_b->x * row->J_wb.x,
        inv_inertia_b->y * row->J_wb.y,
        inv_inertia_b->z * row->J_wb.z
    };

    float denom = inv_mass_a + inv_mass_b
                + vec3_dot(row->J_wa, iIa_Jwa)
                + vec3_dot(row->J_wb, iIb_Jwb);

    if (denom <= 0.0f) { return 0.0f; }

    return 1.0f / denom;
}

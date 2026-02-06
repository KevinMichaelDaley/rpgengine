/**
 * @file constraint_tangent.c
 * @brief Compute orthonormal tangent basis from a contact normal.
 */

#include "ferrum/physics/constraint.h"
#include "ferrum/math/vec3.h"

#include <math.h>

void phys_compute_tangent_basis(
    phys_vec3_t normal,
    phys_vec3_t *tangent1,
    phys_vec3_t *tangent2)
{
    if (!tangent1 && !tangent2) { return; }

    /* Choose a reference axis most different from the normal.
     * If |normal.y| < 0.9, use (0,1,0); otherwise use (1,0,0). */
    phys_vec3_t ref;
    if (fabsf(normal.y) < 0.9f) {
        ref = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    } else {
        ref = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    }

    /* tangent1 = normalize(cross(normal, ref)) */
    phys_vec3_t t1 = vec3_cross(normal, ref);
    t1 = vec3_normalize_safe(t1, 1e-8f);

    /* tangent2 = cross(normal, tangent1) */
    phys_vec3_t t2 = vec3_cross(normal, t1);

    if (tangent1) { *tangent1 = t1; }
    if (tangent2) { *tangent2 = t2; }
}

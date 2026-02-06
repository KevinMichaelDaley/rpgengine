/**
 * @file narrowphase_sphere.c
 * @brief Sphere-sphere narrowphase intersection test.
 */

#include <math.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"

bool phys_sphere_vs_sphere(
    phys_vec3_t center_a, float radius_a,
    phys_vec3_t center_b, float radius_b,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) {
        return false;
    }

    /* Vector from A to B. */
    phys_vec3_t diff = vec3_sub(center_b, center_a);
    float dist_sq = vec3_dot(diff, diff);
    float r_sum = radius_a + radius_b;

    /* No contact if separated. */
    if (dist_sq > r_sum * r_sum) {
        return false;
    }

    float dist = sqrtf(dist_sq);

    if (dist < 1e-4f) {
        /* Coincident centers — use arbitrary up normal. */
        contact_out->normal = (phys_vec3_t){0.0f, 1.0f, 0.0f};
        contact_out->penetration = r_sum;
    } else {
        /* Normal from A toward B, penetration = overlap depth. */
        contact_out->normal = vec3_scale(diff, 1.0f / dist);
        contact_out->penetration = r_sum - dist;
    }

    /* Contact point: midpoint of overlap region. */
    contact_out->point_world = vec3_add(
        center_a,
        vec3_scale(contact_out->normal,
                   radius_a - contact_out->penetration * 0.5f));

    /* Spheres have no geometric features for tracking. */
    contact_out->feature_id = 0;

    return true;
}

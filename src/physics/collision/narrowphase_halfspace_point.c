/**
 * @file narrowphase_halfspace_point.c
 * @brief Point collider vs halfspace narrowphase test.
 *
 * A point collider has zero volume — it generates a single contact
 * when the body center penetrates the halfspace or falls within
 * the speculative margin.  This is the minimal collider type used
 * by ghost/connector bones in ragdolls that need ground contact
 * but have no authored collision shape.
 *
 * Non-static functions: 1 (phys_point_vs_halfspace)
 */

#include "ferrum/physics/collision/halfspace.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/math/vec3.h"

#include <stddef.h>
#include <string.h>

bool phys_point_vs_halfspace(
    phys_vec3_t point,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    phys_contact_point_t *contact_out)
{
    if (!contact_out) return false;

    /* Signed distance from point to plane surface. */
    float signed_dist = vec3_dot(plane_normal, point) - plane_distance;

    /* Penetration: positive when point is behind the plane. */
    float pen = -signed_dist;

    /* No contact if the point is farther than speculative margin. */
    if (pen < -speculative_margin) return false;

    /* Contact point is the point itself projected onto the plane. */
    phys_vec3_t contact_pt = vec3_sub(point,
                                       vec3_scale(plane_normal, signed_dist));

    /* Normal points from halfspace toward the point (= -plane_normal),
     * consistent with other halfspace tests. */
    contact_out->normal = vec3_scale(plane_normal, -1.0f);
    contact_out->penetration = pen;
    contact_out->point_world = contact_pt;
    /* Point collider: local_a is offset from body center to contact. */
    contact_out->local_a = vec3_sub(contact_pt, point);
    /* Halfspace: local_b is projection onto plane (body at origin). */
    contact_out->local_b = contact_pt;
    contact_out->feature_id = 0;

    return true;
}

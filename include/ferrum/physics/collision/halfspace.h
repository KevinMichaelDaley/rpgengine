#ifndef FERRUM_PHYSICS_COLLISION_HALFSPACE_H
#define FERRUM_PHYSICS_COLLISION_HALFSPACE_H

/**
 * @file halfspace.h
 * @brief Halfspace (infinite plane) collision tests.
 *
 * Provides sphere, capsule, box, and convex hull vs halfspace narrowphase
 * tests.
 * A halfspace is defined by a unit normal and signed distance from origin.
 * The plane equation is dot(normal, point) = distance.  Points with
 * dot(normal, point) < distance are behind the plane (penetrating).
 *
 * All functions are NULL-safe (return false / 0 if contact_out is NULL).
 */

#include <stdbool.h>

#include "ferrum/physics/phys_types.h"

struct phys_contact_point;
struct phys_convex_hull;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test sphere vs halfspace intersection.
 *
 * @param sphere_center  World-space center of the sphere.
 * @param sphere_radius  Radius of the sphere.
 * @param plane_normal   Unit normal of the halfspace (outward-facing).
 * @param plane_distance Signed distance of the plane from origin.
 * @param speculative_margin  Max separation for speculative contacts.
 * @param contact_out    Output contact point (non-NULL on true return).
 * @return true if sphere penetrates or is within speculative margin.
 *
 * Normal in contact_out points from halfspace toward sphere (= plane_normal).
 * Penetration is positive for overlap, negative for speculative.
 */
bool phys_sphere_vs_halfspace(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    struct phys_contact_point *contact_out);

/**
 * @brief Test capsule vs halfspace intersection.
 *
 * Tests both capsule endpoints against the plane and returns contacts
 * for each endpoint that is penetrating or within speculative margin.
 * When the capsule lies on its side, both endpoints produce contacts.
 *
 * @param capsule_center     World-space center of the capsule.
 * @param capsule_rotation   World-space orientation of the capsule.
 * @param capsule_radius     Radius of the capsule.
 * @param capsule_half_height Half the cylinder segment length.
 * @param plane_normal       Unit normal of the halfspace.
 * @param plane_distance     Signed distance of the plane from origin.
 * @param speculative_margin Max separation for speculative contacts.
 * @param contacts_out       Output array (must have capacity for 2 contacts).
 * @param max_contacts       Capacity of contacts_out (typically 2).
 * @return Number of contacts written (0 if no intersection).
 */
int phys_capsule_vs_halfspace(
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    struct phys_contact_point *contacts_out, int max_contacts);

/**
 * @brief Test box vs halfspace intersection.
 *
 * Tests all 8 box vertices against the plane and returns contacts
 * for vertices that are penetrating (up to 4 deepest).
 *
 * @param box_center      World-space center of the box.
 * @param box_rotation    World-space orientation of the box.
 * @param box_half_extents Half-extents along local axes.
 * @param plane_normal    Unit normal of the halfspace.
 * @param plane_distance  Signed distance of the plane from origin.
 * @param speculative_margin Max separation for speculative contacts.
 * @param contacts_out    Output array (must have capacity for 4 contacts).
 * @param max_contacts    Capacity of contacts_out (typically 4).
 * @return Number of contacts written (0 if no intersection).
 */
int phys_box_vs_halfspace(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    struct phys_contact_point *contacts_out, int max_contacts);

/**
 * @brief Test convex hull vs halfspace intersection.
 *
 * Transforms hull vertices to world space and tests each against the
 * plane.  Returns contacts for the deepest penetrating vertices
 * (up to max_contacts, typically 4).
 *
 * @param hull             Convex hull shape data.
 * @param hull_center      World-space center of the hull body.
 * @param hull_rotation    World-space orientation of the hull body.
 * @param plane_normal     Unit normal of the halfspace (outward).
 * @param plane_distance   Signed distance of the plane from origin.
 * @param speculative_margin Max separation for speculative contacts.
 * @param contacts_out     Output array (must have capacity for
 *                         max_contacts entries).
 * @param max_contacts     Capacity of contacts_out (typically 4).
 * @return Number of contacts written (0 if no intersection).
 */
int phys_convex_hull_vs_halfspace(
    const struct phys_convex_hull *hull,
    phys_vec3_t hull_center, phys_quat_t hull_rotation,
    phys_vec3_t plane_normal, float plane_distance,
    float speculative_margin,
    struct phys_contact_point *contacts_out, int max_contacts);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_COLLISION_HALFSPACE_H */

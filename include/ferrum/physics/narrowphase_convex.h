/**
 * @file narrowphase_convex.h
 * @brief Narrowphase collision tests involving convex hulls.
 *
 * Uses GJK/EPA internally.  Each function follows the existing
 * narrowphase pattern: returns true on contact, writes result
 * to contact_out.
 *
 * Public types (0):
 *   (uses phys_contact_point_t from manifold.h)
 */

#ifndef FERRUM_PHYSICS_NARROWPHASE_CONVEX_H
#define FERRUM_PHYSICS_NARROWPHASE_CONVEX_H

#include <stdbool.h>

#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sphere vs convex hull intersection.
 *
 * @param sphere_center  World-space center of the sphere.
 * @param sphere_radius  Radius of the sphere.
 * @param hull_center    World-space center (position) of the hull.
 * @param hull_rotation  World-space orientation of the hull.
 * @param hull           Convex hull shape (local space).
 * @param speculative_margin  Max separation for speculative contacts.
 * @param contact_out    Output contact point.
 * @return true if contact generated.
 *
 * Side effects: writes to *contact_out on true return.
 */
bool phys_sphere_vs_convex(
    phys_vec3_t sphere_center, float sphere_radius,
    phys_vec3_t hull_center, phys_quat_t hull_rotation,
    const phys_convex_hull_t *hull,
    float speculative_margin,
    struct phys_contact_point *contact_out);

/**
 * @brief Box vs convex hull intersection.
 *
 * @param box_center      World-space center of the box.
 * @param box_rotation    World-space orientation of the box.
 * @param box_half_extents  Half-extents of the box.
 * @param hull_center     World-space center of the hull.
 * @param hull_rotation   World-space orientation of the hull.
 * @param hull            Convex hull shape (local space).
 * @param speculative_margin  Max separation for speculative contacts.
 * @param contact_out     Output contact point.
 * @return true if contact generated.
 *
 * Side effects: writes to *contact_out on true return.
 */
bool phys_box_vs_convex(
    phys_vec3_t box_center, phys_quat_t box_rotation,
    phys_vec3_t box_half_extents,
    phys_vec3_t hull_center, phys_quat_t hull_rotation,
    const phys_convex_hull_t *hull,
    float speculative_margin,
    struct phys_contact_point *contact_out);

/**
 * @brief Capsule vs convex hull intersection.
 *
 * @param capsule_center    World-space center of the capsule.
 * @param capsule_rotation  World-space orientation of the capsule.
 * @param capsule_radius    Radius of the capsule.
 * @param capsule_half_height  Half the capsule cylinder length.
 * @param hull_center       World-space center of the hull.
 * @param hull_rotation     World-space orientation of the hull.
 * @param hull              Convex hull shape (local space).
 * @param speculative_margin  Max separation for speculative contacts.
 * @param contact_out       Output contact point.
 * @return true if contact generated.
 *
 * Side effects: writes to *contact_out on true return.
 */
bool phys_capsule_vs_convex(
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    phys_vec3_t hull_center, phys_quat_t hull_rotation,
    const phys_convex_hull_t *hull,
    float speculative_margin,
    struct phys_contact_point *contact_out);

/**
 * @brief Convex hull vs convex hull intersection.
 *
 * @param center_a     World-space center of hull A.
 * @param rotation_a   World-space orientation of hull A.
 * @param hull_a       Convex hull shape A (local space).
 * @param center_b     World-space center of hull B.
 * @param rotation_b   World-space orientation of hull B.
 * @param hull_b       Convex hull shape B (local space).
 * @param speculative_margin  Max separation for speculative contacts.
 * @param contact_out  Output contact point.
 * @return true if contact generated.
 *
 * Side effects: writes to *contact_out on true return.
 */
bool phys_convex_vs_convex(
    phys_vec3_t center_a, phys_quat_t rotation_a,
    const phys_convex_hull_t *hull_a,
    phys_vec3_t center_b, phys_quat_t rotation_b,
    const phys_convex_hull_t *hull_b,
    float speculative_margin,
    struct phys_contact_point *contact_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_NARROWPHASE_CONVEX_H */

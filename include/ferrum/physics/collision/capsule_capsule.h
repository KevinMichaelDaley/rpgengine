#ifndef FERRUM_PHYSICS_COLLISION_CAPSULE_CAPSULE_H
#define FERRUM_PHYSICS_COLLISION_CAPSULE_CAPSULE_H

/** @file
 * @brief Capsule vs Capsule narrowphase collision detection.
 *
 * Performs closest-point-on-segment computation between the two
 * capsule axes, then reduces to a sphere-sphere test at the
 * closest points.
 */

#include <stdbool.h>

#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test capsule vs capsule intersection.
 *
 * Each capsule is defined by its world-space center, orientation
 * quaternion, radius, and half-height (cylinder segment along +Y).
 *
 * @param center_a      World-space center of capsule A.
 * @param rotation_a    World-space orientation of capsule A.
 * @param radius_a      Radius of capsule A.
 * @param half_height_a Half the cylinder height of capsule A.
 * @param center_b      World-space center of capsule B.
 * @param rotation_b    World-space orientation of capsule B.
 * @param radius_b      Radius of capsule B.
 * @param half_height_b Half the cylinder height of capsule B.
 * @param speculative_margin Max separation for speculative contacts (0 = disabled).
 * @param contact_out   Output contact point (must be non-NULL for true return).
 * @return true if capsules overlap/touch or are within speculative margin.
 *
 * Normal points from A to B.  Penetration is positive for overlap,
 * negative for speculative contacts.
 * If closest points coincide, normal defaults to (0,1,0).
 * Returns false if contact_out is NULL.
 */
bool phys_capsule_vs_capsule(
    phys_vec3_t center_a, phys_quat_t rotation_a,
    float radius_a, float half_height_a,
    phys_vec3_t center_b, phys_quat_t rotation_b,
    float radius_b, float half_height_b,
    float speculative_margin,
    struct phys_contact_point *contact_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_COLLISION_CAPSULE_CAPSULE_H */

#ifndef FERRUM_PHYSICS_COLLISION_BOX_BOX_H
#define FERRUM_PHYSICS_COLLISION_BOX_BOX_H

/** @file
 * @brief Box vs Box narrowphase using Separating Axis Theorem.
 */

#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Box vs Box narrowphase using Separating Axis Theorem.
 *
 * Tests two oriented bounding boxes (OBBs) for intersection and
 * generates contact points for the collision solver.
 *
 * @param center_a       World-space center of box A.
 * @param rotation_a     World-space orientation of box A.
 * @param half_extents_a Half-extents of box A in local space.
 * @param center_b       World-space center of box B.
 * @param rotation_b     World-space orientation of box B.
 * @param half_extents_b Half-extents of box B in local space.
 * @param contact_out    Output array for contact points. Must have room
 *                       for max_contacts entries. May be NULL if
 *                       max_contacts is 0.
 * @param max_contacts   Maximum number of contacts to generate.
 * @param speculative_margin Max separation for speculative contacts (0 = disabled).
 * @return Number of contacts found (0 if separated beyond margin or invalid input).
 *
 * Normal in each contact points from A toward B.
 * Penetration is positive for overlap, negative for speculative contacts.
 * Returns 0 if contact_out is NULL and max_contacts > 0.
 */
int phys_box_vs_box(
    phys_vec3_t center_a, phys_quat_t rotation_a, phys_vec3_t half_extents_a,
    phys_vec3_t center_b, phys_quat_t rotation_b, phys_vec3_t half_extents_b,
    struct phys_contact_point *contact_out, int max_contacts,
    float speculative_margin);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_COLLISION_BOX_BOX_H */

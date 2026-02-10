#ifndef FERRUM_PHYSICS_COLLISION_BOX_CAPSULE_H
#define FERRUM_PHYSICS_COLLISION_BOX_CAPSULE_H

#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/**
 * Box vs Capsule narrowphase collision.
 * Returns true if contact found.
 * The capsule is treated as a swept sphere along its axis.
 *
 * @param box_center         World-space center of the box.
 * @param box_rotation       World-space orientation of the box.
 * @param box_half_extents   Half-extents of the box along its local axes.
 * @param capsule_center     World-space center of the capsule.
 * @param capsule_rotation   World-space orientation of the capsule.
 * @param capsule_radius     Radius of the capsule.
 * @param capsule_half_height Half the length of the capsule's cylindrical segment.
 * @param speculative_margin Max separation for speculative contacts (0 = disabled).
 * @param contact_out        Output contact point. If NULL, returns false.
 * @return true if the shapes overlap or are within speculative margin.
 *
 * Normal points from box (A) to capsule (B).
 * Penetration is positive for overlap, negative for speculative contacts.
 */
bool phys_box_vs_capsule(
    phys_vec3_t box_center, phys_quat_t box_rotation, phys_vec3_t box_half_extents,
    phys_vec3_t capsule_center, phys_quat_t capsule_rotation,
    float capsule_radius, float capsule_half_height,
    float speculative_margin,
    struct phys_contact_point *contact_out);

#endif /* FERRUM_PHYSICS_COLLISION_BOX_CAPSULE_H */

#ifndef FERRUM_PHYSICS_BODY_H
#define FERRUM_PHYSICS_BODY_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"

/** @file
 * @brief Rigid body core data structure and inertia helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Bitflags for phys_body_t::flags. */
#define PHYS_BODY_FLAG_STATIC (1u << 0)
#define PHYS_BODY_FLAG_KINEMATIC (1u << 1)
#define PHYS_BODY_FLAG_SLEEPING (1u << 2)

/**
 * @brief Core rigid body state.
 *
 * Layout is intentionally stable and validated by size assertions.
 */
typedef struct phys_body {
    phys_vec3_t position;
    phys_quat_t orientation;
    phys_vec3_t linear_vel;
    phys_vec3_t angular_vel;

    float inv_mass;
    phys_vec3_t inv_inertia_diag;

    uint32_t flags;
    uint8_t tier;
    uint8_t _pad[7];
} phys_body_t;

_Static_assert(sizeof(phys_body_t) == 80, "phys_body_t must be exactly 80 bytes");

/** Initialize body to safe defaults (static, identity orientation). */
void phys_body_init(phys_body_t *body);

/** Set mass; mass <= 0 makes the body static (inv_mass = 0). */
void phys_body_set_mass(phys_body_t *body, float mass);

/** Compute inverse inertia diagonal for a box centered at the origin. */
void phys_body_set_box_inertia(phys_body_t *body, float mass, phys_vec3_t half_extents);

/** Compute inverse inertia diagonal for a solid sphere centered at the origin. */
void phys_body_set_sphere_inertia(phys_body_t *body, float mass, float radius);

/**
 * Compute inverse inertia diagonal for a capsule aligned along +Y.
 *
 * @param half_height Half of the cylinder length (excluding hemispheres).
 */
void phys_body_set_capsule_inertia(phys_body_t *body, float mass, float radius, float half_height);

bool phys_body_is_static(const phys_body_t *body);
bool phys_body_is_kinematic(const phys_body_t *body);
bool phys_body_is_sleeping(const phys_body_t *body);
void phys_body_set_sleeping(phys_body_t *body, bool sleeping);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_BODY_H */

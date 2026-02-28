/**
 * @file phys_overlap.h
 * @brief Boolean overlap test between two colliders.
 *
 * Tests whether two colliders intersect in world space using the full
 * narrowphase dispatch (sphere, box, capsule, convex, mesh, halfspace).
 * Used by the editor for select_touching queries where broadphase data
 * may be stale.
 *
 * Public types: 1 (phys_overlap_ctx_t).
 * Public functions: 1 (phys_test_overlap).
 */

#ifndef FERRUM_PHYSICS_OVERLAP_H
#define FERRUM_PHYSICS_OVERLAP_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/collider.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/convex_compound.h"
#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context for overlap testing — holds shape pool pointers.
 *
 * The caller populates this once from the physics world and reuses
 * it for multiple overlap queries.
 *
 * Ownership: borrows all pointers; does not free anything.
 */
typedef struct phys_overlap_ctx {
    const phys_sphere_t     *spheres;      /**< Sphere shape pool. */
    const phys_box_t        *boxes;        /**< Box shape pool. */
    const phys_capsule_t    *capsules;     /**< Capsule shape pool. */
    const phys_mesh_shape_t *meshes;       /**< Mesh shape pool. */
    const phys_convex_hull_t *convex_hulls; /**< Convex hull shape pool. */
    const phys_halfspace_t  *halfspaces;   /**< Halfspace shape pool. */
    const phys_convex_compound_t *compounds; /**< Compound shape pool. */
} phys_overlap_ctx_t;

/**
 * @brief Test whether two colliders overlap in world space.
 *
 * Dispatches to the appropriate narrowphase function based on the
 * shape types of collider A and B. Returns true if ANY contact is
 * generated (penetration >= 0).
 *
 * Handles all supported shape pairs:
 *   sphere×sphere, sphere×box, sphere×capsule, sphere×convex, sphere×mesh
 *   box×box, box×capsule, box×convex, box×mesh
 *   capsule×capsule, capsule×convex, capsule×mesh
 *   convex×convex
 *   halfspace×(sphere, box, capsule) — via flipped sphere/box/capsule tests
 *
 * @param ctx      Shape pool context (non-NULL).
 * @param col_a    Collider A (non-NULL).
 * @param pos_a    World-space position of body A.
 * @param rot_a    World-space orientation of body A.
 * @param col_b    Collider B (non-NULL).
 * @param pos_b    World-space position of body B.
 * @param rot_b    World-space orientation of body B.
 * @return true if shapes overlap.
 *
 * Side effects: none (pure query).
 * Error handling: returns false on NULL inputs or unsupported pairs.
 */
bool phys_test_overlap(const phys_overlap_ctx_t *ctx,
                       const phys_collider_t *col_a,
                       phys_vec3_t pos_a, phys_quat_t rot_a,
                       const phys_collider_t *col_b,
                       phys_vec3_t pos_b, phys_quat_t rot_b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_OVERLAP_H */

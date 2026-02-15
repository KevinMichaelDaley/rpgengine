#ifndef FERRUM_PHYSICS_AABB_UPDATE_H
#define FERRUM_PHYSICS_AABB_UPDATE_H

/** @file
 * @brief Stage 4: AABB Update for active bodies.
 *
 * Computes world-space AABBs for bodies in active tiers (T0-T4) only,
 * skipping T5 (sleeping). Driven by tier lists rather than iterating
 * all bodies.
 */

#include <stdint.h>

struct phys_body;
struct phys_collider;
struct phys_sphere;
struct phys_box;
struct phys_capsule;
struct phys_mesh_shape;
struct phys_halfspace;
struct phys_aabb;
struct phys_tier_lists;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arguments for the AABB update stage.
 *
 * Ownership: the caller owns all arrays. The stage reads from
 * bodies, colliders, shape pools, and tier_lists, and writes to
 * aabbs_out.
 *
 * Nullability: bodies, colliders, aabbs_out, and tier_lists must
 * be non-NULL for meaningful work. Shape pool pointers (spheres,
 * boxes, capsules) may be NULL if no collider references that type.
 */
typedef struct phys_aabb_update_args {
    const struct phys_body *bodies;
    const struct phys_collider *colliders;
    const struct phys_sphere *spheres;
    const struct phys_box *boxes;
    const struct phys_capsule *capsules;
    const struct phys_mesh_shape *meshes;
    const struct phys_halfspace *halfspaces;
    struct phys_aabb *aabbs_out;
    const struct phys_tier_lists *tier_lists;
} phys_aabb_update_args_t;

/**
 * @brief Execute the AABB update stage for active bodies.
 *
 * Iterates tiers T0 through T4 (skipping T5/sleeping). For each
 * body index in each active tier, computes the world-space AABB
 * from the collider's shape and stores it in aabbs_out[body_idx].
 *
 * @param args  Stage arguments (if NULL, no-op).
 *
 * Side effects: writes to args->aabbs_out for active bodies only.
 */
void phys_stage_aabb_update(const phys_aabb_update_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_AABB_UPDATE_H */

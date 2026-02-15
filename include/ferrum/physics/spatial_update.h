#ifndef FERRUM_PHYSICS_SPATIAL_UPDATE_H
#define FERRUM_PHYSICS_SPATIAL_UPDATE_H

/** @file
 * @brief Stage 2: Spatial Index Update.
 *
 * Computes world-space AABBs for all active bodies based on their
 * collider type, and populates the spatial hash grid for broadphase
 * collision detection.
 *
 * The stage clears the grid, iterates bodies, computes each AABB
 * from the collider's shape (sphere, box, or capsule), and inserts
 * the body index into the grid.
 *
 * Inactive bodies (active[i] == 0) are skipped when an active array
 * is provided.  If active is NULL, all bodies are processed.
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
struct phys_spatial_grid;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arguments for the spatial update stage.
 *
 * Ownership: the caller owns all arrays.  The stage reads from
 * bodies, colliders, and shape pools, and writes to aabbs_out
 * and grid_out.
 *
 * Nullability: bodies, colliders, aabbs_out, and grid_out must
 * be non-NULL (unless body_count is 0).  Shape pool pointers
 * (spheres, boxes, capsules) may be NULL if no collider references
 * that shape type.  active may be NULL (all bodies processed).
 */
typedef struct phys_spatial_update_args {
    const struct phys_body *bodies;
    const struct phys_collider *colliders;
    const struct phys_sphere *spheres;
    const struct phys_box *boxes;
    const struct phys_capsule *capsules;
    const struct phys_mesh_shape *meshes;
    const struct phys_halfspace *halfspaces;
    struct phys_aabb *aabbs_out;
    struct phys_spatial_grid *grid_out;
    const uint8_t *active;
    uint32_t body_count;

    /** When non-zero, static bodies (inv_mass==0 and not kinematic) are NOT
     * inserted into the spatial grid. AABBs are still computed.
     *
     * Default (0): preserve legacy behavior (static bodies are inserted).
     */
    uint8_t exclude_static_from_grid;
} phys_spatial_update_args_t;

/**
 * @brief Execute the spatial index update stage.
 *
 * Clears grid_out, then for each active body computes the world-space
 * AABB from the collider shape and inserts it into the grid.
 *
 * @param args  Stage arguments (if NULL, no-op).
 *
 * Side effects: writes to args->aabbs_out and args->grid_out.
 */
void phys_stage_spatial_update(const phys_spatial_update_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_SPATIAL_UPDATE_H */

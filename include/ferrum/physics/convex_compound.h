#ifndef FERRUM_PHYSICS_CONVEX_COMPOUND_H
#define FERRUM_PHYSICS_CONVEX_COMPOUND_H

/** @file
 * @brief Compound convex collider for decomposed meshes.
 *
 * A convex compound is a collection of convex hulls that together
 * approximate a concave mesh.  Each hull is referenced by index
 * into the world's convex_hulls[] pool.
 *
 * Created by phys_decompose_mesh() and registered in the world's
 * compounds[] pool.  The narrowphase iterates each child hull and
 * tests it against the opposing primitive.
 *
 * Ownership: does NOT own the hull data; hulls live in the world
 * pool.  The compound stores only indices.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum convex hulls per compound shape. */
#define PHYS_COMPOUND_MAX_CHILDREN 64u

/**
 * @brief Compound convex collider: array of convex hull indices.
 *
 * Each child_hull_index[i] is an index into the world's convex_hulls[]
 * pool.  The compound's AABB is the union of all child AABBs.
 */
typedef struct phys_convex_compound {
    uint32_t child_hull_indices[PHYS_COMPOUND_MAX_CHILDREN];
    uint32_t child_count;   /**< Number of child hulls (0..64). */
} phys_convex_compound_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CONVEX_COMPOUND_H */

/**
 * @file convex_hull.h
 * @brief Convex hull shape for physics collision.
 *
 * A convex hull is a convex polyhedron defined by a set of vertices
 * and faces.  It supports GJK/EPA collision queries via a support
 * function that returns the vertex furthest along a given direction.
 *
 * Hard limits: ≤64 vertices, ≤64 faces per hull.
 *
 * Public types (2):
 *   1. phys_convex_hull_t     — convex hull shape data
 *   2. phys_convex_face_t     — face descriptor (index range + normal)
 */

#ifndef FERRUM_PHYSICS_CONVEX_HULL_H
#define FERRUM_PHYSICS_CONVEX_HULL_H

#include <stdint.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum vertices per convex hull. */
#define PHYS_CONVEX_MAX_VERTS  64u

/** Maximum faces per convex hull. */
#define PHYS_CONVEX_MAX_FACES  64u

/** Maximum total face indices (sum of all face vertex counts). */
#define PHYS_CONVEX_MAX_INDICES 384u

/**
 * @brief Face descriptor for a convex hull.
 *
 * Each face is a convex polygon defined by a contiguous range of
 * vertex indices in the hull's index array.  Vertices are wound
 * counter-clockwise when viewed from outside.
 */
typedef struct phys_convex_face {
    uint16_t index_start;   /**< Start offset into hull's index array. */
    uint16_t index_count;   /**< Number of vertices in this face. */
    phys_vec3_t normal;     /**< Outward-facing unit normal. */
} phys_convex_face_t;

/**
 * @brief Convex hull shape data.
 *
 * Stored inline (no pointers to external memory).  All arrays are
 * fixed-size to avoid dynamic allocation.
 *
 * Ownership: self-contained value type.  Copy-safe.
 * Nullability: init functions produce valid hulls; zero-initialized
 *   struct has vertex_count=0 and is treated as empty.
 * Error semantics: build functions return 0 on success, -1 on error.
 */
typedef struct phys_convex_hull {
    phys_vec3_t vertices[PHYS_CONVEX_MAX_VERTS];
    uint32_t vertex_count;

    phys_convex_face_t faces[PHYS_CONVEX_MAX_FACES];
    uint32_t face_count;

    uint16_t indices[PHYS_CONVEX_MAX_INDICES];
    uint32_t index_count;

    phys_vec3_t centroid;   /**< Precomputed centroid (mean of vertices). */
    phys_aabb_t aabb;       /**< Local-space AABB. */
} phys_convex_hull_t;

/* ── Public API (4 functions) ──────────────────────────────────── */

/**
 * @brief Find the support point (vertex furthest along a direction).
 *
 * Returns the vertex with the maximum dot product with `dir`.
 * Used by GJK/EPA algorithms.
 *
 * @param hull  Convex hull (non-NULL, vertex_count > 0).
 * @param dir   Search direction (need not be normalized).
 * @return Support point in hull-local coordinates.
 *
 * Side effects: none.
 */
phys_vec3_t phys_convex_hull_support(const phys_convex_hull_t *hull,
                                     phys_vec3_t dir);

/**
 * @brief Build a convex hull from a point cloud.
 *
 * Computes the convex hull of up to PHYS_CONVEX_MAX_VERTS input points.
 * Populates vertices, faces, indices, centroid, and AABB.
 *
 * Uses an incremental convex hull algorithm.
 *
 * @param hull       Output hull (non-NULL, zeroed by caller).
 * @param points     Input point cloud (non-NULL if count > 0).
 * @param count      Number of input points.
 * @return 0 on success, -1 on error (NULL args, count > max, degenerate).
 *
 * Ownership: hull is fully self-contained after build.
 * Side effects: writes to *hull.
 */
int phys_convex_hull_build(phys_convex_hull_t *hull,
                           const phys_vec3_t *points,
                           uint32_t count);

/**
 * @brief Compute the AABB of a convex hull in world space.
 *
 * Transforms each vertex by the given position and rotation,
 * then computes the bounding AABB.
 *
 * @param hull      Convex hull (non-NULL).
 * @param position  World-space translation.
 * @param rotation  World-space rotation.
 * @return World-space AABB.
 *
 * Side effects: none.
 */
phys_aabb_t phys_convex_hull_world_aabb(const phys_convex_hull_t *hull,
                                        phys_vec3_t position,
                                        phys_quat_t rotation);

/**
 * @brief Recompute centroid and local AABB from current vertices.
 *
 * Call after modifying vertices directly (e.g. decimation).
 *
 * @param hull  Convex hull (non-NULL).
 *
 * Side effects: writes hull->centroid, hull->aabb.
 */
void phys_convex_hull_recompute_bounds(phys_convex_hull_t *hull);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CONVEX_HULL_H */

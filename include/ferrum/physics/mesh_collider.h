/**
 * @file mesh_collider.h
 * @brief Triangle mesh collider with SAH-based BVH.
 *
 * Provides a BVH (Bounding Volume Hierarchy) over triangles for static mesh
 * collision.  Used for terrain, architecture, and static props that cannot be
 * approximated by primitive shapes (sphere, box, capsule).
 *
 * ## Ownership
 * The BVH does not own its memory.  The builder allocates the node array
 * (and temporary scratch) from a caller-provided arena.
 *
 * ## Nullability
 * All public functions are NULL-safe (no-op or return 0).
 *
 * ## Thread safety
 * The built BVH is read-only and may be queried concurrently from any thread.
 * Building must be done single-threaded (or externally synchronized).
 */

#ifndef FERRUM_PHYSICS_MESH_COLLIDER_H
#define FERRUM_PHYSICS_MESH_COLLIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/phys_vec3.h"

struct phys_frame_arena;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Triangle ───────────────────────────────────────────────────── */

/**
 * @brief A triangle defined by three vertices.
 *
 * Winding order is CCW when viewed from the front face (determines
 * the outward normal via cross(v1-v0, v2-v0)).
 */
typedef struct phys_triangle {
    phys_vec3_t v[3]; /**< Triangle vertices. */
} phys_triangle_t;

/**
 * @brief Compute the axis-aligned bounding box of a triangle.
 *
 * @param tri  Triangle (must not be NULL).
 * @return AABB enclosing the triangle.
 */
static inline phys_aabb_t phys_triangle_aabb(const phys_triangle_t *tri) {
    phys_aabb_t aabb;
    aabb.min = tri->v[0];
    aabb.max = tri->v[0];
    for (int i = 1; i < 3; i++) {
        if (tri->v[i].x < aabb.min.x) aabb.min.x = tri->v[i].x;
        if (tri->v[i].y < aabb.min.y) aabb.min.y = tri->v[i].y;
        if (tri->v[i].z < aabb.min.z) aabb.min.z = tri->v[i].z;
        if (tri->v[i].x > aabb.max.x) aabb.max.x = tri->v[i].x;
        if (tri->v[i].y > aabb.max.y) aabb.max.y = tri->v[i].y;
        if (tri->v[i].z > aabb.max.z) aabb.max.z = tri->v[i].z;
    }
    return aabb;
}

/* ── Mesh BVH Node ──────────────────────────────────────────────── */

/**
 * @brief A mesh BVH node.
 *
 * Leaf nodes have left == right == UINT32_MAX and store a tri_index.
 * Internal nodes store child indices and have tri_index == UINT32_MAX.
 */
typedef struct phys_mesh_bvh_node {
    phys_aabb_t bounds;   /**< Bounding box of this node. */
    uint32_t    left;     /**< Left child index, or UINT32_MAX if leaf. */
    uint32_t    right;    /**< Right child index, or UINT32_MAX if leaf. */
    uint32_t    tri_index;/**< Triangle index for leaves, UINT32_MAX for internal. */
} phys_mesh_bvh_node_t;

/* ── Mesh BVH Container ────────────────────────────────────────── */

/**
 * @brief Mesh BVH container.
 *
 * The node array is laid out as a binary tree addressed by indices.
 * The triangles array is a borrowed pointer to the original input.
 */
typedef struct phys_mesh_bvh {
    phys_mesh_bvh_node_t *nodes;      /**< Arena-allocated node array. */
    uint32_t              node_count;  /**< Number of valid nodes. */
    uint32_t              root;        /**< Root node index, or UINT32_MAX if empty. */
    const phys_triangle_t *triangles;  /**< Borrowed pointer to triangle array. */
    uint32_t              tri_count;   /**< Number of triangles. */
} phys_mesh_bvh_t;

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Return true if @p node is a leaf.
 */
static inline bool phys_mesh_bvh_node_is_leaf(const phys_mesh_bvh_node_t *node) {
    return node && node->left == UINT32_MAX;
}

/* ── Build API ──────────────────────────────────────────────────── */

/**
 * @brief Build a mesh BVH from a triangle array.
 *
 * Uses top-down SAH (Surface Area Heuristic) with 12-bin partitioning,
 * matching the static_bvh builder's approach.
 *
 * @param out_bvh    Output BVH (NULL-safe no-op).
 * @param triangles  Input triangle array (may be NULL if tri_count==0).
 * @param tri_count  Number of triangles.
 * @param arena      Arena for node + scratch allocations (must be non-NULL
 *                   when tri_count > 0).
 *
 * @note Side effects: allocates from @p arena.
 * @note The BVH borrows @p triangles — caller must keep the array alive.
 */
void phys_mesh_bvh_build(phys_mesh_bvh_t *out_bvh,
                          const phys_triangle_t *triangles,
                          uint32_t tri_count,
                          struct phys_frame_arena *arena);

/* ── Query API ──────────────────────────────────────────────────── */

/**
 * @brief Query the mesh BVH for triangles whose AABBs overlap @p query_aabb.
 *
 * Returns triangle indices (not node indices).
 *
 * @param bvh          Mesh BVH to query (NULL-safe; returns 0).
 * @param query_aabb   Query AABB (NULL-safe; returns 0).
 * @param out_tri_ids  Output array of triangle indices.
 * @param max_results  Capacity of @p out_tri_ids.
 *
 * @return Number of triangle indices written.
 */
uint32_t phys_mesh_bvh_query_aabb(const phys_mesh_bvh_t *bvh,
                                   const phys_aabb_t *query_aabb,
                                   uint32_t *out_tri_ids,
                                   uint32_t max_results);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_MESH_COLLIDER_H */

/**
 * @file snap_depenetrate.h
 * @brief Depenetration for surface snap.
 *
 * Given a mesh placed on a surface (via face snap), computes how far
 * to push it so no vertex penetrates below any environment surface.
 *
 * Two approaches:
 * - snap_depenetrate_plane: project vertices against a single plane.
 * - snap_depenetrate_vs_tris: check vertices against world-space
 *   triangles (handles edges, corners, multi-face geometry).
 *
 * ## Ownership
 * All functions borrow their inputs.
 *
 * ## Nullability
 * All pointer params must be non-NULL; returns false/0 on NULL.
 *
 * ## Public types: snap_depenetrate_result_t (1-type rule)
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SNAP_DEPENETRATE_H
#define FERRUM_EDITOR_VIEWPORT_SNAP_DEPENETRATE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

struct snap_mesh;
struct mat4;

/**
 * @brief Result of a depenetration query.
 *
 * The push vector resolves the overlap: add it to the entity's
 * position so that no vertex lies below the surface.
 */
typedef struct snap_depenetrate_result {
    vec3_t push;         /**< Push vector to resolve overlap. */
    float  penetration;  /**< Depth of deepest penetration (positive = overlap). */
} snap_depenetrate_result_t;

/**
 * @brief Compute how far a mesh penetrates below a single plane.
 *
 * For each vertex of mesh_b (world space), projects onto the surface
 * normal relative to the surface point. Returns depth of deepest
 * vertex below the plane.
 *
 * @param mesh_b         Mesh being placed. Must not be NULL.
 * @param model_b        World transform for mesh B. Must not be NULL.
 * @param surface_point  Point on the surface.
 * @param surface_normal Surface normal (unit vector, outward).
 * @param result         Output. Must not be NULL.
 * @return true if any vertex penetrates below the surface.
 */
bool snap_depenetrate_plane(const struct snap_mesh *mesh_b,
                             const struct mat4 *model_b,
                             vec3_t surface_point,
                             vec3_t surface_normal,
                             snap_depenetrate_result_t *result);

/**
 * @brief Combined depenetration: vertex-vs-face + triangle SAT.
 *
 * Three passes to catch all overlap types:
 * 1. Entity vertices vs env faces — containment (e.g., capsule cap
 *    vertices below a box face).
 * 2. Env vertices vs entity faces — reverse containment (e.g., box
 *    corner inside capsule hemisphere).
 * 3. Triangle-vs-triangle SAT (11 axes) — edge-face and edge-edge
 *    crossings (e.g., box edge through capsule cylinder face).
 *
 * Returns the deepest penetration from any pass.
 *
 * @param mesh_b       Mesh being placed. Must not be NULL. Must have indices.
 * @param model_b      World transform for mesh B. Must not be NULL.
 * @param env_tris     World-space environment triangles. Must not be NULL.
 * @param env_tri_count Number of triangles.
 * @param result       Output. Must not be NULL.
 * @return true if any penetration found (> 1e-5 threshold).
 */
bool snap_depenetrate_vs_tris(const struct snap_mesh *mesh_b,
                               const struct mat4 *model_b,
                               const vec3_t *env_tris,
                               uint32_t env_tri_count,
                               snap_depenetrate_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SNAP_DEPENETRATE_H */

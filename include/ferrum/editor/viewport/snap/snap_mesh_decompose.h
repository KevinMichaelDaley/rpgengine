/**
 * @file snap_mesh_decompose.h
 * @brief Convex decomposition for snap mesh retention.
 *
 * For high-poly meshes (above SNAP_DECOMPOSE_THRESHOLD triangles),
 * runs V-ACD convex decomposition and inserts the simplified convex
 * hull compound into the snap cache instead of the raw mesh.
 *
 * This prevents the editor from crashing when surface-snapping
 * against meshes with tens of thousands of triangles, since the
 * snap raycast and depenetration iterate all triangles linearly.
 *
 * Public types: 0 (forward declarations only).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SNAP_MESH_DECOMPOSE_H
#define FERRUM_EDITOR_VIEWPORT_SNAP_MESH_DECOMPOSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct snap_mesh_cache;
struct snap_decompose_cache;
struct mesh_slot;

/**
 * @brief Triangle count threshold above which meshes are decomposed
 *        into convex hulls for snapping instead of using raw geometry.
 *
 * Matches the SNAP_MAX_ENV_TRIS cap in scene_input.c — any mesh
 * above this count would overflow the environment triangle buffer.
 */
#define SNAP_DECOMPOSE_THRESHOLD 2048u

/**
 * @brief Check whether a mesh slot should be convex-decomposed for snapping.
 *
 * @param slot  Mesh slot to check (NULL returns false).
 * @return true if the mesh has more than SNAP_DECOMPOSE_THRESHOLD triangles.
 */
bool snap_mesh_should_decompose(const struct mesh_slot *slot);

/**
 * @brief Decompose a mesh slot into convex hulls and insert into snap cache.
 *
 * Runs phys_decompose_mesh() (V-ACD) on the mesh, then triangulates the
 * resulting convex hulls via snap_mesh_retain_compound(). The snap cache
 * entry is a low-poly approximation suitable for raycasting and depenetration.
 *
 * @param cache      Snap mesh cache (non-NULL).
 * @param entity_id  Entity ID (must be < cache capacity).
 * @param slot       Mesh slot with CPU-side geometry (non-NULL, non-empty).
 * @param dcache     Optional decompose cache to store result (may be NULL).
 * @return true on success, false on NULL args, empty mesh, or decompose failure.
 *
 * Ownership: allocates temporary memory via malloc (freed before return).
 * Side effects: overwrites any existing snap mesh for entity_id.
 */
bool snap_mesh_retain_decomposed(struct snap_mesh_cache *cache,
                                  uint32_t entity_id,
                                  const struct mesh_slot *slot,
                                  struct snap_decompose_cache *dcache);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SNAP_MESH_DECOMPOSE_H */

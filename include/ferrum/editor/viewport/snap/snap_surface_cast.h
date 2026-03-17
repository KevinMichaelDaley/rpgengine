/**
 * @file snap_surface_cast.h
 * @brief Raycast against all scene entities for surface snap targeting.
 *
 * Iterates all active, non-hidden entities and raycasts against their
 * mesh geometry (from snap_mesh_cache) to find the nearest face hit.
 * Supports MESH entities via cached snap meshes and BOX entities via
 * generated box geometry.
 *
 * Ownership: may lazily insert primitive snap meshes into cache.
 * Nullability: entities and cache must be non-NULL (or count 0).
 * Error semantics: hit.valid = false on miss.
 * Side effects: may allocate snap meshes for primitive entities.
 *
 * Public types: none (uses snap_hit_t from snap_raycast.h).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SNAP_SURFACE_CAST_H
#define FERRUM_EDITOR_VIEWPORT_SNAP_SURFACE_CAST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/math/vec3.h"
#include "ferrum/editor/viewport/snap/snap_raycast.h"

/* Forward declarations. */
struct edit_entity;
struct snap_mesh_cache;

/* ---- Surface cast (snap_surface_cast.c) ---- */

/**
 * @brief Raycast against all entities, find nearest face hit.
 *
 * Iterates entities [0, entity_count). Skips inactive, hidden, and
 * the excluded entity. For each remaining entity, computes its model
 * matrix, retrieves snap mesh, and tests ray-mesh intersection.
 *
 * @param origin        Ray origin (world space).
 * @param dir           Ray direction (world space, normalized).
 * @param entities      Entity array (may be NULL if count is 0).
 * @param entity_count  Number of entity slots.
 * @param cache         Snap mesh cache (non-NULL).
 * @param exclude_id    Entity ID to skip (UINT32_MAX = skip none).
 * @param out           Output snap hit result (non-NULL).
 */
void snap_surface_cast_ray(vec3_t origin, vec3_t dir,
                             const struct edit_entity *entities,
                             uint32_t entity_count,
                             struct snap_mesh_cache *cache,
                             uint32_t exclude_id,
                             snap_hit_t *out);

/**
 * @brief Raycast against a single entity's snap mesh.
 *
 * Computes the entity's model matrix and tests ray vs its snap mesh.
 *
 * @param origin    Ray origin (world space).
 * @param dir       Ray direction (world space, normalized).
 * @param entity    Entity to test (non-NULL, must be active).
 * @param entity_id Entity ID (for snap_hit_t output).
 * @param cache     Snap mesh cache (non-NULL).
 * @param out       Output snap hit result (non-NULL).
 */
void snap_surface_cast_entity(vec3_t origin, vec3_t dir,
                                const struct edit_entity *entity,
                                uint32_t entity_id,
                                struct snap_mesh_cache *cache,
                                snap_hit_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SNAP_SURFACE_CAST_H */

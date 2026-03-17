/**
 * @file snap_surface_apply.h
 * @brief Apply snap hit results to entity position/orientation.
 *
 * Provides three snap application modes:
 *   - Face snap: move to hit point, orient to face normal.
 *   - Vertex snap: move to nearest vertex, orient to vertex normal.
 *   - Surface snap: face snap + offset along normal by AABB extent.
 *
 * Ownership: modifies entity in-place.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: no-op if hit is invalid.
 * Side effects: modifies entity position and orientation.
 *
 * Public types: none (uses types from snap_raycast.h, edit_entity.h).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SNAP_SURFACE_APPLY_H
#define FERRUM_EDITOR_VIEWPORT_SNAP_SURFACE_APPLY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"

/* Forward declarations. */
struct edit_entity;
struct snap_hit;
struct snap_mesh;

/* ---- Face snap (snap_surface_apply.c) ---- */

/**
 * @brief Apply face snap: set entity position and orient to face normal.
 *
 * Sets entity position to hit.position and orients the entity so
 * that its local +Y axis aligns with the hit normal.
 *
 * No-op if hit->valid is false.
 *
 * @param ent  Entity to modify (non-NULL).
 * @param hit  Snap hit result (non-NULL).
 */
void snap_apply_face(struct edit_entity *ent, const struct snap_hit *hit);

/**
 * @brief Apply vertex snap: snap to nearest vertex of hit triangle.
 *
 * Finds the closest vertex of the hit triangle (in world space),
 * sets entity position to that vertex position, and orients the
 * entity to match the vertex normal.
 *
 * No-op if hit->valid is false.
 *
 * @param ent    Entity to modify (non-NULL).
 * @param hit    Snap hit result with face_index (non-NULL).
 * @param mesh   Snap mesh for the hit entity (non-NULL).
 * @param model  Model matrix of the hit entity (non-NULL).
 */
void snap_apply_vertex(struct edit_entity *ent, const struct snap_hit *hit,
                         const struct snap_mesh *mesh, const mat4_t *model);

/* ---- Surface snap (snap_surface_apply_offset.c) ---- */

/**
 * @brief Apply surface snap: face snap + offset along normal by AABB extent.
 *
 * Sets entity position to hit.position + normal * offset, where
 * offset = dot(half_extents, abs(normal)). This places the entity
 * so it rests on the surface.
 *
 * Also orients entity to face normal (same as snap_apply_face).
 *
 * No-op if hit->valid is false.
 *
 * @param ent           Entity to modify (non-NULL).
 * @param hit           Snap hit result (non-NULL).
 * @param half_extents  AABB half-extents of the entity (in local space).
 */
void snap_apply_on_surface(struct edit_entity *ent,
                             const struct snap_hit *hit,
                             vec3_t half_extents);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SNAP_SURFACE_APPLY_H */

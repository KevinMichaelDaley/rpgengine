/**
 * @file mesh_primitives.h
 * @brief Mesh primitive generators — box, cylinder, sphere, plane.
 *
 * Each generator clears the target slot first, then fills it with
 * the requested geometry. Normals and UV0 are computed; UV1, tangents,
 * and colors are zeroed.
 *
 * Ownership: slot must be initialized before calling; generator writes
 * into existing slot buffers.
 * Nullability: NULL slot returns false.
 * Thread safety: not thread-safe (mutates slot).
 */
#ifndef FERRUM_EDITOR_MESH_PRIMITIVES_H
#define FERRUM_EDITOR_MESH_PRIMITIVES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/editor/mesh/mesh_slot.h"

/* ------------------------------------------------------------------------ */
/* Primitive generators                                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Generate an axis-aligned box mesh.
 *
 * @param slot  Target slot (cleared first). Must not be NULL.
 * @param size  Box dimensions [x, y, z]. Must not be NULL.
 * @param segs  Segments per axis [sx, sy, sz] (≥1). Must not be NULL.
 * @param pos   World position offset [x, y, z]. Must not be NULL.
 * @return true on success.
 */
bool mesh_prim_box(mesh_slot_t *slot, const float size[3],
                   const uint32_t segs[3], const float pos[3]);

/**
 * @brief Generate a plane mesh.
 *
 * @param slot  Target slot (cleared first).
 * @param size  Plane dimensions [width, depth].
 * @param segs  Subdivisions [sx, sz] (≥1).
 * @param axis  Up axis: 0=X, 1=Y, 2=Z.
 * @param pos   World position offset.
 * @return true on success.
 */
bool mesh_prim_plane(mesh_slot_t *slot, const float size[2],
                     const uint32_t segs[2], int axis, const float pos[3]);

/**
 * @brief Generate a cylinder mesh.
 *
 * @param slot      Target slot (cleared first).
 * @param radius    Cylinder radius.
 * @param height    Cylinder height.
 * @param segments  Number of radial segments (≥3).
 * @param axis      Up axis: 0=X, 1=Y, 2=Z.
 * @param pos       World position offset.
 * @return true on success.
 */
bool mesh_prim_cylinder(mesh_slot_t *slot, float radius, float height,
                        uint32_t segments, int axis, const float pos[3]);

/**
 * @brief Generate a UV sphere mesh.
 *
 * @param slot      Target slot (cleared first).
 * @param radius    Sphere radius.
 * @param segments  Number of longitude divisions.
 * @param rings     Number of latitude divisions (0 = segments/2).
 * @param pos       World position offset.
 * @return true on success.
 */
bool mesh_prim_sphere(mesh_slot_t *slot, float radius, uint32_t segments,
                      uint32_t rings, const float pos[3]);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_PRIMITIVES_H */

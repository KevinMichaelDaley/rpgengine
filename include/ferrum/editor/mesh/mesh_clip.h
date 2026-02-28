/**
 * @file mesh_clip.h
 * @brief Clip tool — split mesh by plane.
 *
 * Types: mesh_clip_mode_t (enum).
 * Functions: mesh_clip.
 */
#ifndef MESH_CLIP_H
#define MESH_CLIP_H

#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdbool.h>

/**
 * @brief Clip mode — which side to keep.
 */
typedef enum mesh_clip_mode {
    MESH_CLIP_FRONT = 0, /**< Keep geometry on positive side of plane. */
    MESH_CLIP_BACK  = 1  /**< Keep geometry on negative side of plane. */
} mesh_clip_mode_t;

/**
 * @brief Clip mesh by a plane, keeping one side.
 *
 * Triangles straddling the plane are split. Vertices on the
 * discarded side are removed. New vertices are created at
 * intersection points.
 *
 * @param slot      Mesh to clip. Not NULL.
 * @param plane_pt  A point on the plane (3 floats). Not NULL.
 * @param plane_nrm Plane normal (3 floats, must be unit). Not NULL.
 * @param mode      Which side to keep.
 * @return true on success.
 *
 * Ownership: caller owns slot. Modified in-place.
 */
bool mesh_clip(mesh_slot_t *slot, const float *plane_pt,
               const float *plane_nrm, mesh_clip_mode_t mode);

#endif /* MESH_CLIP_H */

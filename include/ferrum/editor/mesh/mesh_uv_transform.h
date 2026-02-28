/**
 * @file mesh_uv_transform.h
 * @brief UV-space transform commands for selected faces.
 *
 * Types: none (uses mesh_slot.h and mesh_edit.h types).
 * All operations apply only to UVs of vertices referenced by selected faces.
 * Pivot values of -1.0f mean "use UV centroid of selection".
 *
 * Ownership: modifies slot UV buffers in place.
 * Nullability: NULL returns false.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_UV_TRANSFORM_H
#define FERRUM_EDITOR_MESH_UV_TRANSFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>

/**
 * @brief Shift (translate) UVs of selected faces.
 *
 * @param slot    Mesh. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param du      U offset.
 * @param dv      V offset.
 * @param channel UV channel (0 or 1).
 * @return true on success.
 */
bool mesh_uv_shift(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                   float du, float dv, int channel);

/**
 * @brief Rotate UVs of selected faces.
 *
 * @param slot    Mesh. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param angle   Rotation angle in radians.
 * @param pivot_u Pivot U (-1 for centroid).
 * @param pivot_v Pivot V (-1 for centroid).
 * @param channel UV channel.
 * @return true on success.
 */
bool mesh_uv_rotate(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                    float angle, float pivot_u, float pivot_v, int channel);

/**
 * @brief Scale UVs of selected faces.
 *
 * @param slot    Mesh. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param su      Scale factor U.
 * @param sv      Scale factor V.
 * @param pivot_u Pivot U (-1 for centroid).
 * @param pivot_v Pivot V (-1 for centroid).
 * @param channel UV channel.
 * @return true on success.
 */
bool mesh_uv_scale(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                   float su, float sv, float pivot_u, float pivot_v,
                   int channel);

/**
 * @brief Fit UVs of selected faces to [0,1] range.
 *
 * @param slot    Mesh. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param channel UV channel.
 * @return true on success.
 */
bool mesh_uv_fit(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                 int channel);

/**
 * @brief Snap UVs to nearest grid point.
 *
 * @param slot      Mesh. Not NULL.
 * @param sel       Face selection. Not NULL.
 * @param grid_size Grid spacing (e.g. 0.25).
 * @param channel   UV channel.
 * @return true on success.
 */
bool mesh_uv_grid_snap(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                       float grid_size, int channel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_UV_TRANSFORM_H */

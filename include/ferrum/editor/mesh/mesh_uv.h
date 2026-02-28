/**
 * @file mesh_uv.h
 * @brief UV projection methods for mesh faces.
 *
 * Types: mesh_axis_t (enum).
 * Functions: mesh_uv_planar, mesh_uv_box, mesh_uv_cylindrical, mesh_uv_spherical.
 */
#ifndef MESH_UV_H
#define MESH_UV_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Axis enum for UV projection.
 */
typedef enum mesh_axis {
    MESH_AXIS_X = 0,
    MESH_AXIS_Y = 1,
    MESH_AXIS_Z = 2
} mesh_axis_t;

/**
 * @brief Planar UV projection.
 *
 * Projects selected face vertices onto a plane perpendicular to @p axis.
 * UVs normalized to [0,1] based on selected face bounding box.
 *
 * @param slot    Mesh to modify. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param axis    Projection axis.
 * @param channel UV channel (0 or 1).
 * @return true on success.
 */
bool mesh_uv_planar(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                    mesh_axis_t axis, int channel);

/**
 * @brief Box UV projection (triplanar).
 *
 * Each face is projected onto the nearest axis-aligned plane.
 *
 * @param slot    Mesh. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param channel UV channel.
 * @return true on success.
 */
bool mesh_uv_box(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                 int channel);

/**
 * @brief Cylindrical UV projection.
 *
 * Wraps UVs around a cylinder aligned to @p axis.
 * Angular position → U, height → V.
 *
 * @param slot    Mesh. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param axis    Cylinder axis.
 * @param channel UV channel.
 * @return true on success.
 */
bool mesh_uv_cylindrical(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                         mesh_axis_t axis, int channel);

/**
 * @brief Spherical UV projection.
 *
 * Longitude → U, latitude → V.
 *
 * @param slot    Mesh. Not NULL.
 * @param sel     Face selection. Not NULL.
 * @param channel UV channel.
 * @return true on success.
 */
bool mesh_uv_spherical(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                       int channel);

#endif /* MESH_UV_H */

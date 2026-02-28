/**
 * @file mesh_material_lift.c
 * @brief Material eyedropper — sample material from a face.
 */
#include "ferrum/editor/mesh/mesh_material_ops.h"
#include "ferrum/editor/mesh/mesh_material.h"

const char *mesh_material_lift(const mesh_slot_t *slot,
                               const mesh_material_map_t *map,
                               uint32_t face) {
    return mesh_material_get_face(slot, map, face);
}

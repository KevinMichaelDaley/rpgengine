/**
 * @file mesh_registry_remove.c
 * @brief Remove meshes from a mesh_registry_t by handle.
 */

#include "ferrum/renderer/mesh/mesh_registry.h"

/* ── mesh_registry_remove ─────────────────────────────────────────── */

void mesh_registry_remove(mesh_registry_t *reg, mesh_handle_t handle)
{
    if (!reg || !reg->types) { return; }
    if (handle.index >= reg->capacity) { return; }
    if (reg->generations[handle.index] != handle.generation) { return; }
    if (reg->types[handle.index] == MESH_TYPE_NONE) { return; }

    /* Destroy the mesh in the slot. */
    if (reg->types[handle.index] == MESH_TYPE_STATIC) {
        static_mesh_destroy(&reg->meshes[handle.index].stat);
    } else if (reg->types[handle.index] == MESH_TYPE_SKELETAL) {
        skeletal_mesh_destroy(&reg->meshes[handle.index].skel);
    }

    reg->types[handle.index] = MESH_TYPE_NONE;

    /* Bump generation to invalidate any outstanding handles. */
    ++reg->generations[handle.index];

    /* Return slot to freelist. */
    reg->freelist[reg->freelist_count++] = handle.index;

    --reg->count;
}

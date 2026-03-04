/**
 * @file mesh_registry_query.c
 * @brief Query functions for mesh_registry_t: validation, type, getters.
 */

#include "ferrum/renderer/mesh/mesh_registry.h"

/* ── mesh_registry_is_valid ───────────────────────────────────────── */

int mesh_registry_is_valid(const mesh_registry_t *reg,
                           mesh_handle_t handle)
{
    if (!reg || !reg->types) { return 0; }
    if (handle.index >= reg->capacity) { return 0; }
    if (reg->generations[handle.index] != handle.generation) { return 0; }
    return reg->types[handle.index] != MESH_TYPE_NONE;
}

/* ── mesh_registry_type ───────────────────────────────────────────── */

mesh_type_t mesh_registry_type(const mesh_registry_t *reg,
                               mesh_handle_t handle)
{
    if (!mesh_registry_is_valid(reg, handle)) { return MESH_TYPE_NONE; }
    return reg->types[handle.index];
}

/* ── mesh_registry_get_static ─────────────────────────────────────── */

const static_mesh_t *mesh_registry_get_static(const mesh_registry_t *reg,
                                               mesh_handle_t handle)
{
    if (!mesh_registry_is_valid(reg, handle)) { return NULL; }
    if (reg->types[handle.index] != MESH_TYPE_STATIC) { return NULL; }
    return &reg->meshes[handle.index].stat;
}

/* ── mesh_registry_get_skeletal ───────────────────────────────────── */

const skeletal_mesh_t *mesh_registry_get_skeletal(
    const mesh_registry_t *reg, mesh_handle_t handle)
{
    if (!mesh_registry_is_valid(reg, handle)) { return NULL; }
    if (reg->types[handle.index] != MESH_TYPE_SKELETAL) { return NULL; }
    return &reg->meshes[handle.index].skel;
}

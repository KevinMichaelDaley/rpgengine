/**
 * @file scene_viewport_collision_mesh.c
 * @brief Collision mesh loading for viewport rendering and snap.
 *
 * Manages a per-entity collision mesh cache parallel to the render mesh
 * cache. When a collision mesh is loaded, it overrides the render mesh
 * for snap raycasting (via snap_mesh_cache) and physics. The collision
 * mesh is also rendered as a wireframe overlay when toggled.
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_render_load_collision_mesh
 *   viewport_render_unload_collision_mesh
 *   viewport_render_get_collision_mesh
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/renderer/mesh/mesh_registry.h"
#include "ferrum/renderer/mesh/static_mesh.h"

#include <string.h>

/* ---- Sentinel ---- */

/** Check if a handle is the "none" sentinel ({0,0} — never valid
 * because mesh_registry starts generations at 1). */
static bool is_handle_none_(mesh_handle_t h) {
    return h.index == 0 && h.generation == 0;
}

/* ---- Public API ---- */

bool viewport_render_load_collision_mesh(viewport_render_state_t *state,
                                          uint32_t entity_id,
                                          const uint8_t *fvma_data,
                                          size_t fvma_size) {
    if (!state || !fvma_data || fvma_size == 0) return false;
    if (!state->initialized) return false;
    if (!state->collision_mesh_cache) return false;
    if (entity_id >= state->collision_mesh_cache_cap) return false;

    /* Deserialize FVMA binary into an editable mesh slot. */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    if (!mesh_vao_deserialize(fvma_data, fvma_size, &slot)) {
        return false;
    }

    /* Build static mesh creation descriptor from the deserialized slot. */
    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = slot.positions;
    info.normals      = slot.normals;
    info.tangents     = slot.tangents;
    info.uv0          = slot.uvs[0];
    info.uv1          = slot.uvs[1];
    info.colors       = slot.colors;
    info.indices      = slot.indices;
    info.vertex_count = slot.vertex_count;
    info.index_count  = slot.index_count;

    /* Insert into the viewport's mesh registry. */
    mesh_handle_t handle;
    int rc = mesh_registry_insert_static(&state->meshes, &info, &handle);

    /* Retain CPU-side geometry for surface snap raycasting.
     * This overwrites any render mesh snap data for this entity —
     * collision mesh takes priority for snapping. */
    snap_mesh_retain_from_slot(&state->snap_meshes, entity_id, &slot);

    /* Free the temporary deserialized slot (data is now on the GPU). */
    mesh_slot_destroy(&slot);

    if (rc != MESH_REGISTRY_OK) {
        return false;
    }

    /* Unload any previously loaded collision mesh for this entity. */
    mesh_handle_t old = state->collision_mesh_cache[entity_id];
    if (!is_handle_none_(old)) {
        if (mesh_registry_is_valid(&state->meshes, old)) {
            mesh_registry_remove(&state->meshes, old);
        }
    }

    /* Cache the new handle. */
    state->collision_mesh_cache[entity_id] = handle;
    return true;
}

void viewport_render_unload_collision_mesh(viewport_render_state_t *state,
                                            uint32_t entity_id) {
    if (!state || !state->initialized) return;
    if (!state->collision_mesh_cache) return;
    if (entity_id >= state->collision_mesh_cache_cap) return;

    mesh_handle_t handle = state->collision_mesh_cache[entity_id];
    if (!is_handle_none_(handle)) {
        if (mesh_registry_is_valid(&state->meshes, handle)) {
            mesh_registry_remove(&state->meshes, handle);
        }
        state->collision_mesh_cache[entity_id].index = 0;
        state->collision_mesh_cache[entity_id].generation = 0;
    }

    /* Remove snap mesh data. The caller should re-load the render mesh's
     * snap data if the entity still has a render mesh loaded. */
    snap_mesh_cache_remove(&state->snap_meshes, entity_id);
}

const static_mesh_t *viewport_render_get_collision_mesh(
    const viewport_render_state_t *state, uint32_t entity_id) {
    if (!state || !state->initialized) return NULL;
    if (!state->collision_mesh_cache) return NULL;
    if (entity_id >= state->collision_mesh_cache_cap) return NULL;

    mesh_handle_t handle = state->collision_mesh_cache[entity_id];
    if (is_handle_none_(handle)) return NULL;

    return mesh_registry_get_static(&state->meshes, handle);
}

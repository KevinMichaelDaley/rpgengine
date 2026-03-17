/**
 * @file scene_viewport_mesh.c
 * @brief Viewport entity mesh loading: FVMA → mesh registry.
 *
 * Loads FVMA binary data into the viewport's mesh registry and caches
 * the resulting mesh handle per entity ID. This enables MESH type
 * entities to render with their actual geometry instead of primitives.
 *
 * Flow: FVMA binary → mesh_vao_deserialize → mesh_slot_t →
 *       static_mesh_create_info_t → mesh_registry_insert_static →
 *       cache handle by entity_id.
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_render_load_entity_mesh
 *   viewport_render_unload_entity_mesh
 *   viewport_render_get_entity_mesh
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/renderer/mesh/mesh_registry.h"
#include "ferrum/renderer/mesh/static_mesh.h"

#include <string.h>

/* ---- Sentinel ---- */

/**
 * @brief Sentinel mesh handle indicating "no mesh loaded".
 *
 * Zero-initialized: {index=0, generation=0}. This is safe because
 * mesh_registry_init sets all slot generations to 1, so generation 0
 * is never valid. This allows vm_reserve'd (zero-filled) cache arrays
 * to serve as "no mesh" without explicit initialization.
 */
static const mesh_handle_t MESH_HANDLE_NONE = {0, 0};

/** Check if a handle is the "none" sentinel. */
static bool is_handle_none_(mesh_handle_t h) {
    return h.index == 0 && h.generation == 0;
}

/* ---- Public API ---- */

bool viewport_render_load_entity_mesh(viewport_render_state_t *state,
                                       uint32_t entity_id,
                                       const uint8_t *fvma_data,
                                       size_t fvma_size) {
    if (!state || !fvma_data || fvma_size == 0) return false;
    if (!state->initialized) return false;
    if (!state->entity_mesh_cache) return false;
    if (entity_id >= state->entity_mesh_cache_cap) return false;

    /* Deserialize FVMA binary into an editable mesh slot. */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    if (!mesh_vao_deserialize(fvma_data, fvma_size, &slot)) return false;

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

    /* Retain CPU-side geometry for surface snap raycasting. */
    snap_mesh_retain_from_slot(&state->snap_meshes, entity_id, &slot);

    /* Free the temporary deserialized slot (data is now on the GPU). */
    mesh_slot_destroy(&slot);

    if (rc != MESH_REGISTRY_OK) return false;

    /* Unload any previously loaded mesh for this entity. */
    mesh_handle_t old = state->entity_mesh_cache[entity_id];
    if (!is_handle_none_(old)) {
        if (mesh_registry_is_valid(&state->meshes, old)) {
            mesh_registry_remove(&state->meshes, old);
        }
    }

    /* Cache the new handle. */
    state->entity_mesh_cache[entity_id] = handle;
    return true;
}

void viewport_render_unload_entity_mesh(viewport_render_state_t *state,
                                         uint32_t entity_id) {
    if (!state || !state->initialized) return;
    if (!state->entity_mesh_cache) return;
    if (entity_id >= state->entity_mesh_cache_cap) return;

    mesh_handle_t handle = state->entity_mesh_cache[entity_id];
    if (!is_handle_none_(handle)) {
        if (mesh_registry_is_valid(&state->meshes, handle)) {
            mesh_registry_remove(&state->meshes, handle);
        }
        state->entity_mesh_cache[entity_id] = MESH_HANDLE_NONE;
    }

    /* Remove CPU-side snap mesh data. */
    snap_mesh_cache_remove(&state->snap_meshes, entity_id);
}

const static_mesh_t *viewport_render_get_entity_mesh(
    const viewport_render_state_t *state, uint32_t entity_id) {
    if (!state || !state->initialized) return NULL;
    if (!state->entity_mesh_cache) return NULL;
    if (entity_id >= state->entity_mesh_cache_cap) return NULL;

    mesh_handle_t handle = state->entity_mesh_cache[entity_id];
    if (is_handle_none_(handle)) return NULL;

    return mesh_registry_get_static(&state->meshes, handle);
}

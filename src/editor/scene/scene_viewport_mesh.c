/**
 * @file scene_viewport_mesh.c
 * @brief Viewport entity mesh loading: FVMA → mesh registry.
 *
 * Loads FVMA binary data into the viewport's mesh registry as a
 * static_mesh_t, regardless of whether the FVMA has bone data.
 * Skeletal promotion only happens via an explicit user action
 * (assigning an .fskel in the inspector), handled by
 * viewport_render_promote_to_skeletal() in scene_viewport_skel.c.
 *
 * When a mesh is convex-decomposed for snapping, the decomposed hull
 * data is also uploaded as a collision overlay GPU mesh so pressing C
 * shows the convex hull wireframe rather than the raw mesh.
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_render_load_entity_mesh
 *   viewport_render_unload_entity_mesh
 *   viewport_render_get_entity_mesh
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/viewport/snap/snap_mesh_decompose.h"
#include "ferrum/editor/viewport/viewport_mesh_type.h"
#include "ferrum/renderer/mesh/mesh_registry.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"

#include <stdio.h>
#include <stdlib.h>
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

/**
 * @brief Upload snap mesh (convex hull) data as a collision overlay GPU mesh.
 *
 * After convex decomposition, the snap cache holds the simplified hull
 * geometry (CPU-only). This function creates a GPU static_mesh_t from
 * that data and stores it in collision_mesh_cache, so pressing C shows
 * the convex hull wireframe instead of the raw dense mesh.
 */
static void upload_snap_as_collision_overlay_(viewport_render_state_t *state,
                                               uint32_t entity_id) {
    if (!state->collision_mesh_cache) return;
    if (entity_id >= state->collision_mesh_cache_cap) return;

    const snap_mesh_t *snap = snap_mesh_cache_get(&state->snap_meshes,
                                                    entity_id);
    if (!snap || !snap->positions || snap->index_count == 0) return;

    /* Build a static mesh from the snap cache's convex hull data. */
    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = snap->positions;
    info.normals      = snap->normals;
    info.indices      = snap->indices;
    info.vertex_count = snap->vertex_count;
    info.index_count  = snap->index_count;

    mesh_handle_t handle;
    int rc = mesh_registry_insert_static(&state->meshes, &info, &handle);
    if (rc != MESH_REGISTRY_OK) return;

    /* Replace any existing collision mesh for this entity. */
    mesh_handle_t old = state->collision_mesh_cache[entity_id];
    if (!is_handle_none_(old)) {
        if (mesh_registry_is_valid(&state->meshes, old)) {
            mesh_registry_remove(&state->meshes, old);
        }
    }
    state->collision_mesh_cache[entity_id] = handle;
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

    /* Unload any previously loaded mesh for this entity. */
    viewport_render_unload_entity_mesh(state, entity_id);

    /* Always load as static mesh. Skeletal promotion is an explicit
     * user action triggered by fskel assignment in the inspector. */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    if (!mesh_vao_deserialize(fvma_data, fvma_size, &slot)) return false;

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

    mesh_handle_t handle;
    int rc = mesh_registry_insert_static(&state->meshes, &info, &handle);

    /* Retain CPU-side geometry for surface snap raycasting.
     * High-poly meshes get convex-decomposed to prevent O(n) raycast
     * and O(n*m) depenetration from crashing the editor. */
    bool decomposed = snap_mesh_should_decompose(&slot);
    if (decomposed) {
        snap_mesh_retain_decomposed(&state->snap_meshes, entity_id, &slot,
                                         &state->decompose_cache);
    } else {
        snap_mesh_retain_from_slot(&state->snap_meshes, entity_id, &slot);
    }

    mesh_slot_destroy(&slot);

    if (rc != MESH_REGISTRY_OK) return false;

    /* Upload decomposed snap hull as collision overlay GPU mesh. */
    if (decomposed) {
        upload_snap_as_collision_overlay_(state, entity_id);
    }

    state->entity_mesh_cache[entity_id] = handle;
    if (state->entity_mesh_types) {
        state->entity_mesh_types[entity_id] = VIEWPORT_MESH_STATIC;
    }
    return true;
}

void viewport_render_unload_entity_mesh(viewport_render_state_t *state,
                                         uint32_t entity_id) {
    if (!state || !state->initialized) return;
    if (!state->entity_mesh_cache) return;
    if (entity_id >= state->entity_mesh_cache_cap) return;

    /* Destroy skeletal mesh if present. */
    if (state->skeletal_mesh_cache &&
        state->skeletal_mesh_cache[entity_id]) {
        skeletal_mesh_destroy(state->skeletal_mesh_cache[entity_id]);
        free(state->skeletal_mesh_cache[entity_id]);
        state->skeletal_mesh_cache[entity_id] = NULL;
    }

    /* Remove static mesh handle from registry. */
    mesh_handle_t handle = state->entity_mesh_cache[entity_id];
    if (!is_handle_none_(handle)) {
        if (mesh_registry_is_valid(&state->meshes, handle)) {
            mesh_registry_remove(&state->meshes, handle);
        }
        state->entity_mesh_cache[entity_id] = MESH_HANDLE_NONE;
    }

    /* Reset mesh type. */
    if (state->entity_mesh_types) {
        state->entity_mesh_types[entity_id] = VIEWPORT_MESH_NONE;
    }

    /* Remove CPU-side snap mesh data. */
    snap_mesh_cache_remove(&state->snap_meshes, entity_id);
}

const static_mesh_t *viewport_render_get_entity_mesh(
    const viewport_render_state_t *state, uint32_t entity_id) {
    if (!state || !state->initialized) return NULL;
    if (!state->entity_mesh_cache) return NULL;
    if (entity_id >= state->entity_mesh_cache_cap) return NULL;

    /* Check for skeletal mesh first — render via base field. */
    if (state->skeletal_mesh_cache &&
        state->skeletal_mesh_cache[entity_id]) {
        return &state->skeletal_mesh_cache[entity_id]->base;
    }

    /* Fall back to static mesh from registry. */
    mesh_handle_t handle = state->entity_mesh_cache[entity_id];
    if (is_handle_none_(handle)) return NULL;

    return mesh_registry_get_static(&state->meshes, handle);
}

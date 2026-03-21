/**
 * @file scene_viewport_skel.c
 * @brief Explicit static→skeletal mesh promotion for viewport entities.
 *
 * When a user assigns an .fskel file to a MESH entity in the inspector,
 * this module destroys the entity's static mesh and reloads the FVMA
 * as a skeletal_mesh_t (if the FVMA has MESH_VAO_FLAG_BONES). The
 * promotion is an explicit user action — meshes are never automatically
 * promoted based on FVMA flags.
 *
 * Non-static functions (2 / 4 limit):
 *   viewport_render_promote_to_skeletal
 *   viewport_render_demote_to_static_blocked
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/viewport/viewport_mesh_type.h"
#include "ferrum/renderer/mesh/mesh_registry.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Sentinel mesh handle. */
static const mesh_handle_t SKEL_HANDLE_NONE_ = {0, 0};

static bool is_handle_none_(mesh_handle_t h) {
    return h.index == 0 && h.generation == 0;
}

/**
 * @brief Check FVMA header flags without full deserialization.
 */
static uint32_t read_fvma_flags_(const uint8_t *data, size_t size) {
    if (!data || size < MESH_VAO_HEADER_SIZE) return 0;
    uint32_t magic;
    memcpy(&magic, data, 4);
    if (magic != MESH_VAO_MAGIC) return 0;
    uint32_t flags;
    memcpy(&flags, data + 16, 4);
    return flags;
}

bool viewport_render_promote_to_skeletal(viewport_render_state_t *state,
                                          uint32_t entity_id,
                                          const uint8_t *fvma_data,
                                          size_t fvma_size) {
    if (!state || !state->initialized) return false;
    if (!fvma_data || fvma_size == 0) return false;
    if (entity_id >= state->entity_mesh_cache_cap) return false;
    if (!state->skeletal_mesh_cache) return false;
    if (!state->entity_mesh_types) return false;

    /* Check current mesh type — reject if already skeletal (no-op). */
    viewport_mesh_type_t current = state->entity_mesh_types[entity_id];
    if (current == VIEWPORT_MESH_SKELETAL) {
        fprintf(stderr, "viewport_skel: entity %u is already skeletal\n",
                entity_id);
        return true; /* Not an error, just a no-op. */
    }

    /* Validate the transition. */
    if (!viewport_mesh_type_can_upgrade(current, VIEWPORT_MESH_SKELETAL)) {
        fprintf(stderr,
                "viewport_skel: cannot promote entity %u (type %d → skeletal)\n",
                entity_id, (int)current);
        return false;
    }

    /* Verify the FVMA has bone data. */
    uint32_t flags = read_fvma_flags_(fvma_data, fvma_size);
    if (!(flags & MESH_VAO_FLAG_BONES)) {
        fprintf(stderr,
                "viewport_skel: entity %u FVMA has no bone data, "
                "cannot bind skeleton\n", entity_id);
        return false;
    }

    /* Create skeletal mesh from FVMA. */
    skeletal_mesh_t *skel = (skeletal_mesh_t *)calloc(1, sizeof(*skel));
    if (!skel) return false;

    int rc = skeletal_mesh_create_from_fvma(&state->loader,
                                              fvma_data, fvma_size, skel);
    if (rc != SKELETAL_MESH_OK) {
        fprintf(stderr,
                "viewport_skel: skeletal mesh creation failed for entity %u "
                "(rc=%d)\n", entity_id, rc);
        free(skel);
        return false;
    }

    /* Destroy the old static mesh from the registry. */
    mesh_handle_t old_handle = state->entity_mesh_cache[entity_id];
    if (!is_handle_none_(old_handle)) {
        if (mesh_registry_is_valid(&state->meshes, old_handle)) {
            mesh_registry_remove(&state->meshes, old_handle);
        }
        state->entity_mesh_cache[entity_id] = SKEL_HANDLE_NONE_;
    }

    /* Store skeletal mesh and update type. */
    state->skeletal_mesh_cache[entity_id] = skel;
    state->entity_mesh_types[entity_id] = VIEWPORT_MESH_SKELETAL;

    printf("viewport_skel: promoted entity %u to skeletal (%u bones)\n",
           entity_id, skel->bone_count);
    return true;
}

bool viewport_render_demote_to_static_blocked(
    const viewport_render_state_t *state, uint32_t entity_id) {
    if (!state || !state->entity_mesh_types) return false;
    if (entity_id >= state->entity_mesh_cache_cap) return false;

    if (state->entity_mesh_types[entity_id] == VIEWPORT_MESH_SKELETAL) {
        fprintf(stderr,
                "viewport_skel: ERROR — cannot strip skeleton binding from "
                "entity %u. Operation is destructive and irreversible.\n",
                entity_id);
        return true; /* Yes, it IS blocked. */
    }
    return false; /* Not blocked — entity is not skeletal. */
}

/**
 * @file mesh_commit.c
 * @brief Bake mesh_slot_t into FVMA asset + world entity.
 */
#include "ferrum/editor/mesh/mesh_commit.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Compute bounding box center and full extents of a mesh slot. */
static void bbox_compute_(const mesh_slot_t *slot,
                           float center[3], float extents[3]) {
    float lo[3] = { 1e30f,  1e30f,  1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        for (int a = 0; a < 3; a++) {
            float val = slot->positions[v * 3 + a];
            if (val < lo[a]) lo[a] = val;
            if (val > hi[a]) hi[a] = val;
        }
    }
    for (int a = 0; a < 3; a++) {
        center[a]  = (lo[a] + hi[a]) * 0.5f;
        extents[a] = hi[a] - lo[a];
        if (extents[a] < 1e-6f) extents[a] = 0.01f; /* avoid zero */
    }
}

/** Serialize mesh to FVMA with all available attributes. */
static bool serialize_fvma_(const mesh_slot_t *slot,
                             uint8_t **out_data, size_t *out_size) {
    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    if (slot->uvs[0]) flags |= MESH_VAO_FLAG_UV0;
    if (slot->uvs[1]) flags |= MESH_VAO_FLAG_UV1;
    if (slot->tangents) flags |= MESH_VAO_FLAG_TANGENTS;
    if (slot->colors) flags |= MESH_VAO_FLAG_COLORS;

    size_t size = mesh_vao_serialized_size(slot, flags);
    if (size == 0) return false;

    uint8_t *buf = malloc(size);
    if (!buf) return false;

    size_t written = mesh_vao_serialize(slot, flags, buf, size);
    if (written == 0) {
        free(buf);
        return false;
    }

    *out_data = buf;
    *out_size = written;
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_commit(mesh_slot_t *slot,
                 edit_entity_store_t *store,
                 const mesh_commit_args_t *args,
                 mesh_commit_result_t *result) {
    if (!slot || !store || !args || !result) return false;
    if (slot->vertex_count == 0 || slot->index_count == 0) return false;

    memset(result, 0, sizeof(*result));

    /* 1. Serialize to FVMA */
    if (!serialize_fvma_(slot, &result->fvma_data, &result->fvma_size)) {
        return false;
    }

    /* 2. Create entity */
    uint32_t eid = edit_entity_store_create(store, EDIT_ENTITY_TYPE_MESH);
    if (eid == EDIT_ENTITY_INVALID_ID) {
        free(result->fvma_data);
        result->fvma_data = NULL;
        return false;
    }
    result->entity_id = eid;

    /* 3. Configure entity */
    edit_entity_t *ent = edit_entity_store_get_mut(store, eid);
    if (ent) {
        /* Position at bounding box center; scale = bbox extents
         * so physics bridge can derive half-extents correctly. */
        float extents[3];
        bbox_compute_(slot, ent->pos, extents);
        ent->scale[0] = extents[0];
        ent->scale[1] = extents[1];
        ent->scale[2] = extents[2];

        /* Name: use provided name, or auto-generate from entity ID. */
        if (args->entity_name[0] != '\0') {
            strncpy(ent->name, args->entity_name, sizeof(ent->name) - 1);
            ent->name[sizeof(ent->name) - 1] = '\0';
        } else {
            snprintf(ent->name, sizeof(ent->name), "mesh_%u", eid);
        }

        /* Material override (apply to slot 0) */
        if (args->material_override[0] != '\0') {
            strncpy(ent->materials[0], args->material_override,
                    sizeof(ent->materials[0]) - 1);
            ent->materials[0][sizeof(ent->materials[0]) - 1] = '\0';
        }
    }

    /* 4. Optionally clear mesh slot */
    if (args->clear_slot) {
        mesh_slot_clear(slot);
    }

    return true;
}

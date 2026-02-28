/**
 * @file mesh_slot.c
 * @brief mesh_slot_t lifecycle — init, destroy, clear, face_count.
 */
#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Allocate all vertex attribute buffers for the given capacity.
 *        Returns false on any allocation failure; partial allocs are freed.
 */
static bool alloc_vertex_buffers_(mesh_slot_t *slot, uint32_t cap) {
    if (cap == 0) {
        slot->positions = NULL;
        slot->normals   = NULL;
        slot->tangents  = NULL;
        slot->uvs[0]    = NULL;
        slot->uvs[1]    = NULL;
        slot->colors    = NULL;
        slot->vertex_capacity = 0;
        return true;
    }

    slot->positions = calloc(cap * 3, sizeof(float));
    slot->normals   = calloc(cap * 3, sizeof(float));
    slot->tangents  = calloc(cap * 4, sizeof(float));
    slot->uvs[0]    = calloc(cap * 2, sizeof(float));
    slot->uvs[1]    = calloc(cap * 2, sizeof(float));
    slot->colors    = calloc(cap * 4, sizeof(float));

    if (!slot->positions || !slot->normals || !slot->tangents ||
        !slot->uvs[0] || !slot->uvs[1] || !slot->colors) {
        free(slot->positions);
        free(slot->normals);
        free(slot->tangents);
        free(slot->uvs[0]);
        free(slot->uvs[1]);
        free(slot->colors);
        memset(slot, 0, sizeof(*slot));
        return false;
    }

    slot->vertex_capacity = cap;
    return true;
}

/**
 * @brief Allocate index + polygroup buffers for the given capacity.
 */
static bool alloc_index_buffers_(mesh_slot_t *slot, uint32_t cap) {
    if (cap == 0) {
        slot->indices       = NULL;
        slot->polygroup_ids = NULL;
        slot->index_capacity = 0;
        return true;
    }

    slot->indices       = calloc(cap, sizeof(uint32_t));
    /* Polygroup IDs: one per triangle = cap / 3. Allocate cap/3 + 1 for safety. */
    uint32_t face_cap   = cap / 3 + 1;
    slot->polygroup_ids = calloc(face_cap, sizeof(uint16_t));

    if (!slot->indices || !slot->polygroup_ids) {
        free(slot->indices);
        free(slot->polygroup_ids);
        slot->indices       = NULL;
        slot->polygroup_ids = NULL;
        slot->index_capacity = 0;
        return false;
    }

    slot->index_capacity = cap;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

bool mesh_slot_init(mesh_slot_t *slot, uint32_t vert_capacity,
                    uint32_t idx_capacity) {
    if (!slot) { return false; }
    memset(slot, 0, sizeof(*slot));

    if (!alloc_vertex_buffers_(slot, vert_capacity)) { return false; }
    if (!alloc_index_buffers_(slot, idx_capacity)) {
        /* Clean up vertex buffers on index alloc failure */
        free(slot->positions);
        free(slot->normals);
        free(slot->tangents);
        free(slot->uvs[0]);
        free(slot->uvs[1]);
        free(slot->colors);
        memset(slot, 0, sizeof(*slot));
        return false;
    }

    return true;
}

void mesh_slot_destroy(mesh_slot_t *slot) {
    if (!slot) { return; }
    free(slot->positions);
    free(slot->normals);
    free(slot->tangents);
    free(slot->uvs[0]);
    free(slot->uvs[1]);
    free(slot->colors);
    free(slot->indices);
    free(slot->polygroup_ids);
    memset(slot, 0, sizeof(*slot));
}

void mesh_slot_clear(mesh_slot_t *slot) {
    if (!slot) { return; }
    slot->vertex_count = 0;
    slot->index_count  = 0;
}

uint32_t mesh_slot_face_count(const mesh_slot_t *slot) {
    if (!slot) { return 0; }
    return slot->index_count / 3;
}

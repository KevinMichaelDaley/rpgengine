/**
 * @file mesh_slot_add.c
 * @brief mesh_slot_t geometry addition — add_vertex, add_triangle.
 */
#include "ferrum/editor/mesh/mesh_slot.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

uint32_t mesh_slot_add_vertex(mesh_slot_t *slot, const float *pos,
                              const float *nrm) {
    if (!slot || !pos || !nrm) { return UINT32_MAX; }

    /* Grow if needed */
    if (slot->vertex_count >= slot->vertex_capacity) {
        uint32_t new_cap = slot->vertex_capacity ? slot->vertex_capacity * 2 : 8;
        if (new_cap > MESH_SLOT_MAX_VERTICES) { new_cap = MESH_SLOT_MAX_VERTICES; }
        if (new_cap <= slot->vertex_count) { return UINT32_MAX; }
        if (!mesh_slot_reserve_vertices(slot, new_cap)) { return UINT32_MAX; }
    }

    uint32_t idx = slot->vertex_count;
    uint32_t off3 = idx * 3;
    uint32_t off4 = idx * 4;
    uint32_t off2 = idx * 2;

    /* Position */
    slot->positions[off3 + 0] = pos[0];
    slot->positions[off3 + 1] = pos[1];
    slot->positions[off3 + 2] = pos[2];

    /* Normal */
    slot->normals[off3 + 0] = nrm[0];
    slot->normals[off3 + 1] = nrm[1];
    slot->normals[off3 + 2] = nrm[2];

    /* Tangent — zero */
    slot->tangents[off4 + 0] = 0.0f;
    slot->tangents[off4 + 1] = 0.0f;
    slot->tangents[off4 + 2] = 0.0f;
    slot->tangents[off4 + 3] = 0.0f;

    /* UVs — zero */
    slot->uvs[0][off2 + 0] = 0.0f;
    slot->uvs[0][off2 + 1] = 0.0f;
    slot->uvs[1][off2 + 0] = 0.0f;
    slot->uvs[1][off2 + 1] = 0.0f;

    /* Colors — zero */
    slot->colors[off4 + 0] = 0.0f;
    slot->colors[off4 + 1] = 0.0f;
    slot->colors[off4 + 2] = 0.0f;
    slot->colors[off4 + 3] = 0.0f;

    slot->vertex_count++;
    return idx;
}

bool mesh_slot_add_triangle(mesh_slot_t *slot, uint32_t i0, uint32_t i1,
                            uint32_t i2, uint16_t polygroup) {
    if (!slot) { return false; }

    /* Need 3 more indices */
    uint32_t needed = slot->index_count + 3;
    if (needed > slot->index_capacity) {
        uint32_t new_cap = slot->index_capacity ? slot->index_capacity * 2 : 12;
        while (new_cap < needed) { new_cap *= 2; }
        if (new_cap > MESH_SLOT_MAX_INDICES) { new_cap = MESH_SLOT_MAX_INDICES; }
        if (new_cap < needed) { return false; }
        if (!mesh_slot_reserve_indices(slot, new_cap)) { return false; }
    }

    slot->indices[slot->index_count + 0] = i0;
    slot->indices[slot->index_count + 1] = i1;
    slot->indices[slot->index_count + 2] = i2;

    uint32_t face_idx = slot->index_count / 3;
    slot->polygroup_ids[face_idx] = polygroup;

    slot->index_count += 3;
    return true;
}

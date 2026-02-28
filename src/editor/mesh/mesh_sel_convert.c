/**
 * @file mesh_sel_convert.c
 * @brief Selection conversion between modes.
 *
 * Non-static functions (4): face_to_vertex, vertex_to_face,
 * face_to_edge, edge_to_face.
 */
#include "ferrum/editor/mesh/mesh_selection.h"

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void mesh_sel_convert_face_to_vertex(const mesh_slot_t *slot,
                                     const mesh_sel_bitset_t *faces,
                                     mesh_sel_bitset_t *out_verts) {
    if (!slot || !faces || !out_verts) { return; }
    mesh_sel_bitset_clear_all(out_verts);

    uint32_t face_count = mesh_slot_face_count(slot);
    for (uint32_t f = 0; f < face_count; f++) {
        if (!mesh_sel_bitset_test(faces, f)) { continue; }
        uint32_t base = f * 3;
        mesh_sel_bitset_set(out_verts, slot->indices[base + 0]);
        mesh_sel_bitset_set(out_verts, slot->indices[base + 1]);
        mesh_sel_bitset_set(out_verts, slot->indices[base + 2]);
    }
}

void mesh_sel_convert_vertex_to_face(const mesh_slot_t *slot,
                                     const mesh_sel_bitset_t *verts,
                                     mesh_sel_bitset_t *out_faces) {
    if (!slot || !verts || !out_faces) { return; }
    mesh_sel_bitset_clear_all(out_faces);

    uint32_t face_count = mesh_slot_face_count(slot);
    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t base = f * 3;
        uint32_t v0 = slot->indices[base + 0];
        uint32_t v1 = slot->indices[base + 1];
        uint32_t v2 = slot->indices[base + 2];
        /* Select face only if ALL 3 vertices are selected */
        if (mesh_sel_bitset_test(verts, v0) &&
            mesh_sel_bitset_test(verts, v1) &&
            mesh_sel_bitset_test(verts, v2)) {
            mesh_sel_bitset_set(out_faces, f);
        }
    }
}

void mesh_sel_convert_face_to_edge(const mesh_slot_t *slot,
                                   const mesh_edge_table_t *table,
                                   const mesh_sel_bitset_t *faces,
                                   mesh_sel_bitset_t *out_edges) {
    if (!slot || !table || !faces || !out_edges) { return; }
    mesh_sel_bitset_clear_all(out_edges);

    uint32_t face_count = mesh_slot_face_count(slot);
    for (uint32_t f = 0; f < face_count; f++) {
        if (!mesh_sel_bitset_test(faces, f)) { continue; }
        uint32_t base = f * 3;
        uint32_t v0 = slot->indices[base + 0];
        uint32_t v1 = slot->indices[base + 1];
        uint32_t v2 = slot->indices[base + 2];

        uint32_t e01 = mesh_edge_table_find(table, v0, v1);
        uint32_t e12 = mesh_edge_table_find(table, v1, v2);
        uint32_t e02 = mesh_edge_table_find(table, v0, v2);

        if (e01 != UINT32_MAX) { mesh_sel_bitset_set(out_edges, e01); }
        if (e12 != UINT32_MAX) { mesh_sel_bitset_set(out_edges, e12); }
        if (e02 != UINT32_MAX) { mesh_sel_bitset_set(out_edges, e02); }
    }
}

void mesh_sel_convert_edge_to_face(const mesh_slot_t *slot,
                                   const mesh_edge_table_t *table,
                                   const mesh_sel_bitset_t *edges,
                                   mesh_sel_bitset_t *out_faces) {
    if (!slot || !table || !edges || !out_faces) { return; }
    mesh_sel_bitset_clear_all(out_faces);

    uint32_t face_count = mesh_slot_face_count(slot);
    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t base = f * 3;
        uint32_t v0 = slot->indices[base + 0];
        uint32_t v1 = slot->indices[base + 1];
        uint32_t v2 = slot->indices[base + 2];

        uint32_t e01 = mesh_edge_table_find(table, v0, v1);
        uint32_t e12 = mesh_edge_table_find(table, v1, v2);
        uint32_t e02 = mesh_edge_table_find(table, v0, v2);

        /* Select face if ANY edge is selected */
        if ((e01 != UINT32_MAX && mesh_sel_bitset_test(edges, e01)) ||
            (e12 != UINT32_MAX && mesh_sel_bitset_test(edges, e12)) ||
            (e02 != UINT32_MAX && mesh_sel_bitset_test(edges, e02))) {
            mesh_sel_bitset_set(out_faces, f);
        }
    }
}

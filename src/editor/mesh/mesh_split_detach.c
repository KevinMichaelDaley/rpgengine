/**
 * @file mesh_split_detach.c
 * @brief Split selection boundary + detach faces to another slot.
 *
 * Non-static functions (2 of 4): mesh_split_selection, mesh_detach.
 */
#include "ferrum/editor/mesh/mesh_normals.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Public: mesh_split_selection                                        */
/* ------------------------------------------------------------------ */

bool mesh_split_selection(mesh_slot_t *slot, const mesh_sel_bitset_t *sel) {
    if (!slot || !sel) return false;

    uint32_t face_count = slot->index_count / 3;
    uint32_t vc = slot->vertex_count;

    /* Mark vertices: used_sel (by selected faces), used_unsel (by unselected) */
    bool *used_sel = calloc(vc, sizeof(bool));
    bool *used_unsel = calloc(vc, sizeof(bool));
    if (!used_sel || !used_unsel) { free(used_sel); free(used_unsel); return false; }

    for (uint32_t f = 0; f < face_count; f++) {
        bool is_sel = mesh_sel_bitset_test(sel, f);
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            if (is_sel) used_sel[vi] = true;
            else        used_unsel[vi] = true;
        }
    }

    /* Vertices shared between selected and unselected need duplication */
    /* Remap: original → new (for selected faces only) */
    uint32_t *remap = malloc(vc * sizeof(uint32_t));
    if (!remap) { free(used_sel); free(used_unsel); return false; }
    for (uint32_t v = 0; v < vc; v++) remap[v] = v;

    /* Reserve for worst case */
    uint32_t shared_count = 0;
    for (uint32_t v = 0; v < vc; v++) {
        if (used_sel[v] && used_unsel[v]) shared_count++;
    }
    mesh_slot_reserve_vertices(slot, slot->vertex_count + shared_count);

    /* Duplicate shared vertices */
    for (uint32_t v = 0; v < vc; v++) {
        if (!used_sel[v] || !used_unsel[v]) continue;

        /* Duplicate vertex */
        float *pos = &slot->positions[v*3];
        float *nrm = &slot->normals[v*3];
        uint32_t nv = mesh_slot_add_vertex(slot, pos, nrm);
        if (nv == UINT32_MAX) {
            free(remap); free(used_sel); free(used_unsel);
            return false;
        }

        /* Copy attributes */
        for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
            if (slot->uvs[ch]) {
                slot->uvs[ch][nv*2+0] = slot->uvs[ch][v*2+0];
                slot->uvs[ch][nv*2+1] = slot->uvs[ch][v*2+1];
            }
        }
        if (slot->colors) {
            memcpy(&slot->colors[nv*4], &slot->colors[v*4], 4*sizeof(float));
        }

        remap[v] = nv;
    }

    /* Remap selected face indices */
    for (uint32_t f = 0; f < face_count; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;
        for (int j = 0; j < 3; j++) {
            slot->indices[f*3+j] = remap[slot->indices[f*3+j]];
        }
    }

    free(remap);
    free(used_sel);
    free(used_unsel);
    return true;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_detach                                                 */
/* ------------------------------------------------------------------ */

bool mesh_detach(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                 mesh_slot_t *target) {
    if (!slot || !sel || !target) return false;

    uint32_t face_count = slot->index_count / 3;

    /* Copy selected faces to target, build vertex remap */
    uint32_t *remap = malloc(slot->vertex_count * sizeof(uint32_t));
    if (!remap) return false;
    memset(remap, 0xFF, slot->vertex_count * sizeof(uint32_t));

    for (uint32_t f = 0; f < face_count; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;

        uint32_t mapped[3];
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            if (remap[vi] == UINT32_MAX) {
                /* Copy vertex to target */
                float *pos = &slot->positions[vi*3];
                float *nrm = &slot->normals[vi*3];
                uint32_t nv = mesh_slot_add_vertex(target, pos, nrm);
                if (nv == UINT32_MAX) { free(remap); return false; }
                remap[vi] = nv;
            }
            mapped[j] = remap[vi];
        }

        uint16_t pg = slot->polygroup_ids ? slot->polygroup_ids[f] : 0;
        mesh_slot_add_triangle(target, mapped[0], mapped[1], mapped[2], pg);
    }

    /* Remove selected faces from source (compact) */
    uint32_t write = 0;
    for (uint32_t f = 0; f < face_count; f++) {
        if (mesh_sel_bitset_test(sel, f)) continue;
        if (write != f) {
            slot->indices[write*3+0] = slot->indices[f*3+0];
            slot->indices[write*3+1] = slot->indices[f*3+1];
            slot->indices[write*3+2] = slot->indices[f*3+2];
            if (slot->polygroup_ids) {
                slot->polygroup_ids[write] = slot->polygroup_ids[f];
            }
        }
        write++;
    }
    slot->index_count = write * 3;

    free(remap);
    return true;
}

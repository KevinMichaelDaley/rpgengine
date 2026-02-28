/**
 * @file mesh_clipboard.c
 * @brief Mesh copy and paste operations.
 *
 * Non-static functions (2 of 4): mesh_copy, mesh_paste.
 */
#include "ferrum/editor/mesh/mesh_transfer.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* mesh_copy                                                           */
/* ------------------------------------------------------------------ */

bool mesh_copy(const mesh_slot_t *src, mesh_slot_t *dst) {
    if (!src || !dst) return false;

    mesh_slot_clear(dst);
    if (!mesh_slot_reserve_vertices(dst, src->vertex_count)) return false;
    if (!mesh_slot_reserve_indices(dst, src->index_count)) return false;

    /* Copy vertex attribute buffers */
    uint32_t vc = src->vertex_count;
    memcpy(dst->positions, src->positions, vc * 3 * sizeof(float));
    memcpy(dst->normals, src->normals, vc * 3 * sizeof(float));
    memcpy(dst->tangents, src->tangents, vc * 4 * sizeof(float));
    memcpy(dst->colors, src->colors, vc * 4 * sizeof(float));
    for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
        if (dst->uvs[ch] && src->uvs[ch]) {
            memcpy(dst->uvs[ch], src->uvs[ch], vc * 2 * sizeof(float));
        }
    }

    /* Copy index buffer */
    uint32_t ic = src->index_count;
    memcpy(dst->indices, src->indices, ic * sizeof(uint32_t));

    /* Copy polygroup IDs */
    uint32_t fc = ic / 3;
    if (dst->polygroup_ids && src->polygroup_ids) {
        memcpy(dst->polygroup_ids, src->polygroup_ids, fc * sizeof(uint16_t));
    }

    dst->vertex_count = vc;
    dst->index_count = ic;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_paste                                                          */
/* ------------------------------------------------------------------ */

bool mesh_paste(const mesh_slot_t *src, mesh_slot_t *dst) {
    if (!src || !dst) return false;

    uint32_t v_offset = dst->vertex_count;
    uint32_t src_vc = src->vertex_count;
    uint32_t src_ic = src->index_count;

    /* Reserve space */
    if (!mesh_slot_reserve_vertices(dst, v_offset + src_vc)) return false;
    if (!mesh_slot_reserve_indices(dst, dst->index_count + src_ic)) return false;

    /* Append vertices */
    for (uint32_t v = 0; v < src_vc; v++) {
        uint32_t nv = mesh_slot_add_vertex(dst,
            &src->positions[v*3], &src->normals[v*3]);
        if (nv == UINT32_MAX) return false;

        for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
            if (dst->uvs[ch] && src->uvs[ch]) {
                dst->uvs[ch][nv*2+0] = src->uvs[ch][v*2+0];
                dst->uvs[ch][nv*2+1] = src->uvs[ch][v*2+1];
            }
        }
    }

    /* Append triangles with offset indices */
    uint32_t src_fc = src_ic / 3;
    for (uint32_t f = 0; f < src_fc; f++) {
        uint32_t i0 = src->indices[f*3+0] + v_offset;
        uint32_t i1 = src->indices[f*3+1] + v_offset;
        uint32_t i2 = src->indices[f*3+2] + v_offset;
        uint16_t pg = src->polygroup_ids ? src->polygroup_ids[f] : 0;
        mesh_slot_add_triangle(dst, i0, i1, i2, pg);
    }

    return true;
}

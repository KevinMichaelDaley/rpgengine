/**
 * @file mesh_normals.c
 * @brief Flip normals and recalculate normals.
 *
 * Non-static functions (2 of 4): mesh_flip_normals, mesh_recalculate_normals.
 */
#include "ferrum/editor/mesh/mesh_normals.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Public: mesh_flip_normals                                           */
/* ------------------------------------------------------------------ */

bool mesh_flip_normals(mesh_slot_t *slot, const mesh_sel_bitset_t *sel) {
    if (!slot || !sel) return false;

    uint32_t face_count = slot->index_count / 3;
    for (uint32_t f = 0; f < face_count; f++) {
        if (!mesh_sel_bitset_test(sel, f)) continue;

        /* Swap indices[1] and indices[2] to reverse winding */
        uint32_t *tri = &slot->indices[f * 3];
        uint32_t tmp = tri[1];
        tri[1] = tri[2];
        tri[2] = tmp;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_recalculate_normals                                    */
/* ------------------------------------------------------------------ */

bool mesh_recalculate_normals(mesh_slot_t *slot) {
    if (!slot) return false;
    if (slot->vertex_count == 0) return false;

    /* Zero all normals */
    memset(slot->normals, 0, slot->vertex_count * 3 * sizeof(float));

    uint32_t face_count = slot->index_count / 3;

    /* Accumulate face normals (area-weighted) to each vertex */
    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t i0 = slot->indices[f*3+0];
        uint32_t i1 = slot->indices[f*3+1];
        uint32_t i2 = slot->indices[f*3+2];

        const float *a = &slot->positions[i0*3];
        const float *b = &slot->positions[i1*3];
        const float *c = &slot->positions[i2*3];

        float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
        float ac[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };

        /* Cross product (not normalized — magnitude = 2 * area) */
        float nx = ab[1]*ac[2] - ab[2]*ac[1];
        float ny = ab[2]*ac[0] - ab[0]*ac[2];
        float nz = ab[0]*ac[1] - ab[1]*ac[0];

        /* Add to each vertex of the face */
        slot->normals[i0*3+0] += nx; slot->normals[i0*3+1] += ny; slot->normals[i0*3+2] += nz;
        slot->normals[i1*3+0] += nx; slot->normals[i1*3+1] += ny; slot->normals[i1*3+2] += nz;
        slot->normals[i2*3+0] += nx; slot->normals[i2*3+1] += ny; slot->normals[i2*3+2] += nz;
    }

    /* Normalize each vertex normal */
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        float *n = &slot->normals[v*3];
        float len = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-12f) {
            n[0] /= len; n[1] /= len; n[2] /= len;
        }
    }

    return true;
}

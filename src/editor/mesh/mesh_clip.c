/**
 * @file mesh_clip.c
 * @brief Clip mesh by plane — vertex classification + triangle splitting.
 *
 * Non-static functions (1 of 4): mesh_clip.
 *
 * Algorithm:
 * 1. Classify each vertex as FRONT (+), BACK (-), or ON (0) vs plane.
 * 2. Keep triangles fully on the kept side.
 * 3. Split straddling triangles: compute edge-plane intersection,
 *    create new vertices, output sub-triangles on the kept side.
 * 4. Discard triangles fully on the discarded side.
 */
#include "ferrum/editor/mesh/mesh_clip.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CLIP_EPS 1e-6f

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

/** Signed distance from point to plane. */
static float plane_dist_(const float *pt, const float *plane_pt,
                          const float *plane_nrm) {
    return (pt[0]-plane_pt[0])*plane_nrm[0]
         + (pt[1]-plane_pt[1])*plane_nrm[1]
         + (pt[2]-plane_pt[2])*plane_nrm[2];
}

/** Create interpolated vertex at edge-plane intersection. */
static uint32_t intersect_edge_(mesh_slot_t *slot, uint32_t va, uint32_t vb,
                                 float da, float db) {
    float t = da / (da - db);
    float pos[3], nrm[3];
    for (int k = 0; k < 3; k++) {
        pos[k] = slot->positions[va*3+k] + t*(slot->positions[vb*3+k] - slot->positions[va*3+k]);
        nrm[k] = slot->normals[va*3+k] + t*(slot->normals[vb*3+k] - slot->normals[va*3+k]);
    }
    float len = sqrtf(nrm[0]*nrm[0]+nrm[1]*nrm[1]+nrm[2]*nrm[2]);
    if (len > 1e-12f) { nrm[0]/=len; nrm[1]/=len; nrm[2]/=len; }

    uint32_t nv = mesh_slot_add_vertex(slot, pos, nrm);
    if (nv == UINT32_MAX) return UINT32_MAX;

    for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
        if (slot->uvs[ch]) {
            slot->uvs[ch][nv*2+0] = slot->uvs[ch][va*2+0] + t*(slot->uvs[ch][vb*2+0]-slot->uvs[ch][va*2+0]);
            slot->uvs[ch][nv*2+1] = slot->uvs[ch][va*2+1] + t*(slot->uvs[ch][vb*2+1]-slot->uvs[ch][va*2+1]);
        }
    }
    return nv;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_clip                                                   */
/* ------------------------------------------------------------------ */

bool mesh_clip(mesh_slot_t *slot, const float *plane_pt,
               const float *plane_nrm, mesh_clip_mode_t mode) {
    if (!slot || !plane_pt || !plane_nrm) return false;

    uint32_t face_count = slot->index_count / 3;
    if (face_count == 0) return true;

    /* Compute signed distance for each vertex */
    float *dist = malloc(slot->vertex_count * sizeof(float));
    if (!dist) return false;

    float sign = (mode == MESH_CLIP_FRONT) ? 1.0f : -1.0f;
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        dist[v] = sign * plane_dist_(&slot->positions[v*3], plane_pt, plane_nrm);
    }

    /* Snapshot original indices */
    uint32_t *orig_idx = malloc(face_count * 3 * sizeof(uint32_t));
    uint16_t *orig_pg = NULL;
    if (!orig_idx) { free(dist); return false; }
    memcpy(orig_idx, slot->indices, face_count * 3 * sizeof(uint32_t));

    if (slot->polygroup_ids) {
        orig_pg = malloc(face_count * sizeof(uint16_t));
        if (orig_pg) memcpy(orig_pg, slot->polygroup_ids, face_count * sizeof(uint16_t));
    }

    /* Reserve extra space for split triangles */
    mesh_slot_reserve_vertices(slot, slot->vertex_count + face_count * 2);
    mesh_slot_reserve_indices(slot, face_count * 3 * 3);

    /* Rebuild index buffer */
    slot->index_count = 0;

    for (uint32_t fi = 0; fi < face_count; fi++) {
        uint32_t v[3] = { orig_idx[fi*3+0], orig_idx[fi*3+1], orig_idx[fi*3+2] };
        float d[3] = { dist[v[0]], dist[v[1]], dist[v[2]] };
        uint16_t pg = orig_pg ? orig_pg[fi] : 0;

        /* Classify: count vertices on front side (dist >= -eps) */
        int front_count = 0;
        for (int j = 0; j < 3; j++) {
            if (d[j] >= -CLIP_EPS) front_count++;
        }

        if (front_count == 3) {
            /* All front — keep */
            mesh_slot_add_triangle(slot, v[0], v[1], v[2], pg);
        } else if (front_count == 0) {
            /* All back — discard */
            continue;
        } else {
            /* Straddling — split */
            /* Find the lone vertex (1 on one side, 2 on the other) */
            /* Rotate so that v[0] is the lone vertex */
            if (front_count == 1) {
                /* One vertex on front. Find it and rotate to position 0 */
                int lone = -1;
                for (int j = 0; j < 3; j++) {
                    if (d[j] >= -CLIP_EPS) { lone = j; break; }
                }
                if (lone == 1) {
                    uint32_t tv = v[0]; v[0] = v[1]; v[1] = v[2]; v[2] = tv;
                    float td = d[0]; d[0] = d[1]; d[1] = d[2]; d[2] = td;
                } else if (lone == 2) {
                    uint32_t tv = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = tv;
                    float td = d[2]; d[2] = d[1]; d[1] = d[0]; d[0] = td;
                }
                /* v[0] is front, v[1],v[2] are back.
                 * Intersection: v[0]→v[1] and v[0]→v[2]. */
                uint32_t i01 = intersect_edge_(slot, v[0], v[1], d[0], d[1]);
                uint32_t i02 = intersect_edge_(slot, v[0], v[2], d[0], d[2]);
                if (i01 == UINT32_MAX || i02 == UINT32_MAX) {
                    free(dist); free(orig_idx); free(orig_pg);
                    return false;
                }
                /* Keep triangle (v[0], i01, i02) */
                mesh_slot_add_triangle(slot, v[0], i01, i02, pg);
            } else {
                /* Two vertices on front (front_count == 2).
                 * Find the lone back vertex and rotate to position 0. */
                int lone = -1;
                for (int j = 0; j < 3; j++) {
                    if (d[j] < -CLIP_EPS) { lone = j; break; }
                }
                if (lone == 1) {
                    uint32_t tv = v[0]; v[0] = v[1]; v[1] = v[2]; v[2] = tv;
                    float td = d[0]; d[0] = d[1]; d[1] = d[2]; d[2] = td;
                } else if (lone == 2) {
                    uint32_t tv = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = tv;
                    float td = d[2]; d[2] = d[1]; d[1] = d[0]; d[0] = td;
                }
                /* v[0] is back, v[1],v[2] are front.
                 * Intersection: v[0]→v[1] and v[0]→v[2]. */
                uint32_t i01 = intersect_edge_(slot, v[0], v[1], d[0], d[1]);
                uint32_t i02 = intersect_edge_(slot, v[0], v[2], d[0], d[2]);
                if (i01 == UINT32_MAX || i02 == UINT32_MAX) {
                    free(dist); free(orig_idx); free(orig_pg);
                    return false;
                }
                /* Keep quad (v[1], v[2], i02, i01) → 2 triangles */
                mesh_slot_add_triangle(slot, v[1], v[2], i02, pg);
                mesh_slot_add_triangle(slot, v[1], i02, i01, pg);
            }
        }
    }

    free(dist);
    free(orig_idx);
    free(orig_pg);
    return true;
}

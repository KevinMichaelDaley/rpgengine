/**
 * @file mesh_csg_boolean.c
 * @brief CSG boolean operations: merge, subtract, intersect.
 *
 * Non-static functions (3 of 4): mesh_csg_merge, mesh_csg_subtract,
 *                                  mesh_csg_intersect.
 *
 * Uses face-centroid inside/outside classification via ray-triangle
 * intersection counting (parity test along +X axis).
 */
#include "ferrum/editor/mesh/mesh_csg.h"

#include <math.h>
#include <string.h>

#define CSG_EPS 1e-6f

/* ------------------------------------------------------------------ */
/* Static: ray-triangle intersection (Möller–Trumbore)                */
/* ------------------------------------------------------------------ */

static bool ray_tri_hit_(const float origin[3], const float dir[3],
                          const float v0[3], const float v1[3],
                          const float v2[3]) {
    float e1[3] = { v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2] };
    float e2[3] = { v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2] };

    float h[3] = {
        dir[1]*e2[2] - dir[2]*e2[1],
        dir[2]*e2[0] - dir[0]*e2[2],
        dir[0]*e2[1] - dir[1]*e2[0]
    };
    float a = e1[0]*h[0] + e1[1]*h[1] + e1[2]*h[2];
    if (fabsf(a) < CSG_EPS) return false;

    float f = 1.0f / a;
    float s[3] = { origin[0]-v0[0], origin[1]-v0[1], origin[2]-v0[2] };
    float u = f * (s[0]*h[0] + s[1]*h[1] + s[2]*h[2]);
    if (u < 0.0f || u > 1.0f) return false;

    float q[3] = {
        s[1]*e1[2] - s[2]*e1[1],
        s[2]*e1[0] - s[0]*e1[2],
        s[0]*e1[1] - s[1]*e1[0]
    };
    float v = f * (dir[0]*q[0] + dir[1]*q[1] + dir[2]*q[2]);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * (e2[0]*q[0] + e2[1]*q[1] + e2[2]*q[2]);
    return t > CSG_EPS;
}

/* ------------------------------------------------------------------ */
/* Static: test if point is inside a mesh (odd ray crossings = inside)*/
/* ------------------------------------------------------------------ */

static bool point_inside_(const float pt[3], const mesh_slot_t *mesh) {
    float dir[3] = { 1.0f, 0.0f, 0.0f }; /* +X ray */
    int crossings = 0;
    uint32_t fc = mesh->index_count / 3;

    for (uint32_t f = 0; f < fc; f++) {
        const float *v0 = &mesh->positions[mesh->indices[f*3+0]*3];
        const float *v1 = &mesh->positions[mesh->indices[f*3+1]*3];
        const float *v2 = &mesh->positions[mesh->indices[f*3+2]*3];
        if (ray_tri_hit_(pt, dir, v0, v1, v2)) crossings++;
    }
    return (crossings & 1) != 0;
}

/* ------------------------------------------------------------------ */
/* Static: face centroid                                               */
/* ------------------------------------------------------------------ */

static void face_centroid_(const mesh_slot_t *slot, uint32_t fi,
                            float out[3]) {
    const uint32_t *idx = &slot->indices[fi*3];
    for (int k = 0; k < 3; k++) {
        out[k] = (slot->positions[idx[0]*3+k]
                + slot->positions[idx[1]*3+k]
                + slot->positions[idx[2]*3+k]) / 3.0f;
    }
}

/* ------------------------------------------------------------------ */
/* Static: copy face from source to dest                               */
/* ------------------------------------------------------------------ */

static bool copy_face_(mesh_slot_t *dst, const mesh_slot_t *src,
                        uint32_t fi, bool flip) {
    const uint32_t *si = &src->indices[fi*3];
    uint32_t vi[3];

    for (int j = 0; j < 3; j++) {
        uint32_t sv = si[j];
        float nrm[3] = {
            src->normals[sv*3+0],
            src->normals[sv*3+1],
            src->normals[sv*3+2]
        };
        if (flip) { nrm[0]=-nrm[0]; nrm[1]=-nrm[1]; nrm[2]=-nrm[2]; }
        vi[j] = mesh_slot_add_vertex(dst, &src->positions[sv*3], nrm);
        if (vi[j] == UINT32_MAX) return false;

        /* Copy UVs */
        for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
            if (dst->uvs[ch] && src->uvs[ch]) {
                dst->uvs[ch][vi[j]*2+0] = src->uvs[ch][sv*2+0];
                dst->uvs[ch][vi[j]*2+1] = src->uvs[ch][sv*2+1];
            }
        }
    }

    if (flip) {
        return mesh_slot_add_triangle(dst, vi[0], vi[2], vi[1], 0);
    }
    return mesh_slot_add_triangle(dst, vi[0], vi[1], vi[2], 0);
}

/* ------------------------------------------------------------------ */
/* Public: mesh_csg_merge (union)                                      */
/* ------------------------------------------------------------------ */

bool mesh_csg_merge(const mesh_slot_t *a, const mesh_slot_t *b,
                    mesh_slot_t *result) {
    if (!a || !b || !result) return false;
    mesh_slot_clear(result);

    uint32_t fc_a = a->index_count / 3;
    uint32_t fc_b = b->index_count / 3;

    /* Keep faces of A that are outside B */
    for (uint32_t f = 0; f < fc_a; f++) {
        float c[3];
        face_centroid_(a, f, c);
        if (!point_inside_(c, b)) {
            copy_face_(result, a, f, false);
        }
    }

    /* Keep faces of B that are outside A */
    for (uint32_t f = 0; f < fc_b; f++) {
        float c[3];
        face_centroid_(b, f, c);
        if (!point_inside_(c, a)) {
            copy_face_(result, b, f, false);
        }
    }

    return result->index_count > 0;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_csg_subtract (A minus B)                               */
/* ------------------------------------------------------------------ */

bool mesh_csg_subtract(const mesh_slot_t *target, const mesh_slot_t *cutter,
                       mesh_slot_t *result) {
    if (!target || !cutter || !result) return false;
    mesh_slot_clear(result);

    uint32_t fc_t = target->index_count / 3;
    uint32_t fc_c = cutter->index_count / 3;

    /* Keep faces of target that are outside cutter */
    for (uint32_t f = 0; f < fc_t; f++) {
        float c[3];
        face_centroid_(target, f, c);
        if (!point_inside_(c, cutter)) {
            copy_face_(result, target, f, false);
        }
    }

    /* Keep faces of cutter that are inside target (flipped) */
    for (uint32_t f = 0; f < fc_c; f++) {
        float c[3];
        face_centroid_(cutter, f, c);
        if (point_inside_(c, target)) {
            copy_face_(result, cutter, f, true);
        }
    }

    return result->index_count > 0;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_csg_intersect                                          */
/* ------------------------------------------------------------------ */

bool mesh_csg_intersect(const mesh_slot_t *a, const mesh_slot_t *b,
                        mesh_slot_t *result) {
    if (!a || !b || !result) return false;
    mesh_slot_clear(result);

    uint32_t fc_a = a->index_count / 3;
    uint32_t fc_b = b->index_count / 3;

    /* Keep faces of A that are inside B */
    for (uint32_t f = 0; f < fc_a; f++) {
        float c[3];
        face_centroid_(a, f, c);
        if (point_inside_(c, b)) {
            copy_face_(result, a, f, false);
        }
    }

    /* Keep faces of B that are inside A */
    for (uint32_t f = 0; f < fc_b; f++) {
        float c[3];
        face_centroid_(b, f, c);
        if (point_inside_(c, a)) {
            copy_face_(result, b, f, false);
        }
    }

    return result->index_count > 0;
}

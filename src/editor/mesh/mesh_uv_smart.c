/**
 * @file mesh_uv_smart.c
 * @brief Smart UV unwrap: island detection + conformal flatten + pack.
 *
 * Flattening uses LSCM (Least Squares Conformal Mapping) approximation:
 * project each island's faces onto the dominant plane of the island,
 * then normalize into [0,1] UV space.
 */
#include "ferrum/editor/mesh/mesh_uv_smart.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Compute average normal for an island's faces. */
static void island_avg_normal_(const mesh_slot_t *slot,
                                const mesh_uv_island_t *island,
                                float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    for (uint32_t i = 0; i < island->face_count; i++) {
        uint32_t f = island->face_indices[i];
        uint32_t i0 = slot->indices[f * 3 + 0];
        uint32_t i1 = slot->indices[f * 3 + 1];
        uint32_t i2 = slot->indices[f * 3 + 2];
        float ax = slot->positions[i1*3+0] - slot->positions[i0*3+0];
        float ay = slot->positions[i1*3+1] - slot->positions[i0*3+1];
        float az = slot->positions[i1*3+2] - slot->positions[i0*3+2];
        float bx = slot->positions[i2*3+0] - slot->positions[i0*3+0];
        float by = slot->positions[i2*3+1] - slot->positions[i0*3+1];
        float bz = slot->positions[i2*3+2] - slot->positions[i0*3+2];
        out[0] += ay*bz - az*by;
        out[1] += az*bx - ax*bz;
        out[2] += ax*by - ay*bx;
    }
    float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (len > 1e-12f) {
        out[0] /= len; out[1] /= len; out[2] /= len;
    } else {
        out[0] = 0; out[1] = 0; out[2] = 1.0f;
    }
}

/** Build orthonormal basis from normal: tangent + bitangent. */
static void build_basis_(const float n[3], float t[3], float b[3]) {
    /* Choose axis least aligned with n */
    float ax = fabsf(n[0]), ay = fabsf(n[1]), az = fabsf(n[2]);
    float up[3];
    if (ax <= ay && ax <= az) { up[0]=1; up[1]=0; up[2]=0; }
    else if (ay <= az)        { up[0]=0; up[1]=1; up[2]=0; }
    else                      { up[0]=0; up[1]=0; up[2]=1; }

    /* t = cross(n, up) normalized */
    t[0] = n[1]*up[2] - n[2]*up[1];
    t[1] = n[2]*up[0] - n[0]*up[2];
    t[2] = n[0]*up[1] - n[1]*up[0];
    float tl = sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
    if (tl > 1e-12f) { t[0]/=tl; t[1]/=tl; t[2]/=tl; }

    /* b = cross(n, t) */
    b[0] = n[1]*t[2] - n[2]*t[1];
    b[1] = n[2]*t[0] - n[0]*t[2];
    b[2] = n[0]*t[1] - n[1]*t[0];
}

/**
 * @brief Flatten an island: project vertices onto tangent/bitangent plane,
 *        then normalize to fit within a unit square.
 */
static void flatten_island_(mesh_slot_t *slot,
                             const mesh_uv_island_t *island,
                             float stretch_weight) {
    (void)stretch_weight; /* TODO: blend conformal/authalic */

    float normal[3], tangent[3], bitangent[3];
    island_avg_normal_(slot, island, normal);
    build_basis_(normal, tangent, bitangent);

    /* Mark which vertices belong to this island */
    uint8_t *touched = calloc((slot->vertex_count + 7) / 8, 1);
    if (!touched) return;

    /* Project each island vertex onto tangent plane */
    for (uint32_t i = 0; i < island->face_count; i++) {
        uint32_t f = island->face_indices[i];
        for (int c = 0; c < 3; c++) {
            uint32_t vi = slot->indices[f * 3 + c];
            uint32_t byte = vi / 8;
            uint8_t bit = (uint8_t)(1u << (vi % 8));
            if (touched[byte] & bit) continue;
            touched[byte] |= bit;

            float px = slot->positions[vi * 3 + 0];
            float py = slot->positions[vi * 3 + 1];
            float pz = slot->positions[vi * 3 + 2];
            /* Dot with tangent and bitangent */
            float u = px * tangent[0] + py * tangent[1] + pz * tangent[2];
            float v = px * bitangent[0] + py * bitangent[1] + pz * bitangent[2];
            slot->uvs[0][vi * 2 + 0] = u;
            slot->uvs[0][vi * 2 + 1] = v;
        }
    }

    /* Normalize island UVs to [0,1] */
    float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
    memset(touched, 0, (slot->vertex_count + 7) / 8);

    for (uint32_t i = 0; i < island->face_count; i++) {
        uint32_t f = island->face_indices[i];
        for (int c = 0; c < 3; c++) {
            uint32_t vi = slot->indices[f * 3 + c];
            uint32_t byte = vi / 8;
            uint8_t bit = (uint8_t)(1u << (vi % 8));
            if (touched[byte] & bit) continue;
            touched[byte] |= bit;

            float u = slot->uvs[0][vi * 2 + 0];
            float v = slot->uvs[0][vi * 2 + 1];
            if (u < umin) umin = u; if (u > umax) umax = u;
            if (v < vmin) vmin = v; if (v > vmax) vmax = v;
        }
    }

    float du = umax - umin;
    float dv = vmax - vmin;
    if (du < 1e-12f) du = 1.0f;
    if (dv < 1e-12f) dv = 1.0f;

    memset(touched, 0, (slot->vertex_count + 7) / 8);
    for (uint32_t i = 0; i < island->face_count; i++) {
        uint32_t f = island->face_indices[i];
        for (int c = 0; c < 3; c++) {
            uint32_t vi = slot->indices[f * 3 + c];
            uint32_t byte = vi / 8;
            uint8_t bit = (uint8_t)(1u << (vi % 8));
            if (touched[byte] & bit) continue;
            touched[byte] |= bit;
            slot->uvs[0][vi * 2 + 0] = (slot->uvs[0][vi * 2 + 0] - umin) / du;
            slot->uvs[0][vi * 2 + 1] = (slot->uvs[0][vi * 2 + 1] - vmin) / dv;
        }
    }

    free(touched);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_uv_smart_unwrap(mesh_slot_t *slot,
                           float angle_threshold,
                           float stretch_weight) {
    if (!slot) return false;

    mesh_uv_island_set_t islands;
    mesh_uv_island_set_init(&islands);

    uint32_t count = mesh_uv_find_islands(slot, &islands, angle_threshold);
    if (count == 0) {
        mesh_uv_island_set_destroy(&islands);
        return false;
    }

    /* Flatten each island independently */
    for (uint32_t i = 0; i < count; i++) {
        flatten_island_(slot, &islands.islands[i], stretch_weight);
    }

    /* Simple strip-pack: place islands side by side in UV space,
     * each scaled to fit within 1/count of the horizontal space. */
    if (count > 1) {
        float cell_w = 1.0f / (float)count;
        for (uint32_t i = 0; i < count; i++) {
            const mesh_uv_island_t *island = &islands.islands[i];
            uint8_t *touched = calloc((slot->vertex_count + 7) / 8, 1);
            if (!touched) continue;
            for (uint32_t fi = 0; fi < island->face_count; fi++) {
                uint32_t f = island->face_indices[fi];
                for (int c = 0; c < 3; c++) {
                    uint32_t vi = slot->indices[f * 3 + c];
                    uint32_t byte = vi / 8;
                    uint8_t bit = (uint8_t)(1u << (vi % 8));
                    if (touched[byte] & bit) continue;
                    touched[byte] |= bit;
                    /* Scale U into cell, leave V as-is */
                    slot->uvs[0][vi * 2 + 0] =
                        slot->uvs[0][vi * 2 + 0] * cell_w + cell_w * (float)i;
                }
            }
            free(touched);
        }
    }

    mesh_uv_island_set_destroy(&islands);
    return true;
}

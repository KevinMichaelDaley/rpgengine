/**
 * @file mesh_select_topo.c
 * @brief Topological face selection: flood fill and select similar.
 *
 * Non-static functions: mesh_select_flood, mesh_select_similar_normal (2 of 4).
 */
#include "ferrum/editor/mesh/mesh_select.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Internal: face adjacency (shared edge = 2 common vertices)          */
/* ------------------------------------------------------------------ */

/**
 * @brief Check if two faces share an edge (2 common vertices).
 */
static bool faces_share_edge_(const mesh_slot_t *slot, uint32_t fa, uint32_t fb) {
    const uint32_t *ia = &slot->indices[fa * 3];
    const uint32_t *ib = &slot->indices[fb * 3];
    int shared = 0;
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            if (ia[a] == ib[b]) { shared++; }
        }
    }
    return shared >= 2;
}

/**
 * @brief Compute face normal from vertex positions (cross product).
 */
static void face_normal_(const mesh_slot_t *slot, uint32_t face,
                          float out[3]) {
    uint32_t base = face * 3;
    const float *p0 = &slot->positions[slot->indices[base + 0] * 3];
    const float *p1 = &slot->positions[slot->indices[base + 1] * 3];
    const float *p2 = &slot->positions[slot->indices[base + 2] * 3];

    float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
    float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};

    out[0] = e1[1]*e2[2] - e1[2]*e2[1];
    out[1] = e1[2]*e2[0] - e1[0]*e2[2];
    out[2] = e1[0]*e2[1] - e1[1]*e2[0];

    float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (len > 1e-8f) {
        out[0] /= len;
        out[1] /= len;
        out[2] /= len;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void mesh_select_flood(const mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                       uint32_t seed_face) {
    if (!slot || !sel) { return; }
    uint32_t fc = mesh_slot_face_count(slot);
    if (seed_face >= fc) { return; }

    /* BFS using a simple queue (fixed-size array since fc ≤ 65536) */
    uint32_t *queue = malloc((size_t)fc * sizeof(uint32_t));
    if (!queue) { return; }

    /* Visited bitset (separate from selection — we add to existing sel) */
    uint8_t *visited = calloc((fc + 7) / 8, 1);
    if (!visited) { free(queue); return; }

    uint32_t head = 0, tail = 0;
    queue[tail++] = seed_face;
    visited[seed_face / 8] |= (uint8_t)(1u << (seed_face % 8));

    while (head < tail) {
        uint32_t f = queue[head++];
        mesh_sel_bitset_set(sel, f);

        /* Find adjacent faces */
        for (uint32_t other = 0; other < fc; other++) {
            if (visited[other / 8] & (1u << (other % 8))) { continue; }
            if (faces_share_edge_(slot, f, other)) {
                visited[other / 8] |= (uint8_t)(1u << (other % 8));
                queue[tail++] = other;
            }
        }
    }

    free(queue);
    free(visited);
}

void mesh_select_similar_normal(const mesh_slot_t *slot,
                                mesh_sel_bitset_t *sel,
                                const float normal[3],
                                float threshold) {
    if (!slot || !sel || !normal) { return; }

    uint32_t fc = mesh_slot_face_count(slot);
    float cos_thresh = cosf(threshold * (float)M_PI / 180.0f);

    for (uint32_t f = 0; f < fc; f++) {
        float fn[3];
        face_normal_(slot, f, fn);

        float dot = fn[0]*normal[0] + fn[1]*normal[1] + fn[2]*normal[2];
        if (dot >= cos_thresh) {
            mesh_sel_bitset_set(sel, f);
        }
    }
}

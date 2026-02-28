/**
 * @file mesh_subdiv_linear.c
 * @brief Linear (midpoint) subdivision — no smoothing.
 *
 * Non-static functions (1 of 4): mesh_subdivide_linear.
 *
 * Algorithm: For each triangle (a, b, c):
 *   1. Find or create midpoint vertices: m_ab, m_bc, m_ca.
 *   2. Replace original triangle with 4 sub-triangles:
 *      (a, m_ab, m_ca), (m_ab, b, m_bc), (m_ca, m_bc, c), (m_ab, m_bc, m_ca).
 *
 * Edge midpoints are de-duplicated via a hash map keyed by canonical edge.
 */
#include "ferrum/editor/mesh/mesh_subdivide.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Edge-to-midpoint hash map                                           */
/* ------------------------------------------------------------------ */

/** Hash map entry for edge → midpoint vertex index. */
typedef struct {
    uint32_t v0, v1;    /* canonical edge (v0 < v1) */
    uint32_t mid;       /* midpoint vertex index */
    bool     occupied;  /* slot in use */
} edge_map_entry_t;

/** Simple open-addressing hash map. */
typedef struct {
    edge_map_entry_t *entries;
    uint32_t capacity;
} edge_map_t;

static bool edge_map_init_(edge_map_t *map, uint32_t cap) {
    map->capacity = cap;
    map->entries = calloc(cap, sizeof(edge_map_entry_t));
    return map->entries != NULL;
}

static void edge_map_destroy_(edge_map_t *map) {
    free(map->entries);
    map->entries = NULL;
}

static uint32_t edge_hash_(uint32_t v0, uint32_t v1, uint32_t cap) {
    uint64_t h = ((uint64_t)v0 * 2654435761ULL) ^ ((uint64_t)v1 * 40503ULL);
    return (uint32_t)(h % cap);
}

/**
 * Get or create midpoint vertex for edge (a, b).
 * Returns midpoint vertex index, or UINT32_MAX on error.
 */
static uint32_t get_or_create_midpoint_(edge_map_t *map, mesh_slot_t *slot,
                                         uint32_t a, uint32_t b) {
    /* Canonicalize */
    uint32_t v0 = a < b ? a : b;
    uint32_t v1 = a < b ? b : a;

    uint32_t idx = edge_hash_(v0, v1, map->capacity);
    for (uint32_t probe = 0; probe < map->capacity; probe++) {
        uint32_t i = (idx + probe) % map->capacity;
        edge_map_entry_t *e = &map->entries[i];

        if (!e->occupied) {
            /* Create midpoint */
            float pos[3], nrm[3];
            for (int k = 0; k < 3; k++) {
                pos[k] = (slot->positions[v0*3+k] + slot->positions[v1*3+k]) * 0.5f;
                nrm[k] = (slot->normals[v0*3+k] + slot->normals[v1*3+k]) * 0.5f;
            }
            float len = sqrtf(nrm[0]*nrm[0]+nrm[1]*nrm[1]+nrm[2]*nrm[2]);
            if (len > 1e-12f) { nrm[0]/=len; nrm[1]/=len; nrm[2]/=len; }

            uint32_t mv = mesh_slot_add_vertex(slot, pos, nrm);
            if (mv == UINT32_MAX) return UINT32_MAX;

            /* Lerp UVs */
            for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
                if (slot->uvs[ch]) {
                    slot->uvs[ch][mv*2+0] = (slot->uvs[ch][v0*2+0]+slot->uvs[ch][v1*2+0])*0.5f;
                    slot->uvs[ch][mv*2+1] = (slot->uvs[ch][v0*2+1]+slot->uvs[ch][v1*2+1])*0.5f;
                }
            }
            if (slot->colors) {
                for (int k = 0; k < 4; k++) {
                    slot->colors[mv*4+k] = (slot->colors[v0*4+k]+slot->colors[v1*4+k])*0.5f;
                }
            }

            e->v0 = v0; e->v1 = v1; e->mid = mv; e->occupied = true;
            return mv;
        }
        if (e->v0 == v0 && e->v1 == v1) {
            return e->mid; /* already created */
        }
    }
    return UINT32_MAX; /* map full (shouldn't happen with 4x capacity) */
}

/* ------------------------------------------------------------------ */
/* Single level of linear subdivision                                  */
/* ------------------------------------------------------------------ */

static bool subdivide_linear_one_(mesh_slot_t *slot) {
    uint32_t face_count = slot->index_count / 3;
    if (face_count == 0) return false;

    /* Edge count ≤ 3*F/2 + 3. Hash map capacity = 4x for load factor. */
    uint32_t est_edges = face_count * 2 + 4;
    edge_map_t map;
    if (!edge_map_init_(&map, est_edges * 4)) return false;

    /* Pre-reserve: 4x faces, +3E new vertices (at most) */
    mesh_slot_reserve_vertices(slot, slot->vertex_count + face_count * 3);
    mesh_slot_reserve_indices(slot, face_count * 4 * 3);

    /* Snapshot original index data (we overwrite slot->indices in-place) */
    uint32_t *orig_idx = malloc(face_count * 3 * sizeof(uint32_t));
    if (!orig_idx) { edge_map_destroy_(&map); return false; }
    memcpy(orig_idx, slot->indices, face_count * 3 * sizeof(uint32_t));

    uint16_t *orig_pg = NULL;
    if (slot->polygroup_ids) {
        orig_pg = malloc(face_count * sizeof(uint16_t));
        if (orig_pg) memcpy(orig_pg, slot->polygroup_ids, face_count * sizeof(uint16_t));
    }

    /* Reset index buffer — we'll rebuild it */
    slot->index_count = 0;

    for (uint32_t fi = 0; fi < face_count; fi++) {
        uint32_t a = orig_idx[fi*3+0];
        uint32_t b = orig_idx[fi*3+1];
        uint32_t c = orig_idx[fi*3+2];

        uint32_t mab = get_or_create_midpoint_(&map, slot, a, b);
        uint32_t mbc = get_or_create_midpoint_(&map, slot, b, c);
        uint32_t mca = get_or_create_midpoint_(&map, slot, c, a);

        if (mab == UINT32_MAX || mbc == UINT32_MAX || mca == UINT32_MAX) {
            free(orig_idx); free(orig_pg); edge_map_destroy_(&map);
            return false;
        }

        uint16_t pg = orig_pg ? orig_pg[fi] : 0;
        mesh_slot_add_triangle(slot, a,   mab, mca, pg);
        mesh_slot_add_triangle(slot, mab, b,   mbc, pg);
        mesh_slot_add_triangle(slot, mca, mbc, c,   pg);
        mesh_slot_add_triangle(slot, mab, mbc, mca, pg);
    }

    free(orig_idx);
    free(orig_pg);
    edge_map_destroy_(&map);
    return true;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_subdivide_linear                                       */
/* ------------------------------------------------------------------ */

bool mesh_subdivide_linear(mesh_slot_t *slot, uint32_t levels) {
    if (!slot || levels == 0) return false;
    if (slot->index_count == 0) return false;

    for (uint32_t lv = 0; lv < levels; lv++) {
        if (!subdivide_linear_one_(slot)) return false;
    }
    return true;
}

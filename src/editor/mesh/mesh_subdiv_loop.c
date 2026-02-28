/**
 * @file mesh_subdiv_loop.c
 * @brief Loop subdivision — smoothing subdivision for triangle meshes.
 *
 * Non-static functions (1 of 4): mesh_subdivide_loop.
 *
 * Algorithm per level:
 * 1. Compute new "odd" vertices at each edge midpoint using Loop weights:
 *    - Interior edge: 3/8*(v0+v1) + 1/8*(opp0+opp1)
 *    - Boundary edge: 1/2*(v0+v1)
 * 2. Compute new "even" vertex positions for original vertices:
 *    - beta = (n > 3) ? 3/(8*n) : 3/16  where n = valence
 *    - new_pos = (1 - n*beta)*pos + beta * sum(neighbors)
 * 3. Split each triangle into 4 using the odd vertices.
 */
#include "ferrum/editor/mesh/mesh_subdivide.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Edge map (same as linear, but stores opposite vertices)             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t v0, v1;
    uint32_t mid;
    uint32_t opp0, opp1;  /* opposite vertices of adjacent faces */
    uint32_t adj_count;   /* 1 = boundary, 2 = interior */
    bool occupied;
} loop_edge_entry_t;

typedef struct {
    loop_edge_entry_t *entries;
    uint32_t capacity;
} loop_edge_map_t;

static bool lmap_init_(loop_edge_map_t *m, uint32_t cap) {
    m->capacity = cap;
    m->entries = calloc(cap, sizeof(loop_edge_entry_t));
    return m->entries != NULL;
}

static void lmap_destroy_(loop_edge_map_t *m) { free(m->entries); }

static uint32_t lmap_hash_(uint32_t v0, uint32_t v1, uint32_t cap) {
    uint64_t h = ((uint64_t)v0 * 2654435761ULL) ^ ((uint64_t)v1 * 40503ULL);
    return (uint32_t)(h % cap);
}

/**
 * Register an edge with its opposite vertex.
 * Returns pointer to entry, or NULL if map full.
 */
static loop_edge_entry_t *lmap_register_(loop_edge_map_t *m,
                                          uint32_t a, uint32_t b,
                                          uint32_t opp) {
    uint32_t v0 = a < b ? a : b;
    uint32_t v1 = a < b ? b : a;
    uint32_t idx = lmap_hash_(v0, v1, m->capacity);

    for (uint32_t probe = 0; probe < m->capacity; probe++) {
        uint32_t i = (idx + probe) % m->capacity;
        loop_edge_entry_t *e = &m->entries[i];

        if (!e->occupied) {
            e->v0 = v0; e->v1 = v1;
            e->opp0 = opp; e->adj_count = 1;
            e->mid = UINT32_MAX;
            e->occupied = true;
            return e;
        }
        if (e->v0 == v0 && e->v1 == v1) {
            if (e->adj_count == 1) { e->opp1 = opp; e->adj_count = 2; }
            return e;
        }
    }
    return NULL;
}

static loop_edge_entry_t *lmap_find_(loop_edge_map_t *m, uint32_t a, uint32_t b) {
    uint32_t v0 = a < b ? a : b;
    uint32_t v1 = a < b ? b : a;
    uint32_t idx = lmap_hash_(v0, v1, m->capacity);

    for (uint32_t probe = 0; probe < m->capacity; probe++) {
        uint32_t i = (idx + probe) % m->capacity;
        loop_edge_entry_t *e = &m->entries[i];
        if (!e->occupied) return NULL;
        if (e->v0 == v0 && e->v1 == v1) return e;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Single level of Loop subdivision                                    */
/* ------------------------------------------------------------------ */

static bool subdivide_loop_one_(mesh_slot_t *slot) {
    uint32_t fc = slot->index_count / 3;
    if (fc == 0) return false;
    uint32_t vc = slot->vertex_count;

    /* Build edge map with opposites */
    uint32_t est_edges = fc * 2 + 4;
    loop_edge_map_t map;
    if (!lmap_init_(&map, est_edges * 4)) return false;

    uint32_t *orig_idx = malloc(fc * 3 * sizeof(uint32_t));
    if (!orig_idx) { lmap_destroy_(&map); return false; }
    memcpy(orig_idx, slot->indices, fc * 3 * sizeof(uint32_t));

    uint16_t *orig_pg = NULL;
    if (slot->polygroup_ids) {
        orig_pg = malloc(fc * sizeof(uint16_t));
        if (orig_pg) memcpy(orig_pg, slot->polygroup_ids, fc * sizeof(uint16_t));
    }

    /* Register all edges with opposite vertices */
    for (uint32_t fi = 0; fi < fc; fi++) {
        uint32_t a = orig_idx[fi*3+0];
        uint32_t b = orig_idx[fi*3+1];
        uint32_t c = orig_idx[fi*3+2];
        lmap_register_(&map, a, b, c);
        lmap_register_(&map, b, c, a);
        lmap_register_(&map, c, a, b);
    }

    /* Compute valence and neighbor sum for even vertex repositioning */
    uint32_t *valence = calloc(vc, sizeof(uint32_t));
    float *neighbor_sum = calloc(vc * 3, sizeof(float));
    if (!valence || !neighbor_sum) {
        free(valence); free(neighbor_sum); free(orig_idx); free(orig_pg);
        lmap_destroy_(&map); return false;
    }

    for (uint32_t fi = 0; fi < fc; fi++) {
        uint32_t tri[3] = { orig_idx[fi*3+0], orig_idx[fi*3+1], orig_idx[fi*3+2] };
        for (int j = 0; j < 3; j++) {
            uint32_t v = tri[j];
            uint32_t vn = tri[(j+1) % 3];
            /* Check if already counted — simple approach, count all edges */
            valence[v]++;
            neighbor_sum[v*3+0] += slot->positions[vn*3+0];
            neighbor_sum[v*3+1] += slot->positions[vn*3+1];
            neighbor_sum[v*3+2] += slot->positions[vn*3+2];
        }
    }

    /* Reserve space */
    mesh_slot_reserve_vertices(slot, vc + est_edges + 16);
    mesh_slot_reserve_indices(slot, fc * 4 * 3);

    /* Create odd vertices (edge midpoints with Loop weights) */
    for (uint32_t i = 0; i < map.capacity; i++) {
        loop_edge_entry_t *e = &map.entries[i];
        if (!e->occupied) continue;

        float pos[3], nrm[3];
        if (e->adj_count == 2) {
            /* Interior edge: 3/8*(v0+v1) + 1/8*(opp0+opp1) */
            for (int k = 0; k < 3; k++) {
                pos[k] = 0.375f * (slot->positions[e->v0*3+k] + slot->positions[e->v1*3+k])
                       + 0.125f * (slot->positions[e->opp0*3+k] + slot->positions[e->opp1*3+k]);
                nrm[k] = 0.375f * (slot->normals[e->v0*3+k] + slot->normals[e->v1*3+k])
                        + 0.125f * (slot->normals[e->opp0*3+k] + slot->normals[e->opp1*3+k]);
            }
        } else {
            /* Boundary edge: simple midpoint */
            for (int k = 0; k < 3; k++) {
                pos[k] = 0.5f * (slot->positions[e->v0*3+k] + slot->positions[e->v1*3+k]);
                nrm[k] = 0.5f * (slot->normals[e->v0*3+k] + slot->normals[e->v1*3+k]);
            }
        }
        float len = sqrtf(nrm[0]*nrm[0]+nrm[1]*nrm[1]+nrm[2]*nrm[2]);
        if (len > 1e-12f) { nrm[0]/=len; nrm[1]/=len; nrm[2]/=len; }

        uint32_t mv = mesh_slot_add_vertex(slot, pos, nrm);
        if (mv == UINT32_MAX) {
            free(valence); free(neighbor_sum); free(orig_idx); free(orig_pg);
            lmap_destroy_(&map); return false;
        }

        /* Lerp UVs */
        for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
            if (slot->uvs[ch]) {
                slot->uvs[ch][mv*2+0] = 0.5f*(slot->uvs[ch][e->v0*2+0]+slot->uvs[ch][e->v1*2+0]);
                slot->uvs[ch][mv*2+1] = 0.5f*(slot->uvs[ch][e->v0*2+1]+slot->uvs[ch][e->v1*2+1]);
            }
        }
        e->mid = mv;
    }

    /* Reposition even (original) vertices */
    for (uint32_t v = 0; v < vc; v++) {
        uint32_t n = valence[v];
        if (n < 2) continue;

        float beta;
        if (n == 3) {
            beta = 3.0f / 16.0f;
        } else {
            beta = 3.0f / (8.0f * (float)n);
        }

        for (int k = 0; k < 3; k++) {
            slot->positions[v*3+k] = (1.0f - (float)n * beta) * slot->positions[v*3+k]
                                   + beta * neighbor_sum[v*3+k];
        }
    }

    /* Rebuild index buffer */
    slot->index_count = 0;

    for (uint32_t fi = 0; fi < fc; fi++) {
        uint32_t a = orig_idx[fi*3+0];
        uint32_t b = orig_idx[fi*3+1];
        uint32_t c = orig_idx[fi*3+2];

        loop_edge_entry_t *eab = lmap_find_(&map, a, b);
        loop_edge_entry_t *ebc = lmap_find_(&map, b, c);
        loop_edge_entry_t *eca = lmap_find_(&map, c, a);

        if (!eab || !ebc || !eca) {
            free(valence); free(neighbor_sum); free(orig_idx); free(orig_pg);
            lmap_destroy_(&map); return false;
        }

        uint32_t mab = eab->mid;
        uint32_t mbc = ebc->mid;
        uint32_t mca = eca->mid;

        uint16_t pg = orig_pg ? orig_pg[fi] : 0;
        mesh_slot_add_triangle(slot, a,   mab, mca, pg);
        mesh_slot_add_triangle(slot, mab, b,   mbc, pg);
        mesh_slot_add_triangle(slot, mca, mbc, c,   pg);
        mesh_slot_add_triangle(slot, mab, mbc, mca, pg);
    }

    free(valence);
    free(neighbor_sum);
    free(orig_idx);
    free(orig_pg);
    lmap_destroy_(&map);
    return true;
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

bool mesh_subdivide_loop(mesh_slot_t *slot, uint32_t levels) {
    if (!slot || levels == 0) return false;
    if (slot->index_count == 0) return false;

    for (uint32_t lv = 0; lv < levels; lv++) {
        if (!subdivide_loop_one_(slot)) return false;
    }
    return true;
}

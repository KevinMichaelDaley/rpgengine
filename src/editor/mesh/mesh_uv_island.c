/**
 * @file mesh_uv_island.c
 * @brief UV island detection by splitting at sharp edges.
 *
 * Algorithm: build face adjacency via shared edges, then flood-fill
 * connected components where dihedral angle < threshold.
 */
#include "ferrum/editor/mesh/mesh_uv_smart.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Compute face normal for triangle (i0,i1,i2). */
static void face_normal_(const float *positions, uint32_t i0, uint32_t i1,
                          uint32_t i2, float out[3]) {
    float ax = positions[i1 * 3 + 0] - positions[i0 * 3 + 0];
    float ay = positions[i1 * 3 + 1] - positions[i0 * 3 + 1];
    float az = positions[i1 * 3 + 2] - positions[i0 * 3 + 2];
    float bx = positions[i2 * 3 + 0] - positions[i0 * 3 + 0];
    float by = positions[i2 * 3 + 1] - positions[i0 * 3 + 1];
    float bz = positions[i2 * 3 + 2] - positions[i0 * 3 + 2];
    out[0] = ay * bz - az * by;
    out[1] = az * bx - ax * bz;
    out[2] = ax * by - ay * bx;
    float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (len > 1e-12f) {
        out[0] /= len; out[1] /= len; out[2] /= len;
    }
}

/** Dihedral angle between two face normals. */
static float dihedral_angle_(const float n1[3], const float n2[3]) {
    float dot = n1[0]*n2[0] + n1[1]*n2[1] + n1[2]*n2[2];
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    return acosf(dot);
}

/**
 * Make a position-based edge key by quantizing vertex positions.
 * This handles split-vertex meshes where the same spatial edge
 * has different vertex indices on adjacent faces.
 */
static uint64_t pos_hash_(const float *positions, uint32_t vi) {
    /* Quantize to 1/1024 units to handle float imprecision */
    int32_t x = (int32_t)(positions[vi * 3 + 0] * 1024.0f);
    int32_t y = (int32_t)(positions[vi * 3 + 1] * 1024.0f);
    int32_t z = (int32_t)(positions[vi * 3 + 2] * 1024.0f);
    /* Pack into 64 bits: 21 bits each */
    uint64_t hx = (uint64_t)(x & 0x1FFFFF);
    uint64_t hy = (uint64_t)(y & 0x1FFFFF);
    uint64_t hz = (uint64_t)(z & 0x1FFFFF);
    return (hx << 42) | (hy << 21) | hz;
}

static uint64_t edge_key_(const float *positions, uint32_t a, uint32_t b) {
    uint64_t ha = pos_hash_(positions, a);
    uint64_t hb = pos_hash_(positions, b);
    /* Canonical order: lower hash first. XOR + sum for combined hash. */
    if (ha > hb) { uint64_t t = ha; ha = hb; hb = t; }
    /* Szudzik pairing */
    return ha * 2097169ull + hb;
}

/** Add face to island, growing if needed. */
static void island_add_face_(mesh_uv_island_t *island, uint32_t face) {
    if (island->face_count >= island->face_capacity) {
        uint32_t new_cap = island->face_capacity ? island->face_capacity * 2 : 8;
        uint32_t *new_arr = realloc(island->face_indices, new_cap * sizeof(uint32_t));
        if (!new_arr) return;
        island->face_indices = new_arr;
        island->face_capacity = new_cap;
    }
    island->face_indices[island->face_count++] = face;
}

/* ------------------------------------------------------------------ */
/* Simple hash map for edge→face adjacency                             */
/* ------------------------------------------------------------------ */

#define EDGE_MAP_SIZE 8192
#define EDGE_MAP_MASK (EDGE_MAP_SIZE - 1)

typedef struct edge_entry {
    uint64_t key;
    uint32_t face_a;
    uint32_t face_b;    /* UINT32_MAX if only one face */
    bool     occupied;
} edge_entry_t;

/** Find or insert edge in hash table. */
static edge_entry_t *edge_find_or_insert_(edge_entry_t *table,
                                           uint64_t key, uint32_t face) {
    uint32_t idx = (uint32_t)(key & EDGE_MAP_MASK);
    for (uint32_t probe = 0; probe < EDGE_MAP_SIZE; probe++) {
        uint32_t i = (idx + probe) & EDGE_MAP_MASK;
        if (!table[i].occupied) {
            table[i].key = key;
            table[i].face_a = face;
            table[i].face_b = UINT32_MAX;
            table[i].occupied = true;
            return &table[i];
        }
        if (table[i].key == key) {
            if (table[i].face_b == UINT32_MAX) {
                table[i].face_b = face;
            }
            return &table[i];
        }
    }
    return NULL; /* table full */
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void mesh_uv_island_set_init(mesh_uv_island_set_t *set) {
    if (!set) return;
    memset(set, 0, sizeof(*set));
}

void mesh_uv_island_set_destroy(mesh_uv_island_set_t *set) {
    if (!set) return;
    for (uint32_t i = 0; i < set->count; i++) {
        free(set->islands[i].face_indices);
    }
    memset(set, 0, sizeof(*set));
}

uint32_t mesh_uv_find_islands(const mesh_slot_t *slot,
                              mesh_uv_island_set_t *out_islands,
                              float angle_threshold) {
    if (!slot || !out_islands) return 0;

    uint32_t face_count = slot->index_count / 3;
    if (face_count == 0) return 0;

    /* Compute per-face normals */
    float *normals = malloc(face_count * 3 * sizeof(float));
    if (!normals) return 0;

    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t i0 = slot->indices[f * 3 + 0];
        uint32_t i1 = slot->indices[f * 3 + 1];
        uint32_t i2 = slot->indices[f * 3 + 2];
        face_normal_(slot->positions, i0, i1, i2, &normals[f * 3]);
    }

    /* Build edge→face adjacency */
    edge_entry_t *edge_table = calloc(EDGE_MAP_SIZE, sizeof(edge_entry_t));
    if (!edge_table) { free(normals); return 0; }

    for (uint32_t f = 0; f < face_count; f++) {
        for (int e = 0; e < 3; e++) {
            uint32_t va = slot->indices[f * 3 + e];
            uint32_t vb = slot->indices[f * 3 + ((e + 1) % 3)];
            uint64_t key = edge_key_(slot->positions, va, vb);
            edge_find_or_insert_(edge_table, key, f);
        }
    }

    /* Build face-face adjacency list (max 3 neighbors per face) */
    uint32_t *adj = malloc(face_count * 3 * sizeof(uint32_t));
    uint32_t *adj_count_arr = calloc(face_count, sizeof(uint32_t));
    if (!adj || !adj_count_arr) {
        free(normals); free(edge_table); free(adj); free(adj_count_arr);
        return 0;
    }

    for (uint32_t i = 0; i < EDGE_MAP_SIZE; i++) {
        if (!edge_table[i].occupied) continue;
        uint32_t fa = edge_table[i].face_a;
        uint32_t fb = edge_table[i].face_b;
        if (fb == UINT32_MAX) continue; /* boundary edge */

        /* Check dihedral angle */
        float angle = dihedral_angle_(&normals[fa * 3], &normals[fb * 3]);
        if (angle < angle_threshold) {
            /* Connected: add to adjacency */
            if (adj_count_arr[fa] < 3) {
                adj[fa * 3 + adj_count_arr[fa]++] = fb;
            }
            if (adj_count_arr[fb] < 3) {
                adj[fb * 3 + adj_count_arr[fb]++] = fa;
            }
        }
    }

    free(edge_table);
    free(normals);

    /* Flood-fill connected components */
    uint8_t *visited = calloc(face_count, 1);
    uint32_t *queue = malloc(face_count * sizeof(uint32_t));
    if (!visited || !queue) {
        free(adj); free(adj_count_arr); free(visited); free(queue);
        return 0;
    }

    out_islands->count = 0;

    for (uint32_t f = 0; f < face_count; f++) {
        if (visited[f]) continue;
        if (out_islands->count >= MESH_UV_MAX_ISLANDS) break;

        mesh_uv_island_t *island = &out_islands->islands[out_islands->count];
        memset(island, 0, sizeof(*island));

        /* BFS */
        uint32_t q_head = 0, q_tail = 0;
        queue[q_tail++] = f;
        visited[f] = 1;

        while (q_head < q_tail) {
            uint32_t cur = queue[q_head++];
            island_add_face_(island, cur);

            for (uint32_t n = 0; n < adj_count_arr[cur]; n++) {
                uint32_t nb = adj[cur * 3 + n];
                if (!visited[nb]) {
                    visited[nb] = 1;
                    queue[q_tail++] = nb;
                }
            }
        }

        out_islands->count++;
    }

    free(adj);
    free(adj_count_arr);
    free(visited);
    free(queue);

    return out_islands->count;
}

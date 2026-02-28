/**
 * @file client_mesh_edges.c
 * @brief Extract unique edges from a triangle index buffer.
 */
#include "ferrum/editor/client/client_mesh_render.h"

#include <stdlib.h>
#include <string.h>

/** Simple edge hash set for deduplication. */
#define EDGE_SET_SIZE 4096
#define EDGE_SET_MASK (EDGE_SET_SIZE - 1)

typedef struct edge_entry {
    uint32_t a, b;
    bool     occupied;
} edge_entry_t;

/** Insert edge (canonical order: a < b). Returns true if new. */
static bool edge_set_insert_(edge_entry_t *table, uint32_t a, uint32_t b) {
    if (a > b) { uint32_t t = a; a = b; b = t; }
    uint32_t hash = (a * 2654435761u + b) & EDGE_SET_MASK;
    for (uint32_t probe = 0; probe < EDGE_SET_SIZE; probe++) {
        uint32_t i = (hash + probe) & EDGE_SET_MASK;
        if (!table[i].occupied) {
            table[i].a = a;
            table[i].b = b;
            table[i].occupied = true;
            return true;
        }
        if (table[i].a == a && table[i].b == b) {
            return false; /* already exists */
        }
    }
    return false; /* table full */
}

bool client_mesh_extract_edges(client_mesh_edges_t *out,
                               const uint32_t *indices,
                               uint32_t index_count) {
    if (!out || !indices || index_count < 3) return false;
    memset(out, 0, sizeof(*out));

    edge_entry_t *table = calloc(EDGE_SET_SIZE, sizeof(edge_entry_t));
    if (!table) return false;

    /* First pass: count unique edges */
    uint32_t count = 0;
    uint32_t face_count = index_count / 3;
    for (uint32_t f = 0; f < face_count; f++) {
        for (int e = 0; e < 3; e++) {
            uint32_t a = indices[f * 3 + e];
            uint32_t b = indices[f * 3 + ((e + 1) % 3)];
            if (edge_set_insert_(table, a, b)) count++;
        }
    }

    /* Allocate edge index buffer (2 indices per edge) */
    out->edge_indices = malloc(count * 2 * sizeof(uint32_t));
    if (!out->edge_indices) { free(table); return false; }

    /* Collect edges */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < EDGE_SET_SIZE; i++) {
        if (table[i].occupied) {
            out->edge_indices[idx * 2 + 0] = table[i].a;
            out->edge_indices[idx * 2 + 1] = table[i].b;
            idx++;
        }
    }
    out->edge_count = count;

    free(table);
    return true;
}

void client_mesh_edges_destroy(client_mesh_edges_t *edges) {
    if (!edges) return;
    free(edges->edge_indices);
    memset(edges, 0, sizeof(*edges));
}

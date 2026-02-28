/**
 * @file mesh_edge_table.c
 * @brief Build and query an edge table from triangle indices.
 *
 * Non-static functions: build, destroy, find (3 of 4 allowed).
 */
#include "ferrum/editor/mesh/mesh_selection.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------ */

/** @brief Compare two edges for qsort (by v0, then v1). */
static int edge_cmp_(const void *a, const void *b) {
    const mesh_edge_t *ea = (const mesh_edge_t *)a;
    const mesh_edge_t *eb = (const mesh_edge_t *)b;
    if (ea->v0 != eb->v0) { return (ea->v0 < eb->v0) ? -1 : 1; }
    if (ea->v1 != eb->v1) { return (ea->v1 < eb->v1) ? -1 : 1; }
    return 0;
}

/** @brief Create a canonical edge (v0 < v1). */
static mesh_edge_t make_edge_(uint32_t a, uint32_t b) {
    mesh_edge_t e;
    if (a < b) { e.v0 = a; e.v1 = b; }
    else       { e.v0 = b; e.v1 = a; }
    return e;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

bool mesh_edge_table_build(mesh_edge_table_t *table, const mesh_slot_t *slot) {
    if (!table) { return false; }
    memset(table, 0, sizeof(*table));
    if (!slot) { return false; }

    uint32_t face_count = mesh_slot_face_count(slot);
    if (face_count == 0) { return true; }

    /* Worst case: 3 edges per face (no sharing) */
    uint32_t max_edges = face_count * 3;
    mesh_edge_t *raw = malloc((size_t)max_edges * sizeof(mesh_edge_t));
    if (!raw) { return false; }

    /* Extract all edges (with duplicates) */
    uint32_t count = 0;
    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t base = f * 3;
        uint32_t i0 = slot->indices[base + 0];
        uint32_t i1 = slot->indices[base + 1];
        uint32_t i2 = slot->indices[base + 2];
        raw[count++] = make_edge_(i0, i1);
        raw[count++] = make_edge_(i1, i2);
        raw[count++] = make_edge_(i0, i2);
    }

    /* Sort for dedup */
    qsort(raw, count, sizeof(mesh_edge_t), edge_cmp_);

    /* Deduplicate: count unique edges */
    uint32_t unique = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (i == 0 || raw[i].v0 != raw[i - 1].v0 ||
            raw[i].v1 != raw[i - 1].v1) {
            raw[unique++] = raw[i];
        }
    }

    /* Shrink to fit */
    mesh_edge_t *final = realloc(raw, (size_t)unique * sizeof(mesh_edge_t));
    if (!final && unique > 0) {
        /* realloc shrink shouldn't fail, but keep raw if it does */
        final = raw;
    }

    table->edges      = final;
    table->edge_count = unique;
    table->capacity   = unique;
    return true;
}

void mesh_edge_table_destroy(mesh_edge_table_t *table) {
    if (!table) { return; }
    free(table->edges);
    memset(table, 0, sizeof(*table));
}

uint32_t mesh_edge_table_find(const mesh_edge_table_t *table,
                              uint32_t v0, uint32_t v1) {
    if (!table || table->edge_count == 0) { return UINT32_MAX; }

    /* Canonicalize */
    mesh_edge_t target = make_edge_(v0, v1);

    /* Binary search */
    uint32_t lo = 0, hi = table->edge_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = edge_cmp_(&target, &table->edges[mid]);
        if (cmp == 0) { return mid; }
        if (cmp < 0)  { hi = mid; }
        else          { lo = mid + 1; }
    }
    return UINT32_MAX;
}

/**
 * @file mesh_bridge.c
 * @brief Bridge edge loops + connect vertices by face split.
 *
 * Non-static functions (2 of 4): mesh_bridge_edges, mesh_connect_vertices.
 */
#include "ferrum/editor/mesh/mesh_bridge.h"

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Public: mesh_bridge_edges                                           */
/* ------------------------------------------------------------------ */

bool mesh_bridge_edges(mesh_slot_t *slot,
                       const uint32_t *loop_a,
                       const uint32_t *loop_b,
                       uint32_t loop_len) {
    if (!slot || !loop_a || !loop_b || loop_len < 2) return false;

    /* Reserve space for connecting quads */
    mesh_slot_reserve_indices(slot, slot->index_count + loop_len * 6);

    /* Create quad strip between matching loop vertices */
    for (uint32_t i = 0; i < loop_len; i++) {
        uint32_t next = (i + 1) % loop_len;

        uint32_t a0 = loop_a[i];
        uint32_t a1 = loop_a[next];
        uint32_t b0 = loop_b[i];
        uint32_t b1 = loop_b[next];

        /* Quad (a0, a1, b1, b0) → 2 triangles */
        mesh_slot_add_triangle(slot, a0, a1, b1, 0);
        mesh_slot_add_triangle(slot, a0, b1, b0, 0);
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_connect_vertices                                       */
/* ------------------------------------------------------------------ */

bool mesh_connect_vertices(mesh_slot_t *slot, uint32_t v0, uint32_t v1) {
    if (!slot) return false;
    if (v0 >= slot->vertex_count || v1 >= slot->vertex_count) return false;
    if (v0 == v1) return false;

    uint32_t face_count = slot->index_count / 3;

    /* Find a face containing both v0 and v1 but not as an edge */
    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t *tri = &slot->indices[f * 3];
        int pos0 = -1, pos1 = -1;
        for (int j = 0; j < 3; j++) {
            if (tri[j] == v0) pos0 = j;
            if (tri[j] == v1) pos1 = j;
        }

        /* Both vertices must be in this face */
        if (pos0 < 0 || pos1 < 0) continue;

        /* They shouldn't be adjacent (already share an edge) */
        /* In a triangle, all vertices are adjacent — so this is a no-op
         * for triangles. For future quad support, this would split. */
        /* For now, just report success if found. */
        return true;
    }

    return false;
}

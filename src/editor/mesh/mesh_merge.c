/**
 * @file mesh_merge.c
 * @brief Vertex merge (weld) — merge selected vertices to a point.
 *
 * Non-static functions (2 of 4): mesh_merge_vertices, mesh_merge_by_distance.
 */
#include "ferrum/editor/mesh/mesh_merge.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Static: remove degenerate triangles                                 */
/* ------------------------------------------------------------------ */

/**
 * Remove triangles where any two indices are equal.
 * Compacts the index buffer in-place.
 */
static void remove_degenerates_(mesh_slot_t *slot) {
    uint32_t write = 0;
    uint32_t face_count = slot->index_count / 3;

    for (uint32_t f = 0; f < face_count; f++) {
        uint32_t a = slot->indices[f*3+0];
        uint32_t b = slot->indices[f*3+1];
        uint32_t c = slot->indices[f*3+2];

        if (a == b || b == c || a == c) {
            /* Degenerate — skip */
            continue;
        }

        if (write != f) {
            slot->indices[write*3+0] = a;
            slot->indices[write*3+1] = b;
            slot->indices[write*3+2] = c;
            if (slot->polygroup_ids) {
                slot->polygroup_ids[write] = slot->polygroup_ids[f];
            }
        }
        write++;
    }
    slot->index_count = write * 3;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_merge_vertices                                         */
/* ------------------------------------------------------------------ */

bool mesh_merge_vertices(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                         mesh_merge_target_t target, const float *cursor) {
    if (!slot || !sel) return false;
    if (sel->count < 2) return false;

    /* Find first and last selected, compute centroid */
    uint32_t first_sel = UINT32_MAX, last_sel = 0;
    float cx = 0, cy = 0, cz = 0;
    uint32_t count = 0;

    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        if (!mesh_sel_bitset_test(sel, v)) continue;
        if (first_sel == UINT32_MAX) first_sel = v;
        last_sel = v;
        cx += slot->positions[v*3+0];
        cy += slot->positions[v*3+1];
        cz += slot->positions[v*3+2];
        count++;
    }
    if (count < 2) return false;

    /* Determine target position */
    float tpos[3];
    switch (target) {
    case MESH_MERGE_CENTER:
        tpos[0] = cx / (float)count;
        tpos[1] = cy / (float)count;
        tpos[2] = cz / (float)count;
        break;
    case MESH_MERGE_CURSOR:
        if (!cursor) return false;
        tpos[0] = cursor[0]; tpos[1] = cursor[1]; tpos[2] = cursor[2];
        break;
    case MESH_MERGE_FIRST:
        tpos[0] = slot->positions[first_sel*3+0];
        tpos[1] = slot->positions[first_sel*3+1];
        tpos[2] = slot->positions[first_sel*3+2];
        break;
    case MESH_MERGE_LAST:
        tpos[0] = slot->positions[last_sel*3+0];
        tpos[1] = slot->positions[last_sel*3+1];
        tpos[2] = slot->positions[last_sel*3+2];
        break;
    default:
        return false;
    }

    /* Move surviving vertex (first_sel) to target position */
    slot->positions[first_sel*3+0] = tpos[0];
    slot->positions[first_sel*3+1] = tpos[1];
    slot->positions[first_sel*3+2] = tpos[2];

    /* Remap all selected vertices to first_sel in index buffer */
    for (uint32_t i = 0; i < slot->index_count; i++) {
        uint32_t vi = slot->indices[i];
        if (vi != first_sel && mesh_sel_bitset_test(sel, vi)) {
            slot->indices[i] = first_sel;
        }
    }

    /* Remove degenerate triangles */
    remove_degenerates_(slot);

    return true;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_merge_by_distance                                      */
/* ------------------------------------------------------------------ */

bool mesh_merge_by_distance(mesh_slot_t *slot, float threshold) {
    if (!slot) return false;
    if (slot->vertex_count < 2) return false;

    float t2 = threshold * threshold;

    /* Build remap: vertex → surviving vertex index */
    uint32_t *remap = malloc(slot->vertex_count * sizeof(uint32_t));
    if (!remap) return false;
    for (uint32_t i = 0; i < slot->vertex_count; i++) remap[i] = i;

    /* O(V²) brute force — find and group close vertices */
    for (uint32_t i = 0; i < slot->vertex_count; i++) {
        if (remap[i] != i) continue; /* already merged */
        for (uint32_t j = i + 1; j < slot->vertex_count; j++) {
            if (remap[j] != j) continue;
            float dx = slot->positions[j*3+0] - slot->positions[i*3+0];
            float dy = slot->positions[j*3+1] - slot->positions[i*3+1];
            float dz = slot->positions[j*3+2] - slot->positions[i*3+2];
            if (dx*dx + dy*dy + dz*dz < t2) {
                remap[j] = i;
            }
        }
    }

    /* Apply remap to indices */
    for (uint32_t k = 0; k < slot->index_count; k++) {
        slot->indices[k] = remap[slot->indices[k]];
    }

    free(remap);
    remove_degenerates_(slot);
    return true;
}

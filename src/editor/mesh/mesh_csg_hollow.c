/**
 * @file mesh_csg_hollow.c
 * @brief CSG hollow — create inner shell offset inward.
 *
 * Non-static functions (1 of 4): mesh_csg_hollow.
 */
#include "ferrum/editor/mesh/mesh_csg.h"

#include <math.h>
#include <string.h>

bool mesh_csg_hollow(mesh_slot_t *slot, float thickness) {
    if (!slot || thickness <= 0.0f) return false;

    uint32_t orig_vc = slot->vertex_count;
    uint32_t orig_ic = slot->index_count;
    uint32_t orig_fc = orig_ic / 3;

    if (orig_vc == 0 || orig_ic == 0) return false;

    /* Reserve space for inner shell (double everything) */
    if (!mesh_slot_reserve_vertices(slot, orig_vc * 2)) return false;
    if (!mesh_slot_reserve_indices(slot, orig_ic * 2)) return false;

    /* Copy vertices with inward offset along normals */
    for (uint32_t v = 0; v < orig_vc; v++) {
        float pos[3], nrm[3];
        for (int k = 0; k < 3; k++) {
            nrm[k] = slot->normals[v*3+k];
            pos[k] = slot->positions[v*3+k] - nrm[k] * thickness;
        }
        /* Invert normal for inner shell */
        nrm[0] = -nrm[0]; nrm[1] = -nrm[1]; nrm[2] = -nrm[2];
        mesh_slot_add_vertex(slot, pos, nrm);
    }

    /* Copy triangles with reversed winding for inner shell */
    for (uint32_t f = 0; f < orig_fc; f++) {
        uint32_t i0 = slot->indices[f*3+0] + orig_vc;
        uint32_t i1 = slot->indices[f*3+1] + orig_vc;
        uint32_t i2 = slot->indices[f*3+2] + orig_vc;
        uint16_t pg = slot->polygroup_ids ? slot->polygroup_ids[f] : 0;
        /* Reversed winding: i0, i2, i1 */
        mesh_slot_add_triangle(slot, i0, i2, i1, pg);
    }

    return true;
}

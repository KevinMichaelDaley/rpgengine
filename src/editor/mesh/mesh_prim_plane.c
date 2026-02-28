/**
 * @file mesh_prim_plane.c
 * @brief Plane primitive generator.
 *
 * Non-static functions: mesh_prim_plane (1 of 4).
 */
#include "ferrum/editor/mesh/mesh_primitives.h"

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_prim_plane(mesh_slot_t *slot, const float size[2],
                     const uint32_t segs[2], int axis, const float pos[3]) {
    if (!slot || !size || !segs || !pos) { return false; }
    if (axis < 0 || axis > 2) { return false; }
    if (segs[0] == 0 || segs[1] == 0) { return false; }

    mesh_slot_clear(slot);

    uint32_t cols = segs[0] + 1;
    uint32_t rows = segs[1] + 1;

    float hw = size[0] * 0.5f;
    float hd = size[1] * 0.5f;

    /* Choose tangent axes based on up axis */
    /* axis=0(X): plane in YZ, axis=1(Y): plane in XZ, axis=2(Z): plane in XY */
    int u_axis, v_axis;
    float normal[3] = {0, 0, 0};
    normal[axis] = 1.0f;

    if (axis == 0)      { u_axis = 1; v_axis = 2; }
    else if (axis == 1) { u_axis = 0; v_axis = 2; }
    else                { u_axis = 0; v_axis = 1; }

    /* Emit vertices */
    for (uint32_t r = 0; r < rows; r++) {
        float fv = (float)r / (float)segs[1];
        for (uint32_t c = 0; c < cols; c++) {
            float fu = (float)c / (float)segs[0];

            float p[3] = {pos[0], pos[1], pos[2]};
            p[u_axis] += -hw + size[0] * fu;
            p[v_axis] += -hd + size[1] * fv;

            uint32_t vi = mesh_slot_add_vertex(slot, p, normal);
            if (vi == UINT32_MAX) { return false; }

            slot->uvs[0][vi * 2 + 0] = fu;
            slot->uvs[0][vi * 2 + 1] = fv;
        }
    }

    /* Emit triangles (CCW) */
    for (uint32_t r = 0; r < segs[1]; r++) {
        for (uint32_t c = 0; c < segs[0]; c++) {
            uint32_t tl = r * cols + c;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + cols;
            uint32_t br = bl + 1;

            if (!mesh_slot_add_triangle(slot, tl, bl, tr, 0)) { return false; }
            if (!mesh_slot_add_triangle(slot, tr, bl, br, 0)) { return false; }
        }
    }

    return true;
}

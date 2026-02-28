/**
 * @file mesh_prim_box.c
 * @brief Box primitive generator.
 *
 * Non-static functions: mesh_prim_box (1 of 4).
 *
 * Each face of the box is a grid of (segs[a]+1)×(segs[b]+1) vertices,
 * with segs[a]*segs[b]*2 triangles. Vertices are not shared across
 * faces to allow hard normals at edges.
 */
#include "ferrum/editor/mesh/mesh_primitives.h"

#include <string.h>

/* Face table: for each of 6 faces, define the two tangent axes and
 * the normal axis, plus the sign of the normal and the axis offsets. */

/**
 * @brief Generate one face of the box as a subdivided quad.
 *
 * @param slot    Target slot.
 * @param origin  Corner position (3 floats).
 * @param udir    U tangent direction (3 floats, length = face width).
 * @param vdir    V tangent direction (3 floats, length = face height).
 * @param normal  Face normal (3 floats, unit).
 * @param su      Segments along U.
 * @param sv      Segments along V.
 */
static bool emit_face_(mesh_slot_t *slot,
                        const float origin[3],
                        const float udir[3],
                        const float vdir[3],
                        const float normal[3],
                        uint32_t su, uint32_t sv) {
    uint32_t base_vert = slot->vertex_count;
    uint32_t cols = su + 1;
    uint32_t rows = sv + 1;

    /* Emit vertices */
    for (uint32_t r = 0; r < rows; r++) {
        float fv = (float)r / (float)sv;
        for (uint32_t c = 0; c < cols; c++) {
            float fu = (float)c / (float)su;

            float pos[3] = {
                origin[0] + udir[0] * fu + vdir[0] * fv,
                origin[1] + udir[1] * fu + vdir[1] * fv,
                origin[2] + udir[2] * fu + vdir[2] * fv
            };

            uint32_t vi = mesh_slot_add_vertex(slot, pos, normal);
            if (vi == UINT32_MAX) { return false; }

            /* Set UV0 */
            slot->uvs[0][vi * 2 + 0] = fu;
            slot->uvs[0][vi * 2 + 1] = fv;
        }
    }

    /* Emit triangles (CCW winding) */
    for (uint32_t r = 0; r < sv; r++) {
        for (uint32_t c = 0; c < su; c++) {
            uint32_t tl = base_vert + r * cols + c;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + cols;
            uint32_t br = bl + 1;

            if (!mesh_slot_add_triangle(slot, tl, bl, tr, 0)) { return false; }
            if (!mesh_slot_add_triangle(slot, tr, bl, br, 0)) { return false; }
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_prim_box(mesh_slot_t *slot, const float size[3],
                   const uint32_t segs[3], const float pos[3]) {
    if (!slot || !size || !segs || !pos) { return false; }

    mesh_slot_clear(slot);

    float hx = size[0] * 0.5f;
    float hy = size[1] * 0.5f;
    float hz = size[2] * 0.5f;

    float ox = pos[0], oy = pos[1], oz = pos[2];

    /* 6 faces: +X, -X, +Y, -Y, +Z, -Z */
    /* Each face: origin, U direction, V direction, normal, segs_u, segs_v */

    /* +Z face (front) */
    {
        float origin[3] = {ox - hx, oy - hy, oz + hz};
        float udir[3]   = {size[0], 0, 0};
        float vdir[3]   = {0, size[1], 0};
        float normal[3] = {0, 0, 1};
        if (!emit_face_(slot, origin, udir, vdir, normal, segs[0], segs[1])) return false;
    }
    /* -Z face (back) */
    {
        float origin[3] = {ox + hx, oy - hy, oz - hz};
        float udir[3]   = {-size[0], 0, 0};
        float vdir[3]   = {0, size[1], 0};
        float normal[3] = {0, 0, -1};
        if (!emit_face_(slot, origin, udir, vdir, normal, segs[0], segs[1])) return false;
    }
    /* +X face (right) */
    {
        float origin[3] = {ox + hx, oy - hy, oz + hz};
        float udir[3]   = {0, 0, -size[2]};
        float vdir[3]   = {0, size[1], 0};
        float normal[3] = {1, 0, 0};
        if (!emit_face_(slot, origin, udir, vdir, normal, segs[2], segs[1])) return false;
    }
    /* -X face (left) */
    {
        float origin[3] = {ox - hx, oy - hy, oz - hz};
        float udir[3]   = {0, 0, size[2]};
        float vdir[3]   = {0, size[1], 0};
        float normal[3] = {-1, 0, 0};
        if (!emit_face_(slot, origin, udir, vdir, normal, segs[2], segs[1])) return false;
    }
    /* +Y face (top) */
    {
        float origin[3] = {ox - hx, oy + hy, oz + hz};
        float udir[3]   = {size[0], 0, 0};
        float vdir[3]   = {0, 0, -size[2]};
        float normal[3] = {0, 1, 0};
        if (!emit_face_(slot, origin, udir, vdir, normal, segs[0], segs[2])) return false;
    }
    /* -Y face (bottom) */
    {
        float origin[3] = {ox - hx, oy - hy, oz - hz};
        float udir[3]   = {size[0], 0, 0};
        float vdir[3]   = {0, 0, size[2]};
        float normal[3] = {0, -1, 0};
        if (!emit_face_(slot, origin, udir, vdir, normal, segs[0], segs[2])) return false;
    }

    return true;
}

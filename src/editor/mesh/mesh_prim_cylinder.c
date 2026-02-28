/**
 * @file mesh_prim_cylinder.c
 * @brief Cylinder primitive generator with top/bottom caps.
 *
 * Non-static functions: mesh_prim_cylinder (1 of 4).
 */
#include "ferrum/editor/mesh/mesh_primitives.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_prim_cylinder(mesh_slot_t *slot, float radius, float height,
                        uint32_t segments, int axis, const float pos[3]) {
    if (!slot || !pos) { return false; }
    if (segments < 3) { return false; }
    if (axis < 0 || axis > 2) { return false; }

    mesh_slot_clear(slot);

    float half_h = height * 0.5f;

    /* Determine axis mapping: up_axis is height, a0/a1 are radial */
    int up = axis;
    int a0 = (axis + 1) % 3;
    int a1 = (axis + 2) % 3;

    /* --- Side vertices: 2 rings × segments --- */
    for (uint32_t ring = 0; ring < 2; ring++) {
        float h = (ring == 0) ? -half_h : half_h;
        for (uint32_t s = 0; s < segments; s++) {
            float angle = (float)s / (float)segments * 2.0f * (float)M_PI;
            float c = cosf(angle);
            float si = sinf(angle);

            float p[3] = {pos[0], pos[1], pos[2]};
            p[up] += h;
            p[a0] += radius * c;
            p[a1] += radius * si;

            float n[3] = {0, 0, 0};
            n[a0] = c;
            n[a1] = si;

            uint32_t vi = mesh_slot_add_vertex(slot, p, n);
            if (vi == UINT32_MAX) { return false; }

            slot->uvs[0][vi * 2 + 0] = (float)s / (float)segments;
            slot->uvs[0][vi * 2 + 1] = (float)ring;
        }
    }

    /* --- Side triangles --- */
    for (uint32_t s = 0; s < segments; s++) {
        uint32_t s_next = (s + 1) % segments;
        uint32_t bl = s;
        uint32_t br = s_next;
        uint32_t tl = segments + s;
        uint32_t tr = segments + s_next;

        if (!mesh_slot_add_triangle(slot, bl, br, tl, 0)) { return false; }
        if (!mesh_slot_add_triangle(slot, tl, br, tr, 0)) { return false; }
    }

    /* --- Top cap --- */
    {
        float center_pos[3] = {pos[0], pos[1], pos[2]};
        center_pos[up] += half_h;
        float cap_n[3] = {0, 0, 0};
        cap_n[up] = 1.0f;

        uint32_t center = mesh_slot_add_vertex(slot, center_pos, cap_n);
        if (center == UINT32_MAX) { return false; }
        slot->uvs[0][center * 2 + 0] = 0.5f;
        slot->uvs[0][center * 2 + 1] = 0.5f;

        uint32_t cap_base = slot->vertex_count;
        for (uint32_t s = 0; s < segments; s++) {
            float angle = (float)s / (float)segments * 2.0f * (float)M_PI;
            float c = cosf(angle);
            float si = sinf(angle);

            float p[3] = {pos[0], pos[1], pos[2]};
            p[up] += half_h;
            p[a0] += radius * c;
            p[a1] += radius * si;

            uint32_t vi = mesh_slot_add_vertex(slot, p, cap_n);
            if (vi == UINT32_MAX) { return false; }
            slot->uvs[0][vi * 2 + 0] = 0.5f + 0.5f * c;
            slot->uvs[0][vi * 2 + 1] = 0.5f + 0.5f * si;
        }

        for (uint32_t s = 0; s < segments; s++) {
            uint32_t s_next = (s + 1) % segments;
            if (!mesh_slot_add_triangle(slot, center,
                    cap_base + s, cap_base + s_next, 0)) { return false; }
        }
    }

    /* --- Bottom cap --- */
    {
        float center_pos[3] = {pos[0], pos[1], pos[2]};
        center_pos[up] -= half_h;
        float cap_n[3] = {0, 0, 0};
        cap_n[up] = -1.0f;

        uint32_t center = mesh_slot_add_vertex(slot, center_pos, cap_n);
        if (center == UINT32_MAX) { return false; }
        slot->uvs[0][center * 2 + 0] = 0.5f;
        slot->uvs[0][center * 2 + 1] = 0.5f;

        uint32_t cap_base = slot->vertex_count;
        for (uint32_t s = 0; s < segments; s++) {
            float angle = (float)s / (float)segments * 2.0f * (float)M_PI;
            float c = cosf(angle);
            float si = sinf(angle);

            float p[3] = {pos[0], pos[1], pos[2]};
            p[up] -= half_h;
            p[a0] += radius * c;
            p[a1] += radius * si;

            uint32_t vi = mesh_slot_add_vertex(slot, p, cap_n);
            if (vi == UINT32_MAX) { return false; }
            slot->uvs[0][vi * 2 + 0] = 0.5f + 0.5f * c;
            slot->uvs[0][vi * 2 + 1] = 0.5f + 0.5f * si;
        }

        for (uint32_t s = 0; s < segments; s++) {
            uint32_t s_next = (s + 1) % segments;
            /* Reversed winding for bottom cap */
            if (!mesh_slot_add_triangle(slot, center,
                    cap_base + s_next, cap_base + s, 0)) { return false; }
        }
    }

    return true;
}

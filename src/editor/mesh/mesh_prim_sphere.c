/**
 * @file mesh_prim_sphere.c
 * @brief UV sphere primitive generator.
 *
 * Non-static functions: mesh_prim_sphere (1 of 4).
 *
 * Generates a UV sphere with `segments` longitude divisions and
 * `segments/2` latitude divisions. Poles share a single vertex each.
 */
#include "ferrum/editor/mesh/mesh_primitives.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_prim_sphere(mesh_slot_t *slot, float radius, uint32_t segments,
                      const float pos[3]) {
    if (!slot || !pos) { return false; }
    if (segments < 4) { segments = 4; }

    mesh_slot_clear(slot);

    uint32_t lon_segs = segments;
    uint32_t lat_segs = segments / 2;
    if (lat_segs < 2) { lat_segs = 2; }

    /* --- Generate vertices --- */
    /* Top pole */
    {
        float p[3] = {pos[0], pos[1] + radius, pos[2]};
        float n[3] = {0, 1, 0};
        uint32_t vi = mesh_slot_add_vertex(slot, p, n);
        if (vi == UINT32_MAX) { return false; }
        slot->uvs[0][vi * 2 + 0] = 0.5f;
        slot->uvs[0][vi * 2 + 1] = 0.0f;
    }

    /* Body rings (from top to bottom, excluding poles) */
    for (uint32_t lat = 1; lat < lat_segs; lat++) {
        float phi = (float)M_PI * (float)lat / (float)lat_segs;
        float sp = sinf(phi);
        float cp = cosf(phi);

        for (uint32_t lon = 0; lon <= lon_segs; lon++) {
            float theta = 2.0f * (float)M_PI * (float)lon / (float)lon_segs;
            float st = sinf(theta);
            float ct = cosf(theta);

            float nx = sp * ct;
            float ny = cp;
            float nz = sp * st;

            float p[3] = {
                pos[0] + radius * nx,
                pos[1] + radius * ny,
                pos[2] + radius * nz
            };
            float n[3] = {nx, ny, nz};

            uint32_t vi = mesh_slot_add_vertex(slot, p, n);
            if (vi == UINT32_MAX) { return false; }

            slot->uvs[0][vi * 2 + 0] = (float)lon / (float)lon_segs;
            slot->uvs[0][vi * 2 + 1] = (float)lat / (float)lat_segs;
        }
    }

    /* Bottom pole */
    {
        float p[3] = {pos[0], pos[1] - radius, pos[2]};
        float n[3] = {0, -1, 0};
        uint32_t vi = mesh_slot_add_vertex(slot, p, n);
        if (vi == UINT32_MAX) { return false; }
        slot->uvs[0][vi * 2 + 0] = 0.5f;
        slot->uvs[0][vi * 2 + 1] = 1.0f;
    }

    /* --- Generate triangles --- */
    uint32_t ring_size = lon_segs + 1;

    /* Top cap: pole (vertex 0) to first ring */
    for (uint32_t lon = 0; lon < lon_segs; lon++) {
        uint32_t v0 = 0; /* top pole */
        uint32_t v1 = 1 + lon;
        uint32_t v2 = 1 + lon + 1;
        if (!mesh_slot_add_triangle(slot, v0, v1, v2, 0)) { return false; }
    }

    /* Body quads between rings */
    for (uint32_t lat = 0; lat < lat_segs - 2; lat++) {
        uint32_t row0 = 1 + lat * ring_size;
        uint32_t row1 = 1 + (lat + 1) * ring_size;

        for (uint32_t lon = 0; lon < lon_segs; lon++) {
            uint32_t tl = row0 + lon;
            uint32_t tr = row0 + lon + 1;
            uint32_t bl = row1 + lon;
            uint32_t br = row1 + lon + 1;

            if (!mesh_slot_add_triangle(slot, tl, bl, tr, 0)) { return false; }
            if (!mesh_slot_add_triangle(slot, tr, bl, br, 0)) { return false; }
        }
    }

    /* Bottom cap: last ring to pole */
    uint32_t bottom_pole = slot->vertex_count - 1;
    uint32_t last_ring   = 1 + (lat_segs - 2) * ring_size;
    for (uint32_t lon = 0; lon < lon_segs; lon++) {
        uint32_t v0 = last_ring + lon;
        uint32_t v1 = bottom_pole;
        uint32_t v2 = last_ring + lon + 1;
        if (!mesh_slot_add_triangle(slot, v0, v1, v2, 0)) { return false; }
    }

    return true;
}

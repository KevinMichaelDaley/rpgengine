/**
 * @file mesh_uv_pack.c
 * @brief Shelf-based UV island packing into [0,1] square.
 */
#include "ferrum/editor/mesh/mesh_uv_pack.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Compute bounding box of an island's UVs. */
static void island_uv_bounds_(const mesh_slot_t *slot,
                                const mesh_uv_island_t *island,
                                float *umin, float *umax,
                                float *vmin, float *vmax) {
    *umin = 1e30f; *umax = -1e30f;
    *vmin = 1e30f; *vmax = -1e30f;
    for (uint32_t i = 0; i < island->face_count; i++) {
        uint32_t f = island->face_indices[i];
        for (int c = 0; c < 3; c++) {
            uint32_t vi = slot->indices[f * 3 + c];
            float u = slot->uvs[0][vi * 2 + 0];
            float v = slot->uvs[0][vi * 2 + 1];
            if (u < *umin) *umin = u; if (u > *umax) *umax = u;
            if (v < *vmin) *vmin = v; if (v > *vmax) *vmax = v;
        }
    }
}

/** Shift island UVs by offset. */
static void island_shift_(mesh_slot_t *slot,
                            const mesh_uv_island_t *island,
                            float du, float dv) {
    uint8_t *touched = calloc((slot->vertex_count + 7) / 8, 1);
    if (!touched) return;
    for (uint32_t i = 0; i < island->face_count; i++) {
        uint32_t f = island->face_indices[i];
        for (int c = 0; c < 3; c++) {
            uint32_t vi = slot->indices[f * 3 + c];
            uint32_t byte = vi / 8;
            uint8_t bit = (uint8_t)(1u << (vi % 8));
            if (touched[byte] & bit) continue;
            touched[byte] |= bit;
            slot->uvs[0][vi * 2 + 0] += du;
            slot->uvs[0][vi * 2 + 1] += dv;
        }
    }
    free(touched);
}

/** Scale island UVs by factor relative to its min corner. */
static void island_scale_(mesh_slot_t *slot,
                            const mesh_uv_island_t *island,
                            float su, float sv,
                            float origin_u, float origin_v) {
    uint8_t *touched = calloc((slot->vertex_count + 7) / 8, 1);
    if (!touched) return;
    for (uint32_t i = 0; i < island->face_count; i++) {
        uint32_t f = island->face_indices[i];
        for (int c = 0; c < 3; c++) {
            uint32_t vi = slot->indices[f * 3 + c];
            uint32_t byte = vi / 8;
            uint8_t bit = (uint8_t)(1u << (vi % 8));
            if (touched[byte] & bit) continue;
            touched[byte] |= bit;
            slot->uvs[0][vi * 2 + 0] = (slot->uvs[0][vi * 2 + 0] - origin_u) * su + origin_u;
            slot->uvs[0][vi * 2 + 1] = (slot->uvs[0][vi * 2 + 1] - origin_v) * sv + origin_v;
        }
    }
    free(touched);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_uv_pack_islands(mesh_slot_t *slot,
                          const mesh_uv_island_set_t *islands,
                          float padding,
                          uint32_t resolution) {
    if (!slot || !islands || islands->count == 0) return false;
    (void)resolution;

    uint32_t n = islands->count;

    /* First normalize each island to its own [0,1] bounding box */
    for (uint32_t i = 0; i < n; i++) {
        const mesh_uv_island_t *island = &islands->islands[i];
        float umin, umax, vmin, vmax;
        island_uv_bounds_(slot, island, &umin, &umax, &vmin, &vmax);
        float du = umax - umin;
        float dv = vmax - vmin;
        if (du < 1e-12f) du = 1.0f;
        if (dv < 1e-12f) dv = 1.0f;
        /* Shift to origin then scale to [0,1] */
        island_shift_(slot, island, -umin, -vmin);
        island_scale_(slot, island, 1.0f/du, 1.0f/dv, 0.0f, 0.0f);
    }

    /* Shelf packing: arrange islands in a grid */
    uint32_t cols = (uint32_t)ceilf(sqrtf((float)n));
    uint32_t rows = (n + cols - 1) / cols;

    float cell_w = (1.0f - padding * (float)(cols + 1)) / (float)cols;
    float cell_h = (1.0f - padding * (float)(rows + 1)) / (float)rows;
    if (cell_w < 0.01f) cell_w = 0.01f;
    if (cell_h < 0.01f) cell_h = 0.01f;

    for (uint32_t i = 0; i < n; i++) {
        const mesh_uv_island_t *island = &islands->islands[i];
        uint32_t col = i % cols;
        uint32_t row = i / cols;
        float x0 = padding + (float)col * (cell_w + padding);
        float y0 = padding + (float)row * (cell_h + padding);

        /* Scale island from [0,1] to cell size, then shift to position */
        island_scale_(slot, island, cell_w, cell_h, 0.0f, 0.0f);
        island_shift_(slot, island, x0, y0);
    }

    return true;
}

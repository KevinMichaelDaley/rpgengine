/**
 * @file srd_sdf_layout_raster.c
 * @brief Soft rasteriser and adjacency query helpers.
 *
 * Non-static functions (3): srd_sdf_layout_rasterize,
 *                            srd_sdf_layout_adj_count, srd_sdf_layout_adj_list
 */
#include "ferrum/procgen/srd/srd_sdf_layout.h"

#include <math.h>

void srd_sdf_layout_rasterize(const srd_sdf_layout_t *layout,
                              int grid_w, int grid_h,
                              float temperature, float *out) {
    if (!layout || !out || grid_w <= 0 || grid_h <= 0) return;

    for (int gz = 0; gz < grid_h; gz++) {
        for (int gx = 0; gx < grid_w; gx++) {
            float wx = (float)gx + 0.5f;
            float wz = (float)gz + 0.5f;
            float sdf = srd_sdf_layout_union(layout, wx, wz, temperature);
            /* Occupancy: sigmoid(-sdf / T) */
            float occ = 1.0f / (1.0f + expf(sdf / temperature));
            out[gz * grid_w + gx] = occ;
        }
    }
}

int srd_sdf_layout_adj_count(const srd_sdf_layout_t *layout, int idx) {
    if (!layout || idx < 0 || idx >= layout->n_boxes) return 0;
    int count = 0;
    for (int j = 0; j < layout->n_boxes; j++) {
        if (j != idx && layout->adj[idx * SRD_MAX_BOXES + j] != 0)
            count++;
    }
    return count;
}

int srd_sdf_layout_adj_list(const srd_sdf_layout_t *layout, int idx,
                            int *out, int max_out) {
    if (!layout || !out || idx < 0 || idx >= layout->n_boxes) return 0;
    int count = 0;
    for (int j = 0; j < layout->n_boxes && count < max_out; j++) {
        if (j != idx && layout->adj[idx * SRD_MAX_BOXES + j] != 0)
            out[count++] = j;
    }
    return count;
}

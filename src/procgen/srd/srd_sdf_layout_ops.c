/**
 * @file srd_sdf_layout_ops.c
 * @brief SDF layout: box add/remove, adjacency get/set/count/list.
 *
 * Non-static functions (4): srd_sdf_layout_add_box, srd_sdf_layout_remove_box,
 *                            srd_sdf_layout_set_adj, srd_sdf_layout_get_adj
 */
#include "ferrum/procgen/srd/srd_sdf_layout.h"

#include <string.h>

int srd_sdf_layout_add_box(srd_sdf_layout_t *layout,
                           const srd_sdf_box_t *box) {
    if (!layout || !box || layout->n_boxes >= SRD_MAX_BOXES) return -1;
    int idx = layout->n_boxes;
    layout->boxes[idx] = *box;

    /* Clear adjacency row and column for the new box */
    for (int j = 0; j < SRD_MAX_BOXES; j++) {
        layout->adj[idx * SRD_MAX_BOXES + j] = 0;
        layout->adj[j * SRD_MAX_BOXES + idx] = 0;
    }

    layout->n_boxes++;
    return idx;
}

int srd_sdf_layout_remove_box(srd_sdf_layout_t *layout, int idx) {
    if (!layout || idx < 0 || idx >= layout->n_boxes) return -1;

    int n = layout->n_boxes;

    /* Shift boxes down */
    for (int i = idx; i < n - 1; i++)
        layout->boxes[i] = layout->boxes[i + 1];

    /* Rebuild adjacency: shift rows and columns, skipping idx */
    /* We build a temporary copy of the relevant portion */
    uint8_t old_adj[SRD_MAX_BOXES * SRD_MAX_BOXES];
    memcpy(old_adj, layout->adj, sizeof(old_adj));

    /* Clear target adjacency area */
    memset(layout->adj, 0, sizeof(layout->adj));

    /* Remap: old indices [0..n-1] minus idx → new indices [0..n-2] */
    for (int oi = 0; oi < n; oi++) {
        if (oi == idx) continue;
        int ni = (oi < idx) ? oi : oi - 1;
        for (int oj = 0; oj < n; oj++) {
            if (oj == idx) continue;
            int nj = (oj < idx) ? oj : oj - 1;
            layout->adj[ni * SRD_MAX_BOXES + nj] =
                old_adj[oi * SRD_MAX_BOXES + oj];
        }
    }

    layout->n_boxes = n - 1;
    return 0;
}

void srd_sdf_layout_set_adj(srd_sdf_layout_t *layout,
                            int i, int j, bool value) {
    if (!layout || i < 0 || j < 0 || i == j) return;
    if (i >= SRD_MAX_BOXES || j >= SRD_MAX_BOXES) return;
    uint8_t v = value ? 1 : 0;
    layout->adj[i * SRD_MAX_BOXES + j] = v;
    layout->adj[j * SRD_MAX_BOXES + i] = v;
}

bool srd_sdf_layout_get_adj(const srd_sdf_layout_t *layout, int i, int j) {
    if (!layout || i < 0 || j < 0 || i == j) return false;
    if (i >= SRD_MAX_BOXES || j >= SRD_MAX_BOXES) return false;
    return layout->adj[i * SRD_MAX_BOXES + j] != 0;
}

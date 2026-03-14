/**
 * @file viewport_bsp_layout.c
 * @brief BSP tree layout computation: walks tree to assign rects.
 *
 * Non-static functions (1 / 4 limit):
 *   viewport_bsp_compute_rects
 */

#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include "ferrum/editor/scene/scene_panel.h"

/** @brief Recursively compute rects for BSP subtree. */
static void compute_node_rect(const viewport_bsp_t *bsp, int node_index,
                              panel_rect_t rect, panel_rect_t *rects_out) {
    if (node_index >= VIEWPORT_BSP_MAX_NODES) return;
    if (!bsp->nodes[node_index].active) return;

    const viewport_bsp_node_t *node = &bsp->nodes[node_index];

    if (node->split == SPLIT_NONE) {
        /* Leaf: assign rect to viewport slot. */
        if (node->viewport_index < VIEWPORT_MAX_COUNT) {
            rects_out[node->viewport_index] = rect;
        }
        return;
    }

    /* Internal node: compute child rects based on split. */
    int left = 2 * node_index + 1;
    int right = 2 * node_index + 2;
    panel_rect_t left_rect = rect;
    panel_rect_t right_rect = rect;

    /* 1px gap on each side of the split line (2px total boundary). */
    if (node->split == SPLIT_VERTICAL) {
        int split_x = (int)(rect.w * node->ratio);
        left_rect.w = split_x - 1;
        right_rect.x = rect.x + split_x + 1;
        right_rect.w = rect.w - split_x - 1;
    } else {
        /* SPLIT_HORIZONTAL */
        int split_y = (int)(rect.h * node->ratio);
        left_rect.h = split_y - 1;
        right_rect.y = rect.y + split_y + 1;
        right_rect.h = rect.h - split_y - 1;
    }

    compute_node_rect(bsp, left, left_rect, rects_out);
    compute_node_rect(bsp, right, right_rect, rects_out);
}

void viewport_bsp_compute_rects(const viewport_bsp_t *bsp,
                                const struct panel_rect *panel_rect,
                                struct panel_rect *rects_out) {
    if (!bsp || !panel_rect || !rects_out) return;

    panel_rect_t root_rect = *panel_rect;
    compute_node_rect(bsp, 0, root_rect, rects_out);
}

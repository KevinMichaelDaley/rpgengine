/**
 * @file viewport_bsp_drag.c
 * @brief BSP divider drag operation.
 *
 * Non-static functions (1 / 4 limit):
 *   viewport_bsp_drag_divider
 */

#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"

void viewport_bsp_drag_divider(viewport_bsp_t *bsp, uint8_t node_index,
                               int delta, int total_size) {
    if (!bsp) return;
    if (node_index >= VIEWPORT_BSP_MAX_NODES) return;
    if (!bsp->nodes[node_index].active) return;
    if (bsp->nodes[node_index].split == SPLIT_NONE) return;
    if (total_size <= 0) return;

    float delta_ratio = (float)delta / (float)total_size;
    float new_ratio = bsp->nodes[node_index].ratio + delta_ratio;

    /* Clamp so both children get at least VIEWPORT_MIN_SIZE pixels. */
    float min_ratio = (float)VIEWPORT_MIN_SIZE / (float)total_size;
    float max_ratio = 1.0f - min_ratio;

    if (min_ratio > max_ratio) {
        /* Total size too small for two viewports — don't move. */
        return;
    }

    if (new_ratio < min_ratio) new_ratio = min_ratio;
    if (new_ratio > max_ratio) new_ratio = max_ratio;

    bsp->nodes[node_index].ratio = new_ratio;
}

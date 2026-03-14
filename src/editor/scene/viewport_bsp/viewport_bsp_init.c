/**
 * @file viewport_bsp_init.c
 * @brief BSP tree initialization and leaf queries.
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_bsp_init
 *   viewport_bsp_leaf_count
 *   viewport_bsp_find_leaf
 */

#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include <string.h>

void viewport_bsp_init(viewport_bsp_t *bsp) {
    memset(bsp, 0, sizeof(*bsp));

    /* Root is a single leaf viewport at slot 0. */
    bsp->nodes[0].split = SPLIT_NONE;
    bsp->nodes[0].ratio = 0.0f;
    bsp->nodes[0].viewport_index = 0;
    bsp->nodes[0].active = true;

    bsp->focused_viewport = 0;
    bsp->viewport_count = 1;
}

uint8_t viewport_bsp_leaf_count(const viewport_bsp_t *bsp) {
    uint8_t count = 0;
    for (int i = 0; i < VIEWPORT_BSP_MAX_NODES; i++) {
        if (bsp->nodes[i].active && bsp->nodes[i].split == SPLIT_NONE) {
            count++;
        }
    }
    return count;
}

uint8_t viewport_bsp_find_leaf(const viewport_bsp_t *bsp,
                               uint8_t viewport_index) {
    for (int i = 0; i < VIEWPORT_BSP_MAX_NODES; i++) {
        if (bsp->nodes[i].active &&
            bsp->nodes[i].split == SPLIT_NONE &&
            bsp->nodes[i].viewport_index == viewport_index) {
            return (uint8_t)i;
        }
    }
    return VIEWPORT_BSP_MAX_NODES;
}

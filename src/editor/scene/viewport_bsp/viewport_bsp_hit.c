/**
 * @file viewport_bsp_hit.c
 * @brief BSP tree hit testing for viewports and dividers.
 *
 * Non-static functions (2 / 4 limit):
 *   viewport_bsp_hit_test_viewport
 *   viewport_bsp_hit_test_divider
 */

#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include "ferrum/editor/scene/scene_panel.h"

/** Divider grab zone half-width in pixels. */
#define DIVIDER_GRAB_HALF 4

bool viewport_bsp_hit_test_viewport(const viewport_bsp_t *bsp,
                                    const struct panel_rect *rects,
                                    int x, int y,
                                    uint8_t *hit_out) {
    if (!bsp || !rects || !hit_out) return false;

    /* Check each active leaf viewport rect. */
    for (int i = 0; i < VIEWPORT_BSP_MAX_NODES; i++) {
        if (!bsp->nodes[i].active) continue;
        if (bsp->nodes[i].split != SPLIT_NONE) continue;

        uint8_t vi = bsp->nodes[i].viewport_index;
        if (vi >= VIEWPORT_MAX_COUNT) continue;

        const panel_rect_t *r = &rects[vi];
        if (x >= r->x && x < r->x + r->w &&
            y >= r->y && y < r->y + r->h) {
            *hit_out = vi;
            return true;
        }
    }

    return false;
}

/** @brief Recursive divider hit test. */
static bool hit_divider_recursive(const viewport_bsp_t *bsp, int node_index,
                                  panel_rect_t rect, int x, int y,
                                  uint8_t *node_out) {
    if (node_index >= VIEWPORT_BSP_MAX_NODES) return false;
    if (!bsp->nodes[node_index].active) return false;
    if (bsp->nodes[node_index].split == SPLIT_NONE) return false;

    const viewport_bsp_node_t *node = &bsp->nodes[node_index];

    if (node->split == SPLIT_VERTICAL) {
        int div_x = rect.x + (int)(rect.w * node->ratio);
        if (x >= div_x - DIVIDER_GRAB_HALF &&
            x <= div_x + DIVIDER_GRAB_HALF &&
            y >= rect.y && y < rect.y + rect.h) {
            *node_out = (uint8_t)node_index;
            return true;
        }

        /* Recurse into children. */
        int split_x = (int)(rect.w * node->ratio);
        panel_rect_t left_rect = rect;
        left_rect.w = split_x;
        panel_rect_t right_rect = rect;
        right_rect.x = rect.x + split_x;
        right_rect.w = rect.w - split_x;

        int left = 2 * node_index + 1;
        int right_child = 2 * node_index + 2;
        if (hit_divider_recursive(bsp, left, left_rect, x, y, node_out))
            return true;
        if (hit_divider_recursive(bsp, right_child, right_rect, x, y, node_out))
            return true;
    } else {
        /* SPLIT_HORIZONTAL */
        int div_y = rect.y + (int)(rect.h * node->ratio);
        if (y >= div_y - DIVIDER_GRAB_HALF &&
            y <= div_y + DIVIDER_GRAB_HALF &&
            x >= rect.x && x < rect.x + rect.w) {
            *node_out = (uint8_t)node_index;
            return true;
        }

        int split_y = (int)(rect.h * node->ratio);
        panel_rect_t top_rect = rect;
        top_rect.h = split_y;
        panel_rect_t bot_rect = rect;
        bot_rect.y = rect.y + split_y;
        bot_rect.h = rect.h - split_y;

        int left = 2 * node_index + 1;
        int right_child = 2 * node_index + 2;
        if (hit_divider_recursive(bsp, left, top_rect, x, y, node_out))
            return true;
        if (hit_divider_recursive(bsp, right_child, bot_rect, x, y, node_out))
            return true;
    }

    return false;
}

bool viewport_bsp_hit_test_divider(const viewport_bsp_t *bsp,
                                   const struct panel_rect *panel_rect,
                                   int x, int y,
                                   uint8_t *node_out) {
    if (!bsp || !panel_rect || !node_out) return false;

    panel_rect_t root_rect = *panel_rect;
    return hit_divider_recursive(bsp, 0, root_rect, x, y, node_out);
}

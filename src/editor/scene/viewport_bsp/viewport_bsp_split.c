/**
 * @file viewport_bsp_split.c
 * @brief BSP tree split and close operations.
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_bsp_can_split
 *   viewport_bsp_split
 *   viewport_bsp_close
 */

#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include <string.h>

/** @brief Allocate the next free viewport slot index. */
static int alloc_viewport_slot(viewport_bsp_t *bsp) {
    /* Viewport slots are tracked by which indices appear in active leaves.
     * Find the lowest unused slot index [0, VIEWPORT_MAX_COUNT). */
    bool used[VIEWPORT_MAX_COUNT];
    memset(used, 0, sizeof(used));
    for (int i = 0; i < VIEWPORT_BSP_MAX_NODES; i++) {
        if (bsp->nodes[i].active && bsp->nodes[i].split == SPLIT_NONE) {
            if (bsp->nodes[i].viewport_index < VIEWPORT_MAX_COUNT) {
                used[bsp->nodes[i].viewport_index] = true;
            }
        }
    }
    for (int i = 0; i < VIEWPORT_MAX_COUNT; i++) {
        if (!used[i]) return i;
    }
    return -1;
}

/** @brief Recursively deactivate a subtree rooted at node_index. */
static void deactivate_subtree(viewport_bsp_t *bsp, int node_index) {
    if (node_index >= VIEWPORT_BSP_MAX_NODES) return;
    if (!bsp->nodes[node_index].active) return;

    if (bsp->nodes[node_index].split != SPLIT_NONE) {
        deactivate_subtree(bsp, 2 * node_index + 1);
        deactivate_subtree(bsp, 2 * node_index + 2);
    }
    bsp->nodes[node_index].active = false;
}

/**
 * @brief Recursively copy a subtree from temp[] at src to bsp->nodes at dst.
 *
 * Copies the node at temp[src] to bsp->nodes[dst], then recurses into
 * children if the source node is an internal (split) node.
 */
static void copy_subtree(viewport_bsp_t *bsp,
                          const viewport_bsp_node_t *temp,
                          int dst, int src) {
    if (dst >= VIEWPORT_BSP_MAX_NODES || src >= VIEWPORT_BSP_MAX_NODES) return;
    if (!temp[src].active) return;

    bsp->nodes[dst] = temp[src];

    if (temp[src].split != SPLIT_NONE) {
        copy_subtree(bsp, temp, 2 * dst + 1, 2 * src + 1);
        copy_subtree(bsp, temp, 2 * dst + 2, 2 * src + 2);
    }
}


bool viewport_bsp_can_split(const viewport_bsp_t *bsp,
                            uint8_t viewport_index) {
    if (bsp->viewport_count >= VIEWPORT_MAX_COUNT) return false;

    /* Find the leaf node for this viewport. */
    uint8_t node = viewport_bsp_find_leaf(bsp, viewport_index);
    if (node >= VIEWPORT_BSP_MAX_NODES) return false;

    /* Check children fit in the array. */
    int left = 2 * node + 1;
    int right = 2 * node + 2;
    if (left >= VIEWPORT_BSP_MAX_NODES || right >= VIEWPORT_BSP_MAX_NODES) {
        return false;
    }

    return true;
}

bool viewport_bsp_split(viewport_bsp_t *bsp, uint8_t viewport_index,
                        viewport_split_dir_t dir, bool original_first,
                        uint8_t *new_viewport) {
    if (!bsp || !new_viewport) return false;
    if (dir != SPLIT_VERTICAL && dir != SPLIT_HORIZONTAL) return false;
    if (!viewport_bsp_can_split(bsp, viewport_index)) return false;

    /* Find the leaf node. */
    uint8_t node = viewport_bsp_find_leaf(bsp, viewport_index);

    /* Allocate a new viewport slot. */
    int new_slot = alloc_viewport_slot(bsp);
    if (new_slot < 0) return false;

    int left = 2 * node + 1;
    int right = 2 * node + 2;

    /* Convert leaf to internal node. */
    bsp->nodes[node].split = dir;
    bsp->nodes[node].ratio = 0.5f;

    /* Set up children. */
    memset(&bsp->nodes[left], 0, sizeof(viewport_bsp_node_t));
    memset(&bsp->nodes[right], 0, sizeof(viewport_bsp_node_t));

    bsp->nodes[left].active = true;
    bsp->nodes[left].split = SPLIT_NONE;
    bsp->nodes[right].active = true;
    bsp->nodes[right].split = SPLIT_NONE;

    if (original_first) {
        bsp->nodes[left].viewport_index = viewport_index;
        bsp->nodes[right].viewport_index = (uint8_t)new_slot;
    } else {
        bsp->nodes[left].viewport_index = (uint8_t)new_slot;
        bsp->nodes[right].viewport_index = viewport_index;
    }

    bsp->viewport_count++;
    *new_viewport = (uint8_t)new_slot;

    return true;
}

bool viewport_bsp_close(viewport_bsp_t *bsp, uint8_t viewport_index) {
    if (!bsp) return false;
    if (bsp->viewport_count <= 1) return false;

    /* Find the leaf node. */
    uint8_t node = viewport_bsp_find_leaf(bsp, viewport_index);
    if (node >= VIEWPORT_BSP_MAX_NODES) return false;

    /* Root leaf (only viewport) — can't close. */
    if (node == 0) return false;

    /* Find parent and sibling. */
    int parent = ((int)node - 1) / 2;
    int left = 2 * parent + 1;
    int right = 2 * parent + 2;
    int sibling = (node == left) ? right : left;

    /* Save the entire tree, then deactivate the parent subtree. */
    viewport_bsp_node_t temp[VIEWPORT_BSP_MAX_NODES];
    memcpy(temp, bsp->nodes, sizeof(temp));
    deactivate_subtree(bsp, parent);

    /* Promote sibling subtree into parent position. */
    copy_subtree(bsp, temp, parent, sibling);

    bsp->viewport_count--;

    /* If closed viewport was focused, move focus to the first active leaf. */
    if (bsp->focused_viewport == viewport_index) {
        for (int i = 0; i < VIEWPORT_BSP_MAX_NODES; i++) {
            if (bsp->nodes[i].active && bsp->nodes[i].split == SPLIT_NONE) {
                bsp->focused_viewport = bsp->nodes[i].viewport_index;
                break;
            }
        }
    }

    return true;
}

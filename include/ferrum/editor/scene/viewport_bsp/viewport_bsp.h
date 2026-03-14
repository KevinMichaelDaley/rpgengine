/**
 * @file viewport_bsp.h
 * @brief BSP tree for tiled multi-viewport layout.
 *
 * A binary space partition tree manages viewport tiling within the
 * editor's viewport panel.  Each leaf node is an independent viewport;
 * each internal node is a horizontal or vertical split with a
 * configurable ratio.
 *
 * The tree is stored as a flat implicit-index array (parent of i is
 * (i-1)/2; children of i are 2i+1 and 2i+2).  Maximum depth is 3,
 * giving up to 8 leaf viewports in 15 nodes.
 *
 * Ownership: caller owns viewport_bsp_t; no dynamic allocation.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: split/close return false on failure.
 * Side effects: none (pure layout state).
 *
 * Public types: viewport_bsp_node_t, viewport_bsp_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_VIEWPORT_BSP_H
#define FERRUM_EDITOR_SCENE_VIEWPORT_BSP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration. */
struct panel_rect;

/** Maximum number of BSP nodes (5 levels deep). */
#define VIEWPORT_BSP_MAX_NODES 31

/** Maximum number of viewports (leaf nodes). */
#define VIEWPORT_MAX_COUNT 16

/** Minimum viewport dimension in pixels. */
#define VIEWPORT_MIN_SIZE 60

/**
 * @brief Split direction for an internal BSP node.
 */
typedef enum viewport_split_dir {
    SPLIT_NONE       = 0,   /**< Leaf node (no split). */
    SPLIT_VERTICAL   = 1,   /**< Left/right children. */
    SPLIT_HORIZONTAL = 2    /**< Top/bottom children. */
} viewport_split_dir_t;

/**
 * @brief A single BSP tree node.
 *
 * Leaf nodes have split == SPLIT_NONE and a valid viewport_index.
 * Internal nodes have a split direction and ratio.
 */
typedef struct viewport_bsp_node {
    viewport_split_dir_t split;           /**< SPLIT_NONE = leaf. */
    float                ratio;           /**< Split position [0,1]. */
    uint8_t              viewport_index;  /**< For leaves: viewport slot. */
    bool                 active;          /**< True if this node is in use. */
} viewport_bsp_node_t;

/**
 * @brief BSP tree for viewport tiling.
 *
 * Flat implicit-index array.  Root is nodes[0].
 */
typedef struct viewport_bsp {
    viewport_bsp_node_t nodes[VIEWPORT_BSP_MAX_NODES];
    uint8_t             focused_viewport;  /**< Index of focused viewport. */
    uint8_t             viewport_count;    /**< Number of active leaves. */
} viewport_bsp_t;

/* ---- Lifecycle (viewport_bsp_init.c) ---- */

/**
 * @brief Initialize BSP tree with a single viewport leaf at root.
 * @param bsp  BSP tree to initialize (non-NULL).
 */
void viewport_bsp_init(viewport_bsp_t *bsp);

/**
 * @brief Count the number of active leaf nodes.
 * @param bsp  BSP tree (non-NULL).
 * @return Number of active leaf viewports.
 */
uint8_t viewport_bsp_leaf_count(const viewport_bsp_t *bsp);

/**
 * @brief Find the BSP node index for a given viewport slot.
 * @param bsp             BSP tree (non-NULL).
 * @param viewport_index  Viewport slot to find.
 * @return Node index, or VIEWPORT_BSP_MAX_NODES if not found.
 */
uint8_t viewport_bsp_find_leaf(const viewport_bsp_t *bsp,
                               uint8_t viewport_index);

/* ---- Splitting (viewport_bsp_split.c) ---- */

/**
 * @brief Check whether a viewport can be split.
 * @param bsp             BSP tree (non-NULL).
 * @param viewport_index  Viewport to split.
 * @return true if the viewport exists and children fit.
 */
bool viewport_bsp_can_split(const viewport_bsp_t *bsp,
                            uint8_t viewport_index);

/**
 * @brief Split a viewport leaf into two children.
 *
 * The original viewport is placed in the child indicated by
 * original_first: true = first child (left or top), false = second
 * child (right or bottom).
 *
 * @param bsp             BSP tree (non-NULL).
 * @param viewport_index  Viewport to split.
 * @param dir             Split direction (VERTICAL or HORIZONTAL).
 * @param original_first  Whether original goes in the first child.
 * @param new_viewport    [out] Index of the newly created viewport slot.
 * @return true on success, false if cannot split.
 */
bool viewport_bsp_split(viewport_bsp_t *bsp, uint8_t viewport_index,
                        viewport_split_dir_t dir, bool original_first,
                        uint8_t *new_viewport);

/**
 * @brief Close a viewport, collapsing its parent split.
 *
 * The sibling viewport is promoted to the parent's position.
 * Cannot close the last remaining viewport.
 *
 * @param bsp             BSP tree (non-NULL).
 * @param viewport_index  Viewport to close.
 * @return true on success, false if cannot close.
 */
bool viewport_bsp_close(viewport_bsp_t *bsp, uint8_t viewport_index);

/* ---- Layout (viewport_bsp_layout.c) ---- */

/**
 * @brief Compute pixel rectangles for all leaf viewports.
 *
 * Walks the BSP tree and assigns a panel_rect_t to each leaf.
 * The rects array must have VIEWPORT_MAX_COUNT entries; only
 * active viewport slots are written.
 *
 * @param bsp         BSP tree (non-NULL).
 * @param panel_rect  Bounding rectangle of the viewport panel.
 * @param rects_out   Output array [VIEWPORT_MAX_COUNT] of rects.
 */
void viewport_bsp_compute_rects(const viewport_bsp_t *bsp,
                                const struct panel_rect *panel_rect,
                                struct panel_rect *rects_out);

/* ---- Hit testing (viewport_bsp_hit.c) ---- */

/**
 * @brief Determine which viewport contains a screen point.
 *
 * @param bsp         BSP tree (non-NULL).
 * @param rects       Pre-computed viewport rects [VIEWPORT_MAX_COUNT].
 * @param x           Screen X coordinate.
 * @param y           Screen Y coordinate.
 * @param hit_out     [out] Viewport index that was hit.
 * @return true if a viewport was hit.
 */
bool viewport_bsp_hit_test_viewport(const viewport_bsp_t *bsp,
                                    const struct panel_rect *rects,
                                    int x, int y,
                                    uint8_t *hit_out);

/**
 * @brief Determine which BSP divider is near a screen point.
 *
 * @param bsp         BSP tree (non-NULL).
 * @param panel_rect  Bounding rectangle of the viewport panel.
 * @param x           Screen X coordinate.
 * @param y           Screen Y coordinate.
 * @param node_out    [out] BSP node index of the divider.
 * @return true if a divider was hit.
 */
bool viewport_bsp_hit_test_divider(const viewport_bsp_t *bsp,
                                   const struct panel_rect *panel_rect,
                                   int x, int y,
                                   uint8_t *node_out);

/* ---- Divider drag (viewport_bsp_drag.c) ---- */

/**
 * @brief Drag a BSP divider by a pixel delta.
 *
 * Adjusts the split ratio of the given internal node, clamped so
 * both children maintain at least VIEWPORT_MIN_SIZE pixels.
 *
 * @param bsp          BSP tree (non-NULL).
 * @param node_index   BSP internal node to drag.
 * @param delta        Pixel delta (positive = right/down).
 * @param total_size   Total dimension along the split axis (pixels).
 */
void viewport_bsp_drag_divider(viewport_bsp_t *bsp, uint8_t node_index,
                               int delta, int total_size);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_VIEWPORT_BSP_H */

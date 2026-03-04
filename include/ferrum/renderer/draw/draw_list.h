#ifndef FERRUM_RENDERER_DRAW_LIST_H
#define FERRUM_RENDERER_DRAW_LIST_H

/**
 * @file draw_list.h
 * @brief Flat array of draw commands sorted by 64-bit sort keys.
 *
 * Each render pass operates on a draw_list_t.  Commands are pushed
 * during scene traversal and sorted before dispatch to minimize GPU
 * state changes (shader > material > mesh > depth).
 *
 * Capacity is fixed at init time (single malloc).  The list is
 * cleared each frame and reused.
 *
 * @note Ownership: the draw_list_t owns its command array.
 *       Destroy via draw_list_destroy().
 * @note Thread safety: not thread-safe.
 */

#include <stdint.h>

#include "ferrum/renderer/draw/draw_sort.h"
#include "ferrum/renderer/mesh/mesh_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ─────────────────────────────────────────────────── */

enum {
    DRAW_LIST_OK          = 0, /**< Success. */
    DRAW_LIST_ERR_INVALID = 1, /**< NULL or invalid parameter. */
    DRAW_LIST_ERR_FULL    = 2, /**< No room for another command. */
    DRAW_LIST_ERR_OOM     = 3  /**< Allocation failure. */
};

/* ── Types ────────────────────────────────────────────────────────── */

/**
 * @brief Single draw command in a draw list.
 */
typedef struct draw_command {
    draw_sort_key_t sort_key;         /**< Packed sort key. */
    mesh_handle_t   mesh;             /**< Mesh registry handle. */
    uint32_t        submesh_index;    /**< Submesh to draw. */
    uint32_t        instance_offset;  /**< Offset into per-instance UBO. */
    uint32_t        instance_count;   /**< 1 for non-batched, N for instanced. */
} draw_command_t;

/**
 * @brief Flat array of draw commands for a single render pass.
 */
typedef struct draw_list {
    draw_command_t *commands;   /**< Heap-allocated command array. */
    uint32_t        count;      /**< Number of commands currently stored. */
    uint32_t        capacity;   /**< Maximum commands before full. */
} draw_list_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

/**
 * @brief Initialize a draw list with the given capacity.
 *
 * @param list      List to initialize (non-NULL).
 * @param capacity  Maximum commands (> 0).
 * @return Status code.
 */
int draw_list_init(draw_list_t *list, uint32_t capacity);

/**
 * @brief Destroy the draw list, freeing the command array.
 *
 * Safe to call with NULL.
 *
 * @param list  List to destroy (NULL-safe).
 */
void draw_list_destroy(draw_list_t *list);

/* ── Operations ───────────────────────────────────────────────────── */

/**
 * @brief Append a draw command to the list.
 *
 * @param list  List (non-NULL).
 * @param cmd   Command to copy in (non-NULL).
 * @return Status code.
 */
int draw_list_push(draw_list_t *list, const draw_command_t *cmd);

/**
 * @brief Reset the command count to zero (does not free memory).
 *
 * @param list  List (non-NULL, NULL is a no-op).
 */
void draw_list_clear(draw_list_t *list);

/**
 * @brief Sort commands by sort key (ascending) using radix sort.
 *
 * O(n) 8-pass radix sort on the 64-bit key.  Stable.
 * Uses a temporary scratch buffer allocated alongside the command array.
 *
 * @param list  List (non-NULL, NULL or empty is a no-op).
 */
void draw_list_sort(draw_list_t *list);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_DRAW_LIST_H */

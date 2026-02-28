/**
 * @file mesh_undo.h
 * @brief Mesh undo/redo system using full-slot snapshots.
 *
 * Types: mesh_undo_stack_t (undo/redo stack).
 *
 * Each push saves a full copy of the mesh_slot_t state. Undo restores
 * the most recent snapshot. Redo re-applies after undo.
 *
 * For editor meshes (<64K vertices), full snapshots are practical.
 *
 * Ownership: stack owns its snapshot memory.
 * Nullability: NULL handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_UNDO_H
#define FERRUM_EDITOR_MESH_UNDO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Maximum undo depth. */
#define MESH_UNDO_MAX 64

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Snapshot of a mesh slot state.
 *
 * Stores copies of all vertex/index buffers.
 */
typedef struct mesh_undo_snapshot {
    float    *positions;    /**< Copy of positions buffer. */
    float    *normals;      /**< Copy of normals buffer. */
    uint32_t *indices;      /**< Copy of index buffer. */
    uint16_t *polygroup_ids;/**< Copy of polygroup IDs. */
    uint32_t  vertex_count; /**< Vertex count at snapshot time. */
    uint32_t  index_count;  /**< Index count at snapshot time. */
} mesh_undo_snapshot_t;

/**
 * @brief Undo/redo stack for mesh operations.
 *
 * Circular buffer of snapshots. Pushing beyond MESH_UNDO_MAX
 * discards the oldest snapshot.
 */
typedef struct mesh_undo_stack {
    mesh_undo_snapshot_t entries[MESH_UNDO_MAX]; /**< Snapshot ring. */
    int32_t cursor;  /**< Current position (-1 = empty). */
    int32_t count;   /**< Total entries in stack. */
} mesh_undo_stack_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize an undo stack to empty.
 * @param stack  Stack. Not NULL.
 */
void mesh_undo_stack_init(mesh_undo_stack_t *stack);

/**
 * @brief Free all snapshot memory.
 * @param stack  Stack. NULL is safe.
 */
void mesh_undo_stack_destroy(mesh_undo_stack_t *stack);

/* ------------------------------------------------------------------------ */
/* Operations                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Push current mesh state as an undo snapshot.
 *
 * Invalidates any redo entries above the cursor.
 *
 * @param stack  Undo stack. Not NULL.
 * @param slot   Current mesh state. Not NULL.
 */
void mesh_undo_push(mesh_undo_stack_t *stack, const mesh_slot_t *slot);

/**
 * @brief Undo: restore the previous mesh state.
 *
 * Moves cursor back and restores the snapshot into @p slot.
 *
 * @param stack  Undo stack. Not NULL.
 * @param slot   Mesh to restore into. Not NULL.
 * @return true if undo was performed, false if nothing to undo.
 */
bool mesh_undo(mesh_undo_stack_t *stack, mesh_slot_t *slot);

/**
 * @brief Redo: re-apply the next mesh state.
 *
 * Moves cursor forward and restores the snapshot into @p slot.
 *
 * @param stack  Undo stack. Not NULL.
 * @param slot   Mesh to restore into. Not NULL.
 * @return true if redo was performed, false if nothing to redo.
 */
bool mesh_redo(mesh_undo_stack_t *stack, mesh_slot_t *slot);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_UNDO_H */

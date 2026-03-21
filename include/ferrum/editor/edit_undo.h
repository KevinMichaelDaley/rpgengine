/**
 * @file edit_undo.h
 * @brief Editor undo/redo stack with snapshot arena.
 *
 * Records forward/inverse command pairs at mutation-drain time.
 * Supports grouped undo (multiple commands reversed atomically).
 * Entity snapshots for delete-undo are stored in a dedicated arena.
 *
 * Thread safety: all functions must be called from the main tick thread only.
 */
#ifndef FERRUM_EDITOR_EDIT_UNDO_H
#define FERRUM_EDITOR_EDIT_UNDO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/** @brief Default undo stack capacity (number of entries). */
#define EDIT_UNDO_DEFAULT_CAP       4096

/** @brief Default snapshot arena size (16 MB). */
#define EDIT_UNDO_DEFAULT_ARENA_MB  16

/** @brief Group ID indicating no group (standalone command). */
#define EDIT_UNDO_NO_GROUP          0

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single undo/redo entry.
 *
 * Stores the forward command (what was done), the inverse command
 * (how to undo it), an optional group ID, and an optional entity
 * snapshot for reconstructing deleted entities.
 */
typedef struct edit_undo_entry {
    uint32_t forward_type;      /**< Forward command type tag. */
    uint32_t inverse_type;      /**< Inverse command type tag. */
    uint32_t group_id;          /**< Group ID (0 = ungrouped). */
    uint32_t entity_id;         /**< Entity this command affected. */
    uint32_t sub_index;         /**< Sub-resource index (bone index for bone ops, 0 otherwise). */
    void    *snapshot_data;     /**< Pointer into snapshot arena (or NULL). */
    uint32_t snapshot_size;     /**< Size of snapshot data in bytes. */

    /* Compact delta storage for move/rotate (avoids snapshot for simple ops). */
    float    delta[4];          /**< Position delta (xyz) or rotation delta (xyzw). */
} edit_undo_entry_t;

/**
 * @brief Undo/redo stack with dedicated snapshot arena.
 *
 * Implemented as a ring buffer. When the ring wraps, the oldest entries
 * are silently discarded. Snapshot arena space is reclaimed when entries
 * are evicted.
 *
 * Ownership:
 * - The stack owns its entries array and snapshot arena.
 * - init() allocates; destroy() frees.
 */
typedef struct edit_undo_stack {
    edit_undo_entry_t *entries;     /**< Ring buffer of entries. */
    uint32_t           capacity;    /**< Number of slots. */
    uint32_t           base;        /**< Oldest valid entry index (ring offset). */
    uint32_t           top;         /**< Next write position (ring offset). */
    uint32_t           cursor;      /**< Current position for redo. */

    /* Snapshot arena (bump allocator). */
    uint8_t           *arena_buf;   /**< Arena backing memory. */
    size_t             arena_cap;   /**< Total arena capacity. */
    size_t             arena_used;  /**< Current arena usage. */

    /* Group tracking. */
    uint32_t           next_group;  /**< Next group ID to assign. */
    uint32_t           active_group;/**< Current active group (0 = none). */
} edit_undo_stack_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the undo stack.
 *
 * @param stack       Stack to initialize.
 * @param capacity    Number of undo entries (will be used directly, not rounded).
 * @param arena_size  Size of snapshot arena in bytes.
 * @return true on success, false on allocation failure.
 */
bool edit_undo_init(edit_undo_stack_t *stack, uint32_t capacity, size_t arena_size);

/**
 * @brief Free all memory owned by the stack.
 * @param stack  Stack to destroy.
 */
void edit_undo_destroy(edit_undo_stack_t *stack);

/**
 * @brief Clear the stack (reset to empty without freeing memory).
 * @param stack  Stack to clear.
 */
void edit_undo_clear(edit_undo_stack_t *stack);

/* ------------------------------------------------------------------------ */
/* Recording                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Record an undo entry.
 *
 * Truncates any redo-able entries above cursor (standard undo behavior:
 * new action after undo discards the redo history).
 *
 * @param stack          Stack to record into.
 * @param entry          Entry to record (copied).
 * @param snapshot       Optional snapshot data to copy into arena (or NULL).
 * @param snapshot_size  Size of snapshot data.
 * @return true on success, false if snapshot doesn't fit in arena.
 */
bool edit_undo_record(edit_undo_stack_t *stack, const edit_undo_entry_t *entry,
                      const void *snapshot, uint32_t snapshot_size);

/* ------------------------------------------------------------------------ */
/* Undo / Redo                                                               */
/* ------------------------------------------------------------------------ */

/**
 * @brief Peek at the next entry to undo (without modifying cursor).
 *
 * @param stack  Stack to peek.
 * @return Pointer to the entry, or NULL if nothing to undo.
 */
const edit_undo_entry_t *edit_undo_peek_undo(const edit_undo_stack_t *stack);

/**
 * @brief Undo one step (or one group). Moves cursor backward.
 *
 * For grouped entries, undoes all entries in the group. Returns the
 * number of entries undone (0 if nothing to undo).
 *
 * The caller should iterate the returned count, calling peek before
 * each undo to get the inverse command. This function just moves
 * the cursor; the caller applies the inverse.
 *
 * @param stack  Stack to undo from.
 * @return Number of entries undone.
 */
uint32_t edit_undo_step(edit_undo_stack_t *stack);

/**
 * @brief Peek at the next entry to redo (without modifying cursor).
 *
 * @param stack  Stack to peek.
 * @return Pointer to the entry, or NULL if nothing to redo.
 */
const edit_undo_entry_t *edit_undo_peek_redo(const edit_undo_stack_t *stack);

/**
 * @brief Redo one step (or one group). Moves cursor forward.
 *
 * @param stack  Stack to redo from.
 * @return Number of entries redone.
 */
uint32_t edit_undo_redo(edit_undo_stack_t *stack);

/* ------------------------------------------------------------------------ */
/* Groups                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * @brief Begin a new undo group.
 *
 * All entries recorded until end_group share the same group_id.
 * Groups can be nested (inner group becomes part of outer group).
 *
 * @param stack  Stack to begin group on.
 * @return The group ID assigned.
 */
uint32_t edit_undo_begin_group(edit_undo_stack_t *stack);

/**
 * @brief End the current undo group.
 * @param stack  Stack to end group on.
 */
void edit_undo_end_group(edit_undo_stack_t *stack);

/* ------------------------------------------------------------------------ */
/* Query                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Return the number of undo-able entries.
 * @param stack  Stack to query.
 */
uint32_t edit_undo_count(const edit_undo_stack_t *stack);

/**
 * @brief Return the number of redo-able entries.
 * @param stack  Stack to query.
 */
uint32_t edit_undo_redo_count(const edit_undo_stack_t *stack);

/**
 * @brief Access an entry by absolute index (base-relative).
 *
 * @param stack      Stack to query.
 * @param abs_index  Absolute index (must be in [base, top)).
 * @return Pointer to the entry, or NULL if out of range.
 */
const edit_undo_entry_t *edit_undo_entry_at(const edit_undo_stack_t *stack,
                                             uint32_t abs_index);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_UNDO_H */

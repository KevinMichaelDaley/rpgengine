/**
 * @file undo_rebase.h
 * @brief Branching undo: rebase displaced entries on new edits.
 *
 * When a new edit is recorded after an undo, displaced redo entries
 * are checked for conflicts with the new edit. Non-conflicting entries
 * are rebased (appended after the new edit). Conflicting entries are
 * moved to orphan branches for manual recovery.
 *
 * Thread safety: must be called from the main tick thread only.
 */
#ifndef FERRUM_EDITOR_UNDO_REBASE_H
#define FERRUM_EDITOR_UNDO_REBASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct edit_undo_stack;
struct edit_undo_entry;

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/** @brief Default maximum orphan branches. */
#define UNDO_BRANCHES_DEFAULT_MAX      16

/** @brief Default maximum entries per orphan branch. */
#define UNDO_BRANCH_DEFAULT_ENTRY_MAX  64

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single orphan branch — displaced entries that conflicted.
 *
 * Entries in an orphan branch are read-only; they can be inspected
 * via :undo tree but not directly re-applied.
 */
typedef struct undo_branch {
    struct edit_undo_entry *entries;  /**< Array of displaced entries. */
    uint32_t count;                  /**< Number of entries. */
    uint32_t capacity;               /**< Allocated capacity. */
    uint32_t diverge_id;             /**< Undo stack position where branch diverged. */
} undo_branch_t;

/**
 * @brief Collection of orphan branches.
 *
 * Fixed-capacity ring: when full, the oldest branch is overwritten.
 *
 * Ownership:
 * - init() allocates; destroy() frees.
 * - Snapshot data pointers in orphan entries may be stale (arena was
 *   compacted); callers should not dereference snapshot_data in orphans.
 */
typedef struct undo_branches {
    undo_branch_t *branches;       /**< Array of branches. */
    uint32_t       max_branches;   /**< Capacity. */
    uint32_t       count;          /**< Current number of branches. */
    uint32_t       head;           /**< Next write position (ring). */
    uint32_t       entry_cap;      /**< Max entries per branch. */
} undo_branches_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize orphan branch storage.
 *
 * @param branches        Storage to initialize.
 * @param max_branches    Maximum number of orphan branches.
 * @param max_entries     Maximum entries per branch.
 * @return true on success, false on allocation failure or NULL.
 */
bool undo_branches_init(undo_branches_t *branches,
                         uint32_t max_branches, uint32_t max_entries);

/**
 * @brief Free all memory owned by the branch storage.
 * @param branches  Storage to destroy (NULL is safe).
 */
void undo_branches_destroy(undo_branches_t *branches);

/* ------------------------------------------------------------------------ */
/* Query                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Return the number of active orphan branches.
 * @param branches  Storage to query (NULL returns 0).
 */
uint32_t undo_branches_count(const undo_branches_t *branches);

/**
 * @brief Get an orphan branch by index (0 = oldest).
 *
 * @param branches  Storage to query.
 * @param index     Branch index (0..count-1).
 * @return Pointer to branch, or NULL if out of range.
 */
const undo_branch_t *undo_branches_get(const undo_branches_t *branches,
                                        uint32_t index);

/* ------------------------------------------------------------------------ */
/* Rebase-aware recording                                                    */
/* ------------------------------------------------------------------------ */

/**
 * @brief Record an undo entry with rebase of displaced redo entries.
 *
 * Instead of truncating redo entries (the default behavior of
 * edit_undo_record), this function:
 * 1. Saves displaced entries (cursor..top).
 * 2. Records the new entry normally.
 * 3. For each displaced entry, checks conflict against the new entry.
 * 4. Non-conflicting entries are re-recorded (rebased).
 * 5. Conflicting entries are moved to a new orphan branch.
 *
 * If branches is NULL, falls back to normal edit_undo_record (truncate).
 *
 * @param stack          Undo stack.
 * @param entry          New entry to record.
 * @param snapshot       Optional snapshot data.
 * @param snapshot_size  Size of snapshot.
 * @param branches       Orphan branch storage (NULL = truncate mode).
 * @return true on success.
 */
bool edit_undo_record_rebase(struct edit_undo_stack *stack,
                              const struct edit_undo_entry *entry,
                              const void *snapshot, uint32_t snapshot_size,
                              undo_branches_t *branches);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UNDO_REBASE_H */

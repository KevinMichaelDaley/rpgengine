/**
 * @file edit_selection.h
 * @brief Editor entity selection system.
 *
 * Maintains a set of selected entity IDs. Commands operate on the
 * current selection. Selection state is pushed to connected clients
 * for highlight rendering.
 *
 * Implementation uses a sorted array for fast iteration and
 * reasonable lookup. Max 4096 entities.
 *
 * Thread safety: only mutated from the main tick thread during drain.
 */
#ifndef FERRUM_EDITOR_EDIT_SELECTION_H
#define FERRUM_EDITOR_EDIT_SELECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/** @brief Maximum number of simultaneously selected entities. */
#define EDIT_SELECTION_MAX  4096

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Selection set — sorted array of entity IDs.
 *
 * Ownership: init() allocates, destroy() frees.
 */
typedef struct edit_selection {
    uint32_t *ids;      /**< Sorted array of selected entity IDs. */
    uint32_t  count;    /**< Number of selected entities. */
    uint32_t  capacity; /**< Allocated capacity. */
    uint32_t  version;  /**< Increments on every change (for dirty tracking). */
} edit_selection_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the selection.
 * @param sel  Selection to initialize.
 * @return true on success.
 */
bool edit_selection_init(edit_selection_t *sel);

/**
 * @brief Free selection memory.
 * @param sel  Selection to destroy.
 */
void edit_selection_destroy(edit_selection_t *sel);

/* ------------------------------------------------------------------------ */
/* Mutation                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Add an entity to the selection. No-op if already selected.
 * @param sel  Selection.
 * @param id   Entity ID to add.
 * @return true if added, false if already present or full.
 */
bool edit_selection_add(edit_selection_t *sel, uint32_t id);

/**
 * @brief Remove an entity from the selection. No-op if not selected.
 * @param sel  Selection.
 * @param id   Entity ID to remove.
 * @return true if removed, false if not found.
 */
bool edit_selection_remove(edit_selection_t *sel, uint32_t id);

/**
 * @brief Toggle an entity in the selection (add if absent, remove if present).
 * @param sel  Selection.
 * @param id   Entity ID to toggle.
 * @return true if now selected, false if now deselected.
 */
bool edit_selection_toggle(edit_selection_t *sel, uint32_t id);

/**
 * @brief Clear the entire selection.
 * @param sel  Selection.
 */
void edit_selection_clear(edit_selection_t *sel);

/* ------------------------------------------------------------------------ */
/* Query                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Check if an entity is selected.
 * @param sel  Selection.
 * @param id   Entity ID to check.
 * @return true if selected.
 */
bool edit_selection_contains(const edit_selection_t *sel, uint32_t id);

/**
 * @brief Get the number of selected entities.
 * @param sel  Selection.
 * @return Count of selected entities.
 */
uint32_t edit_selection_count(const edit_selection_t *sel);

/**
 * @brief Get the array of selected entity IDs (sorted).
 * @param sel  Selection.
 * @return Pointer to sorted ID array, or NULL if empty.
 */
const uint32_t *edit_selection_ids(const edit_selection_t *sel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SELECTION_H */

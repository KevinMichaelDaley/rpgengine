/**
 * @file scene_outliner.h
 * @brief Scene outliner flat display list built from LCRS tree.
 *
 * Provides a pre-built array of entries for the outliner UI to render.
 * Each entry has an indent level, entity ID, and expand/collapse state.
 * Rebuilt when the tree structure or expand state changes.
 *
 * Thread safety: must be called from the main render thread only.
 */
#ifndef FERRUM_EDITOR_SCENE_OUTLINER_H
#define FERRUM_EDITOR_SCENE_OUTLINER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum entries in the outliner display list. */
#define SCENE_OUTLINER_MAX_ENTRIES 2048

/**
 * @brief A single entry in the outliner flat display list.
 */
typedef struct scene_outliner_entry {
    uint32_t entity_id;    /**< Entity ID. */
    uint32_t indent;       /**< Indentation depth (0 = root). */
    bool     has_children; /**< True if entity has children in tree. */
    bool     expanded;     /**< True if children are visible. */
} scene_outliner_entry_t;

/* Forward declarations. */
struct edit_entity_store;
struct scene_editor;

/**
 * @brief Build the flat outliner display list from the LCRS tree.
 *
 * Performs DFS from all root entities, skipping collapsed subtrees.
 * Only includes active (non-pending-delete) entities.
 *
 * @param entries    Output array (must have SCENE_OUTLINER_MAX_ENTRIES capacity).
 * @param store      Entity store with LCRS tree.
 * @param expanded   Per-entity expanded state array (capacity-sized).
 * @param exp_cap    Capacity of expanded array.
 * @return Number of entries written.
 */
uint32_t scene_outliner_build(scene_outliner_entry_t *entries,
                               const struct edit_entity_store *store,
                               const bool *expanded, uint32_t exp_cap);

/**
 * @brief Toggle expand/collapse state for an entity.
 *
 * @param expanded  Per-entity expanded array.
 * @param exp_cap   Capacity.
 * @param entity_id Entity to toggle.
 */
void scene_outliner_toggle_expand(bool *expanded, uint32_t exp_cap,
                                    uint32_t entity_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_OUTLINER_H */

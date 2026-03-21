/**
 * @file edit_bone_selection.h
 * @brief Bone selection state for skeleton editing.
 *
 * Tracks which bones (by index) are selected within a single entity's
 * skeleton. All selected bones must belong to the same entity; switching
 * entities clears the bone selection.
 *
 * Public types: edit_bone_selection_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_EDIT_BONE_SELECTION_H
#define FERRUM_EDITOR_EDIT_BONE_SELECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum number of simultaneously selected bones. */
#define EDIT_BONE_SEL_MAX 256

/** @brief Sentinel value: no entity has bone selection. */
#define EDIT_BONE_SEL_NONE UINT32_MAX

/**
 * @brief Bone selection state.
 *
 * All selected bones belong to entity_id. Adding a bone from a
 * different entity clears the existing selection first.
 */
typedef struct edit_bone_selection {
    uint32_t entity_id;                  /**< Entity owning the skeleton. */
    uint32_t bones[EDIT_BONE_SEL_MAX];   /**< Selected bone indices. */
    uint32_t count;                      /**< Number of selected bones. */
} edit_bone_selection_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize bone selection to empty.
 * @param sel  Selection to initialize (non-NULL).
 */
void edit_bone_selection_init(edit_bone_selection_t *sel);

/**
 * @brief Destroy bone selection (no-op, stack-allocated).
 * @param sel  Selection (non-NULL).
 */
void edit_bone_selection_destroy(edit_bone_selection_t *sel);

/* ---- Mutation (edit_bone_selection.c) ---- */

/**
 * @brief Add a bone to the selection.
 *
 * If entity_id differs from the current selection's entity, the
 * existing selection is cleared first. Duplicate adds are no-ops.
 *
 * @param sel        Selection (non-NULL).
 * @param entity_id  Entity owning the skeleton.
 * @param bone_index Bone index within the skeleton.
 * @return true if added, false if already present or full.
 */
bool edit_bone_selection_add(edit_bone_selection_t *sel,
                              uint32_t entity_id, uint32_t bone_index);

/**
 * @brief Remove a bone from the selection.
 * @param sel        Selection (non-NULL).
 * @param entity_id  Entity (must match current entity_id).
 * @param bone_index Bone index to remove.
 * @return true if removed, false if not found.
 */
bool edit_bone_selection_remove(edit_bone_selection_t *sel,
                                 uint32_t entity_id, uint32_t bone_index);

/**
 * @brief Toggle a bone in the selection.
 * @param sel        Selection (non-NULL).
 * @param entity_id  Entity owning the skeleton.
 * @param bone_index Bone index to toggle.
 */
void edit_bone_selection_toggle(edit_bone_selection_t *sel,
                                 uint32_t entity_id, uint32_t bone_index);

/**
 * @brief Clear all bone selections.
 * @param sel  Selection (non-NULL).
 */
void edit_bone_selection_clear(edit_bone_selection_t *sel);

/* ---- Query (edit_bone_selection_query.c) ---- */

/**
 * @brief Check if a specific bone is selected.
 * @param sel        Selection (non-NULL).
 * @param entity_id  Entity to check.
 * @param bone_index Bone index to check.
 * @return true if the bone is selected.
 */
bool edit_bone_selection_contains(const edit_bone_selection_t *sel,
                                   uint32_t entity_id, uint32_t bone_index);

/**
 * @brief Get the number of selected bones.
 * @param sel  Selection (non-NULL).
 * @return Number of selected bones.
 */
uint32_t edit_bone_selection_count(const edit_bone_selection_t *sel);

/**
 * @brief Get the array of selected bone indices.
 * @param sel    Selection (non-NULL).
 * @param count  Output: number of bones (non-NULL).
 * @return Pointer to bone index array, or NULL if empty.
 */
const uint32_t *edit_bone_selection_bones(const edit_bone_selection_t *sel,
                                           uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_BONE_SELECTION_H */

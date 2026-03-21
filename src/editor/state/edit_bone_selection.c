/**
 * @file edit_bone_selection.c
 * @brief Bone selection state — mutation operations.
 *
 * Non-static functions (4 / 4 limit):
 *   edit_bone_selection_add
 *   edit_bone_selection_remove
 *   edit_bone_selection_toggle
 *   edit_bone_selection_clear
 */

#include "ferrum/editor/edit_bone_selection.h"
#include <string.h>

void edit_bone_selection_init(edit_bone_selection_t *sel) {
    if (!sel) return;
    sel->entity_id = EDIT_BONE_SEL_NONE;
    sel->count = 0;
}

void edit_bone_selection_destroy(edit_bone_selection_t *sel) {
    if (!sel) return;
    sel->entity_id = EDIT_BONE_SEL_NONE;
    sel->count = 0;
}

bool edit_bone_selection_add(edit_bone_selection_t *sel,
                              uint32_t entity_id, uint32_t bone_index) {
    if (!sel) return false;

    /* If switching entities, clear existing selection. */
    if (sel->entity_id != entity_id) {
        sel->count = 0;
        sel->entity_id = entity_id;
    }

    /* Check for duplicate. */
    for (uint32_t i = 0; i < sel->count; i++) {
        if (sel->bones[i] == bone_index) return false;
    }

    /* Check capacity. */
    if (sel->count >= EDIT_BONE_SEL_MAX) return false;

    sel->bones[sel->count++] = bone_index;
    return true;
}

bool edit_bone_selection_remove(edit_bone_selection_t *sel,
                                 uint32_t entity_id, uint32_t bone_index) {
    if (!sel) return false;
    if (sel->entity_id != entity_id) return false;

    for (uint32_t i = 0; i < sel->count; i++) {
        if (sel->bones[i] == bone_index) {
            /* Swap with last element for O(1) removal. */
            sel->bones[i] = sel->bones[sel->count - 1];
            sel->count--;
            /* Reset entity_id if selection is now empty. */
            if (sel->count == 0) {
                sel->entity_id = EDIT_BONE_SEL_NONE;
            }
            return true;
        }
    }
    return false;
}

void edit_bone_selection_toggle(edit_bone_selection_t *sel,
                                 uint32_t entity_id, uint32_t bone_index) {
    if (!sel) return;

    /* If same entity, try to remove first. */
    if (sel->entity_id == entity_id) {
        if (edit_bone_selection_remove(sel, entity_id, bone_index)) {
            return;
        }
    }

    edit_bone_selection_add(sel, entity_id, bone_index);
}

void edit_bone_selection_clear(edit_bone_selection_t *sel) {
    if (!sel) return;
    sel->entity_id = EDIT_BONE_SEL_NONE;
    sel->count = 0;
}

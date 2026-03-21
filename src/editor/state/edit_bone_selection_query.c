/**
 * @file edit_bone_selection_query.c
 * @brief Bone selection state — query operations.
 *
 * Non-static functions (3 / 4 limit):
 *   edit_bone_selection_contains
 *   edit_bone_selection_count
 *   edit_bone_selection_bones
 */

#include "ferrum/editor/edit_bone_selection.h"

#include <stddef.h>

bool edit_bone_selection_contains(const edit_bone_selection_t *sel,
                                   uint32_t entity_id, uint32_t bone_index) {
    if (!sel) return false;
    if (sel->entity_id != entity_id) return false;

    for (uint32_t i = 0; i < sel->count; i++) {
        if (sel->bones[i] == bone_index) return true;
    }
    return false;
}

uint32_t edit_bone_selection_count(const edit_bone_selection_t *sel) {
    if (!sel) return 0;
    return sel->count;
}

const uint32_t *edit_bone_selection_bones(const edit_bone_selection_t *sel,
                                           uint32_t *count) {
    if (!sel || !count) return NULL;
    *count = sel->count;
    if (sel->count == 0) return NULL;
    return sel->bones;
}

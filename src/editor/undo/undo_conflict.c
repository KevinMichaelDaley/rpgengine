/**
 * @file undo_conflict.c
 * @brief Undo conflict key extraction and comparison.
 *
 * Non-static functions (2 / 4 limit):
 *   undo_conflict_key_extract
 *   undo_conflict_check
 */

#include "ferrum/editor/undo_conflict.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include <string.h>

undo_conflict_key_t undo_conflict_key_extract(const edit_undo_entry_t *entry) {
    undo_conflict_key_t key;
    memset(&key, 0, sizeof(key));
    if (!entry) return key;

    key.entity_id = entry->entity_id;

    /* Bone operations use entity_id + bone_index as compound key. */
    if (entry->forward_type == EDIT_CMD_TYPE_BONE_MOVE ||
        entry->forward_type == EDIT_CMD_TYPE_BONE_ROTATE) {
        key.key_type  = 1; /* Bone-level. */
        key.sub_index = entry->sub_index;
    } else {
        key.key_type  = 0; /* Entity-level. */
        key.sub_index = 0;
    }
    return key;
}

bool undo_conflict_check(const undo_conflict_key_t *a,
                          const undo_conflict_key_t *b) {
    if (!a || !b) return false;

    /* Different key types don't conflict (entity ops vs bone ops). */
    if (a->key_type != b->key_type) return false;

    /* Same entity required. */
    if (a->entity_id != b->entity_id) return false;

    /* For bone-level keys, also compare bone index. */
    if (a->key_type == 1) {
        return a->sub_index == b->sub_index;
    }

    return true;
}

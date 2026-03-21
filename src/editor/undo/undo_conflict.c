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
#include <string.h>

undo_conflict_key_t undo_conflict_key_extract(const edit_undo_entry_t *entry) {
    undo_conflict_key_t key;
    memset(&key, 0, sizeof(key));
    if (!entry) return key;

    /* All current command types use entity_id as the conflict key. */
    key.entity_id = entry->entity_id;
    key.key_type  = 0; /* Entity-level. */
    return key;
}

bool undo_conflict_check(const undo_conflict_key_t *a,
                          const undo_conflict_key_t *b) {
    if (!a || !b) return false;

    /* Same key_type and same entity_id = conflict. */
    if (a->key_type != b->key_type) return false;
    return a->entity_id == b->entity_id;
}

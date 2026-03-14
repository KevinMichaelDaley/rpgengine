/**
 * @file mode_object_ops.c
 * @brief Object mode duplicate and delete operations.
 *
 * Non-static functions: 2 (duplicate, delete).
 */

#include "ferrum/editor/mode/mode_object.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include <string.h>

void object_mode_duplicate(struct edit_entity_store *store,
                             struct edit_selection *sel) {
    uint32_t count = edit_selection_count(sel);
    const uint32_t *ids = edit_selection_ids(sel);
    if (!ids || count == 0) return;

    /* Collect IDs to duplicate (copy since we'll mutate selection). */
    uint32_t src_ids[4096];
    if (count > 4096) count = 4096;
    memcpy(src_ids, ids, count * sizeof(uint32_t));

    /* Clear selection, will be repopulated with duplicates. */
    edit_selection_clear(sel);

    for (uint32_t i = 0; i < count; i++) {
        const edit_entity_t *src = edit_entity_store_get(store, src_ids[i]);
        if (!src) continue;

        uint32_t new_id = edit_entity_store_create(store, src->type);
        if (new_id == EDIT_ENTITY_INVALID_ID) continue;

        edit_entity_t *dst = edit_entity_store_get_mut(store, new_id);
        /* Copy transform and properties. */
        memcpy(dst->pos, src->pos, sizeof(dst->pos));
        memcpy(dst->rot, src->rot, sizeof(dst->rot));
        dst->orientation = src->orientation;
        memcpy(dst->scale, src->scale, sizeof(dst->scale));
        memcpy(dst->pivot_offset, src->pivot_offset, sizeof(dst->pivot_offset));
        memcpy(dst->name, src->name, sizeof(dst->name));
        memcpy(dst->materials, src->materials, sizeof(dst->materials));

        edit_selection_add(sel, new_id);
    }
}

void object_mode_delete(struct edit_entity_store *store,
                          struct edit_selection *sel) {
    uint32_t count = edit_selection_count(sel);
    const uint32_t *ids = edit_selection_ids(sel);
    if (!ids || count == 0) return;

    /* Copy IDs since removing changes the selection. */
    uint32_t del_ids[4096];
    if (count > 4096) count = 4096;
    memcpy(del_ids, ids, count * sizeof(uint32_t));

    edit_selection_clear(sel);

    for (uint32_t i = 0; i < count; i++) {
        edit_entity_store_remove(store, del_ids[i]);
    }
}

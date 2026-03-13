/**
 * @file mode_object.c
 * @brief Object mode transform operations: translate, rotate, scale.
 *
 * Non-static functions: 3 (translate, rotate, scale).
 */

#include "ferrum/editor/mode/mode_object.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

void object_mode_translate(struct edit_entity_store *store,
                            const struct edit_selection *sel,
                            const float delta[3]) {
    uint32_t count = edit_selection_count(sel);
    const uint32_t *ids = edit_selection_ids(sel);
    if (!ids) return;

    for (uint32_t i = 0; i < count; i++) {
        edit_entity_t *e = edit_entity_store_get_mut(store, ids[i]);
        if (!e) continue;
        e->pos[0] += delta[0];
        e->pos[1] += delta[1];
        e->pos[2] += delta[2];
    }
}

void object_mode_rotate(struct edit_entity_store *store,
                          const struct edit_selection *sel,
                          const float delta[3]) {
    uint32_t count = edit_selection_count(sel);
    const uint32_t *ids = edit_selection_ids(sel);
    if (!ids) return;

    for (uint32_t i = 0; i < count; i++) {
        edit_entity_t *e = edit_entity_store_get_mut(store, ids[i]);
        if (!e) continue;
        e->rot[0] += delta[0];
        e->rot[1] += delta[1];
        e->rot[2] += delta[2];
    }
}

void object_mode_scale(struct edit_entity_store *store,
                        const struct edit_selection *sel,
                        const float delta[3]) {
    uint32_t count = edit_selection_count(sel);
    const uint32_t *ids = edit_selection_ids(sel);
    if (!ids) return;

    for (uint32_t i = 0; i < count; i++) {
        edit_entity_t *e = edit_entity_store_get_mut(store, ids[i]);
        if (!e) continue;
        e->scale[0] *= delta[0];
        e->scale[1] *= delta[1];
        e->scale[2] *= delta[2];
    }
}

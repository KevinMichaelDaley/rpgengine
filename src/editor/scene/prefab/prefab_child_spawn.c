/**
 * @file prefab_child_spawn.c
 * @brief Prefab child spawn tracking — pending spawn queue management.
 *
 * Non-static functions (3 / 4-function rule):
 *   1. prefab_pending_spawn_init
 *   2. prefab_pending_spawn_add
 *   3. prefab_pending_spawn_resolve
 */

#include "ferrum/editor/scene/prefab/prefab_child_spawn.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"
#include <string.h>

void prefab_pending_spawn_init(prefab_pending_spawn_t *ps) {
    if (!ps) { return; }
    memset(ps, 0, sizeof(*ps));
}

void prefab_pending_spawn_add(prefab_pending_spawn_t *ps, uint32_t cmd_id) {
    if (!ps) { return; }
    if (ps->count >= PREFAB_PENDING_SPAWN_MAX) { return; }
    ps->cmd_ids[ps->count] = cmd_id;
    ps->count++;
}

void prefab_pending_spawn_resolve(prefab_pending_spawn_t *ps,
                                   struct edit_entity_store *store,
                                   uint32_t new_entity_id,
                                   uint32_t root_entity_id) {
    if (!ps || !store) { return; }
    if (ps->count == 0) { return; }

    /* Set PARENT_ID attribute on the new entity. */
    edit_entity_t *ent = edit_entity_store_get_mut(store, new_entity_id);
    if (!ent) { return; }

    entity_attrs_set(&ent->attrs, SCRIPT_KEY_PARENT_ID, SCRIPT_ATTR_U32,
                     &root_entity_id, sizeof(uint32_t));

    /* Shift remaining entries down and decrement count. */
    ps->count--;
    if (ps->count > 0) {
        memmove(&ps->cmd_ids[0], &ps->cmd_ids[1],
                ps->count * sizeof(uint32_t));
    }
}

/**
 * @file prefab_bone_parent.c
 * @brief Parent/unparent entities to skeleton bones via entity attrs.
 *
 * Sets or removes SCRIPT_KEY_PARENT_ID and SCRIPT_KEY_BONE_INDEX on
 * entities to express bone parenting relationships.
 *
 * Non-static functions: prefab_parent_to_bone, prefab_unparent (2/4).
 */

#include "ferrum/editor/scene/prefab/prefab_bone_parent.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"

#include <string.h>

bool prefab_parent_to_bone(struct edit_entity_store *store,
                           uint32_t entity_id,
                           uint32_t root_entity_id,
                           uint32_t bone_index) {
    if (!store) return false;

    edit_entity_t *ent = edit_entity_store_get_mut(store, entity_id);
    if (!ent) return false;

    /* Set PARENT_ID = root_entity_id. */
    bool ok1 = entity_attrs_set(&ent->attrs, SCRIPT_KEY_PARENT_ID,
                                SCRIPT_ATTR_U32, &root_entity_id,
                                sizeof(uint32_t));

    /* Set BONE_INDEX = bone_index. */
    bool ok2 = entity_attrs_set(&ent->attrs, SCRIPT_KEY_BONE_INDEX,
                                SCRIPT_ATTR_U32, &bone_index,
                                sizeof(uint32_t));

    return ok1 && ok2;
}

bool prefab_unparent(struct edit_entity_store *store, uint32_t entity_id) {
    if (!store) return false;

    edit_entity_t *ent = edit_entity_store_get_mut(store, entity_id);
    if (!ent) return false;

    bool r1 = entity_attrs_remove(&ent->attrs, SCRIPT_KEY_PARENT_ID);
    bool r2 = entity_attrs_remove(&ent->attrs, SCRIPT_KEY_BONE_INDEX);

    /* Return true only if at least one attr was present. */
    return r1 || r2;
}

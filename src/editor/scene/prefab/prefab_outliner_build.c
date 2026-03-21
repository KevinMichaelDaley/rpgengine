/**
 * @file prefab_outliner_build.c
 * @brief Build the prefab outliner tree from skeleton + entity store.
 *
 * DFS traversal of the bone hierarchy, interleaving collider entities
 * that are parented to each bone.
 *
 * Non-static functions: prefab_outliner_init, prefab_outliner_build (2/4).
 */

#include "ferrum/editor/scene/prefab/prefab_outliner.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/animation/constraint_params.h"

#include <stdio.h>
#include <string.h>

/* ---- Static helpers ---- */

/**
 * @brief Read a u32 attribute from an entity's attrs block.
 * @return true if found and type matches, false otherwise.
 */
static bool read_u32_attr(const entity_attrs_t *attrs, uint16_t key,
                          uint32_t *out) {
    uint8_t type, size;
    const void *data = entity_attrs_get(attrs, key, &type, &size);
    if (!data || type != SCRIPT_ATTR_U32 || size != sizeof(uint32_t)) {
        return false;
    }
    memcpy(out, data, sizeof(uint32_t));
    return true;
}

/**
 * @brief Append collider entities parented to a given bone.
 *
 * Scans the entity store for active entities with PARENT_ID == root_id
 * and BONE_INDEX == bone_index. Appends them to the tree.
 */
static void append_bone_colliders(prefab_outliner_t *tree,
                                  const edit_entity_store_t *entities,
                                  uint32_t root_id,
                                  uint32_t bone_index,
                                  uint8_t indent) {
    uint32_t cap = entities->capacity;
    for (uint32_t i = 0; i < cap; i++) {
        if (tree->count >= PREFAB_OUTLINER_MAX_ENTRIES) break;

        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;

        /* Check if this entity is parented to the correct root + bone. */
        uint32_t parent_id = UINT32_MAX;
        uint32_t bidx = UINT32_MAX;
        if (!read_u32_attr(&ent->attrs, SCRIPT_KEY_PARENT_ID, &parent_id)) {
            continue;
        }
        if (parent_id != root_id) continue;
        if (!read_u32_attr(&ent->attrs, SCRIPT_KEY_BONE_INDEX, &bidx)) {
            continue;
        }
        if (bidx != bone_index) continue;

        /* Append collider entry. */
        prefab_outliner_entry_t *entry = &tree->entries[tree->count];
        entry->bone_index = bone_index;
        entry->indent = indent;
        entry->is_bone = false;
        entry->entity_id = i;

        /* Copy entity name or generate fallback.
         * snprintf truncation is intentional — names are capped. */
        if (ent->name[0] != '\0') {
            memset(entry->name, 0, PREFAB_OUTLINER_NAME_MAX);
            memcpy(entry->name, ent->name,
                   strlen(ent->name) < PREFAB_OUTLINER_NAME_MAX - 1
                       ? strlen(ent->name)
                       : PREFAB_OUTLINER_NAME_MAX - 1);
        } else {
            snprintf(entry->name, PREFAB_OUTLINER_NAME_MAX,
                     "entity_%u", i);
        }

        tree->count++;
    }
}

/**
 * @brief DFS visit a bone and its children.
 *
 * Appends the bone entry, then its collider entities, then recurses
 * into child bones.
 */
static void visit_bone(prefab_outliner_t *tree,
                       const skeleton_def_t *skel,
                       const edit_entity_store_t *entities,
                       uint32_t root_id,
                       uint32_t bone_index,
                       uint8_t depth) {
    if (tree->count >= PREFAB_OUTLINER_MAX_ENTRIES) return;

    /* Append bone entry. */
    prefab_outliner_entry_t *entry = &tree->entries[tree->count];
    entry->bone_index = bone_index;
    entry->indent = depth;
    entry->is_bone = true;
    entry->entity_id = UINT32_MAX;

    /* Copy bone name from skeleton. */
    if (skel->joint_names) {
        strncpy(entry->name, skel->joint_names[bone_index],
                PREFAB_OUTLINER_NAME_MAX - 1);
        entry->name[PREFAB_OUTLINER_NAME_MAX - 1] = '\0';
    } else {
        snprintf(entry->name, PREFAB_OUTLINER_NAME_MAX,
                 "bone_%u", bone_index);
    }

    tree->count++;

    /* Append collider entities for this bone. */
    append_bone_colliders(tree, entities, root_id, bone_index, depth + 1);

    /* Recurse into child bones (those whose parent == bone_index). */
    for (uint32_t c = 0; c < skel->joint_count; c++) {
        if (skel->parent_indices[c] == bone_index) {
            visit_bone(tree, skel, entities, root_id, c, depth + 1);
        }
    }
}

/* ---- Public API ---- */

void prefab_outliner_init(prefab_outliner_t *tree) {
    if (!tree) return;
    tree->count = 0;
}

void prefab_outliner_build(prefab_outliner_t *tree,
                           const struct skeleton_def *skel,
                           const struct edit_entity_store *entities,
                           uint32_t root_id) {
    if (!tree || !skel || !entities) return;

    tree->count = 0;

    if (skel->joint_count == 0) return;

    /* Find root bones (parent == UINT32_MAX) and DFS from each. */
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        if (skel->parent_indices &&
            skel->parent_indices[i] == UINT32_MAX) {
            visit_bone(tree, skel, entities, root_id, i, 0);
        }
    }
}

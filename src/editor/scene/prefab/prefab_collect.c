/**
 * @file prefab_collect.c
 * @brief Collect prefab definition from entity store.
 *
 * Scans the entity store for the root entity and its children
 * (via PARENT_ID), snapshots them into a prefab_def_t. Optionally
 * collects bone collider data if markers are present.
 *
 * Non-static functions: prefab_collect_from_entities (1/4).
 */

#include "ferrum/editor/scene/prefab/prefab_collect.h"
#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/editor/scene/prefab/prefab_hull_build.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/animation/bone_collider.h"

#include <string.h>

/* ---- Static helpers ---- */

static bool read_u32(const entity_attrs_t *attrs, uint16_t key,
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
 * @brief Snapshot an entity into a prefab_entity_snapshot_t.
 */
static void snapshot_entity(prefab_entity_snapshot_t *snap,
                            const edit_entity_t *ent,
                            const float *root_pos,
                            int32_t local_parent) {
    memset(snap, 0, sizeof(*snap));
    snap->type = ent->type;

    /* Store position relative to root. */
    snap->pos[0] = ent->pos[0] - root_pos[0];
    snap->pos[1] = ent->pos[1] - root_pos[1];
    snap->pos[2] = ent->pos[2] - root_pos[2];

    snap->rot[0] = ent->rot[0];
    snap->rot[1] = ent->rot[1];
    snap->rot[2] = ent->rot[2];

    snap->scale[0] = ent->scale[0];
    snap->scale[1] = ent->scale[1];
    snap->scale[2] = ent->scale[2];

    /* Copy name. */
    if (ent->name[0] != '\0') {
        memcpy(snap->name, ent->name, sizeof(snap->name));
    }

    snap->local_parent = local_parent;

    /* Copy attrs block directly. */
    memcpy(&snap->attrs, &ent->attrs, sizeof(entity_attrs_t));
}

/* ---- Public API ---- */

bool prefab_collect_from_entities(struct prefab_def *def,
                                 const struct edit_entity_store *entities,
                                 uint32_t root_id,
                                 uint32_t bone_count) {
    if (!def || !entities) return false;

    prefab_def_init(def);

    /* Snapshot root entity. */
    const edit_entity_t *root = edit_entity_store_get(entities, root_id);
    if (!root) return false;

    snapshot_entity(&def->entities[0], root, root->pos, -1);
    def->entity_count = 1;

    /* Find all children (entities with PARENT_ID == root_id). */
    uint32_t cap = entities->capacity;
    for (uint32_t i = 0; i < cap; i++) {
        if (i == root_id) continue;
        if (def->entity_count >= PREFAB_MAX_ENTITIES) break;

        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;

        uint32_t pid = UINT32_MAX;
        if (!read_u32(&ent->attrs, SCRIPT_KEY_PARENT_ID, &pid)) continue;
        if (pid != root_id) continue;

        snapshot_entity(&def->entities[def->entity_count], ent,
                        root->pos, 0); /* local_parent=0 (the root) */
        def->entity_count++;
    }

    /* Collect bone collider data if bone_count > 0 (skeleton present). */
    if (bone_count > 0 && bone_count <= PREFAB_MAX_BONES) {
        def->bone_count = bone_count;

        for (uint32_t b = 0; b < bone_count; b++) {
            memset(&def->bones[b], 0, sizeof(def->bones[b]));

            /* Look for a collider entity parented to this bone. */
            for (uint32_t i = 0; i < cap; i++) {
                const edit_entity_t *ent = edit_entity_store_get(entities, i);
                if (!ent) continue;
                if (ent->type == EDIT_ENTITY_TYPE_MARKER) continue;

                uint32_t epid = UINT32_MAX, bidx = UINT32_MAX;
                if (!read_u32(&ent->attrs, SCRIPT_KEY_PARENT_ID, &epid)) continue;
                if (epid != root_id) continue;
                if (!read_u32(&ent->attrs, SCRIPT_KEY_BONE_INDEX, &bidx)) continue;
                if (bidx != b) continue;

                /* Map entity type to shape. */
                switch (ent->type) {
                    case EDIT_ENTITY_TYPE_COLLIDER_CAPSULE:
                        def->bones[b].shape_type = BONE_COLLIDER_CAPSULE; break;
                    case EDIT_ENTITY_TYPE_COLLIDER_BOX:
                        def->bones[b].shape_type = BONE_COLLIDER_BOX; break;
                    case EDIT_ENTITY_TYPE_COLLIDER_SPHERE:
                        def->bones[b].shape_type = BONE_COLLIDER_SPHERE; break;
                    case EDIT_ENTITY_TYPE_COLLIDER_HULL:
                        def->bones[b].shape_type = BONE_COLLIDER_CONVEX_HULL; break;
                    default: break;
                }

                /* Read shape params from attrs. */
                uint8_t at = 0, as = 0;
                const void *rd;
                rd = entity_attrs_get(&ent->attrs, SCRIPT_KEY_RADIUS, &at, &as);
                if (rd && at == SCRIPT_ATTR_F32) {
                    memcpy(&def->bones[b].params[0], rd, sizeof(float));
                }
                rd = entity_attrs_get(&ent->attrs, SCRIPT_KEY_HEIGHT, &at, &as);
                if (rd && at == SCRIPT_ATTR_F32) {
                    memcpy(&def->bones[b].params[1], rd, sizeof(float));
                }
                rd = entity_attrs_get(&ent->attrs, SCRIPT_KEY_MASS, &at, &as);
                if (rd && at == SCRIPT_ATTR_F32) {
                    memcpy(&def->bones[b].mass, rd, sizeof(float));
                }
                break; /* First collider per bone. */
            }

            /* Check for hull markers. */
            prefab_hull_result_t hull_result;
            if (prefab_hull_build_from_markers(entities, root_id, b,
                                                &hull_result)) {
                def->bones[b].shape_type = BONE_COLLIDER_CONVEX_HULL;
                def->bones[b].hull_offset = def->hull_vert_count;
                def->bones[b].hull_count = hull_result.hull.vertex_count;

                for (uint32_t v = 0; v < hull_result.hull.vertex_count; v++) {
                    if (def->hull_vert_count >= PREFAB_MAX_HULL_VERTS) break;
                    uint32_t off = def->hull_vert_count * 3;
                    def->hull_verts[off + 0] = hull_result.hull.vertices[v].x;
                    def->hull_verts[off + 1] = hull_result.hull.vertices[v].y;
                    def->hull_verts[off + 2] = hull_result.hull.vertices[v].z;
                    def->hull_vert_count++;
                }
            }
        }
    }

    return true;
}

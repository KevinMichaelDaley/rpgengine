/**
 * @file prefab_hull_build.c
 * @brief Build convex hulls from marker entities parented to bones.
 *
 * Non-static functions: prefab_hull_build_from_markers,
 *                       prefab_hull_count_markers (2/4).
 */

#include "ferrum/editor/scene/prefab/prefab_hull_build.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/physics/convex_hull.h"

#include <string.h>

/** Maximum markers per bone for hull building. */
#define MAX_HULL_MARKERS 256

/* ---- Static helpers ---- */

/**
 * @brief Read a u32 attribute from an entity's attrs block.
 */
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
 * @brief Check if an entity is a marker parented to the given root+bone.
 */
static bool is_bone_marker(const edit_entity_t *ent, uint32_t root_id,
                           uint32_t bone_index) {
    if (ent->type != EDIT_ENTITY_TYPE_MARKER) return false;

    uint32_t pid = UINT32_MAX, bidx = UINT32_MAX;
    if (!read_u32(&ent->attrs, SCRIPT_KEY_PARENT_ID, &pid)) return false;
    if (pid != root_id) return false;
    if (!read_u32(&ent->attrs, SCRIPT_KEY_BONE_INDEX, &bidx)) return false;
    return bidx == bone_index;
}

/* ---- Public API ---- */

bool prefab_hull_build_from_markers(const struct edit_entity_store *entities,
                                    uint32_t root_id,
                                    uint32_t bone_index,
                                    prefab_hull_result_t *out) {
    if (!entities || !out) {
        if (out) {
            memset(out, 0, sizeof(*out));
        }
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* Collect marker positions. */
    phys_vec3_t points[MAX_HULL_MARKERS];
    uint32_t count = 0;

    uint32_t cap = entities->capacity;
    for (uint32_t i = 0; i < cap && count < MAX_HULL_MARKERS; i++) {
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;
        if (!is_bone_marker(ent, root_id, bone_index)) continue;

        points[count].x = ent->pos[0];
        points[count].y = ent->pos[1];
        points[count].z = ent->pos[2];
        count++;
    }

    out->marker_count = count;

    if (count < 4) {
        out->valid = false;
        return false;
    }

    /* Build the convex hull. */
    int rc = phys_convex_hull_build(&out->hull, points, count);
    out->valid = (rc == 0);
    return out->valid;
}

uint32_t prefab_hull_count_markers(const struct edit_entity_store *entities,
                                   uint32_t root_id,
                                   uint32_t bone_index) {
    if (!entities) return 0;

    uint32_t count = 0;
    uint32_t cap = entities->capacity;
    for (uint32_t i = 0; i < cap; i++) {
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;
        if (is_bone_marker(ent, root_id, bone_index)) count++;
    }
    return count;
}

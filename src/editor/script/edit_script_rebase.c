/**
 * @file edit_script_rebase.c
 * @brief Rebase script entity updates onto the authoritative entity store.
 *
 * Iterates the packed update blob and applies each attribute write to the
 * corresponding entity. Well-known keys map to fixed entity fields;
 * user/dynamic keys go to entity_attrs_t.
 *
 * Non-static functions (1 of 4 max):
 *   1. script_rebase_apply
 *
 * Static helpers:
 *   - apply_attr_to_entity_  — dispatch a single attr write
 *   - apply_wellknown_vec3_  — apply a vec3 attr (pos/rot/scale)
 */

#include "ferrum/editor/edit_script_rebase.h"

#include <string.h>
#include "ferrum/math/quat.h"

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_script_env.h"
#include "ferrum/entity/entity_attrs.h"

/* ── Static helpers ────────────────────────────────────────────── */

/**
 * @brief Apply a vec3 attribute to a float[3] field.
 * @return true if applied, false on size mismatch.
 */
static bool apply_wellknown_vec3_(float *dst, const uint8_t *payload,
                                  uint8_t size) {
    if (size != 12) return false;
    memcpy(dst, payload, 12);
    return true;
}

/**
 * @brief Apply a single attribute write to an entity.
 *
 * Well-known keys are mapped to fixed fields. All other keys go
 * to the entity's dynamic attrs block.
 *
 * @param entity   Mutable entity pointer (must not be NULL).
 * @param aw       Attribute write header.
 * @param payload  Pointer to payload bytes (aw->size bytes).
 * @return true if the attribute was applied, false if skipped (mismatch).
 */
static bool apply_attr_to_entity_(edit_entity_t *entity,
                                  const script_attr_write_t *aw,
                                  const uint8_t *payload) {
    switch (aw->key) {
    case SCRIPT_KEY_POS:
        return apply_wellknown_vec3_(entity->pos, payload, aw->size);

    case SCRIPT_KEY_ROT:
        if (!apply_wellknown_vec3_(entity->rot, payload, aw->size))
            return false;
        /* Sync authoritative orientation from updated euler cache. */
        {
            static const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
            entity->orientation = quat_from_euler_yxz(
                entity->rot[0] * DEG_TO_RAD,
                entity->rot[1] * DEG_TO_RAD,
                entity->rot[2] * DEG_TO_RAD);
        }
        return true;

    case SCRIPT_KEY_SCALE:
        return apply_wellknown_vec3_(entity->scale, payload, aw->size);

    case SCRIPT_KEY_TYPE:
        if (aw->size != 4) return false;
        memcpy(&entity->type, payload, 4);
        return true;

    case SCRIPT_KEY_BODY_IDX:
        if (aw->size != 4) return false;
        memcpy(&entity->body_index, payload, 4);
        return true;

    case SCRIPT_KEY_NAME: {
        /* Copy name, ensuring null termination and no overflow. */
        uint32_t copy_len = aw->size;
        if (copy_len >= EDIT_ENTITY_NAME_MAX) {
            copy_len = EDIT_ENTITY_NAME_MAX - 1;
        }
        memcpy(entity->name, payload, copy_len);
        entity->name[copy_len] = '\0';
        return true;
    }

    default:
        /* Dynamic/user attr — write to entity_attrs_t. */
        return entity_attrs_set(&entity->attrs, aw->key,
                                aw->type, payload, aw->size);
    }
}

/* ── Public API ────────────────────────────────────────────────── */

script_rebase_result_t script_rebase_apply(struct edit_entity_store *store,
                                           const uint8_t *blob,
                                           uint32_t used_bytes) {
    script_rebase_result_t result = {0, 0};

    if (!store || !blob || used_bytes == 0) {
        return result;
    }

    uint32_t offset = 0;

    while (offset + sizeof(script_entity_update_t) <= used_bytes) {
        const script_entity_update_t *upd =
            (const script_entity_update_t *)(blob + offset);

        /* Validate total_size to prevent infinite loop. */
        if (upd->total_size < sizeof(script_entity_update_t) ||
            offset + upd->total_size > used_bytes) {
            break;
        }

        /* Look up entity. */
        edit_entity_t *entity =
            edit_entity_store_get_mut(store, upd->entity_id);

        if (!entity) {
            /* Entity missing, deleted, or out of range. */
            result.skipped++;
            offset += upd->total_size;
            continue;
        }

        /* Walk attribute writes within this update. */
        uint32_t attr_offset = offset + (uint32_t)sizeof(script_entity_update_t);

        for (uint16_t i = 0; i < upd->attr_count; i++) {
            /* Bounds check for attr header. */
            if (attr_offset + sizeof(script_attr_write_t) >
                offset + upd->total_size) {
                break;
            }

            const script_attr_write_t *aw =
                (const script_attr_write_t *)(blob + attr_offset);

            uint32_t payload_offset =
                attr_offset + (uint32_t)sizeof(script_attr_write_t);

            /* Bounds check for payload. */
            if (payload_offset + aw->size > offset + upd->total_size) {
                break;
            }

            const uint8_t *payload = blob + payload_offset;
            apply_attr_to_entity_(entity, aw, payload);

            attr_offset = payload_offset + aw->size;
        }

        result.applied++;
        offset += upd->total_size;
    }

    return result;
}

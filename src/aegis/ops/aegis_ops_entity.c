/**
 * @file aegis_ops_entity.c
 * @brief Entity query handlers: query_entity, get_attr.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 * 2 non-static functions.
 */

#include "ferrum/aegis/aegis_ops_entity.h"
#include "ferrum/editor/edit_script_env.h"
#include "ferrum/entity/entity_attrs.h"
#include <string.h>

/* ----------------------------------------------------------------------- */
/* query_entity: find entity in snapshot by ID                              */
/* ----------------------------------------------------------------------- */

bool aegis_op_query_entity(aegis_register_t *dst,
                           const aegis_register_t *id_reg,
                           const struct script_entity_view *view) {
    if (!view) return false;

    const script_entity_view_t *v = (const script_entity_view_t *)view;
    uint32_t target_id = id_reg->u32;

    memset(dst, 0, sizeof(*dst));

    /* Linear scan for matching entity_id. */
    for (uint32_t i = 0; i < v->count; i++) {
        if (v->entities[i].entity_id == target_id) {
            dst->i32 = (int32_t)i;
            return true;
        }
    }

    /* Not found. */
    dst->i32 = -1;
    return true;
}

/* ----------------------------------------------------------------------- */
/* get_attr: read attribute from entity snapshot                            */
/* ----------------------------------------------------------------------- */

bool aegis_op_get_attr(aegis_register_t *dst,
                       uint32_t handle,
                       uint16_t key,
                       const struct script_entity_view *view) {
    if (!view) return false;

    const script_entity_view_t *v = (const script_entity_view_t *)view;

    /* Bounds check. */
    if (handle >= v->count) return false;

    const script_entity_snapshot_t *snap = &v->entities[handle];

    memset(dst, 0, sizeof(*dst));

    /* Well-known keys: read directly from snapshot fields. */
    switch (key) {
    case SCRIPT_KEY_POS:
        memcpy(dst->vec3, snap->pos, 3 * sizeof(float));
        return true;

    case SCRIPT_KEY_ROT:
        memcpy(dst->vec3, snap->rot, 3 * sizeof(float));
        return true;

    case SCRIPT_KEY_SCALE:
        memcpy(dst->vec3, snap->scale, 3 * sizeof(float));
        return true;

    case SCRIPT_KEY_TYPE:
        dst->u32 = snap->type;
        return true;

    case SCRIPT_KEY_BODY_IDX:
        dst->u32 = snap->body_index;
        return true;

    default:
        break;
    }

    /* Dynamic key: delegate to entity_attrs_get. */
    uint8_t attr_type = 0;
    uint8_t attr_size = 0;
    const void *data = entity_attrs_get(&snap->attrs, key,
                                        &attr_type, &attr_size);
    if (!data) return false;

    /* Copy up to 16 bytes (register size). */
    uint8_t copy_size = attr_size > 16 ? 16 : attr_size;
    memcpy(dst->bytes, data, copy_size);
    return true;
}

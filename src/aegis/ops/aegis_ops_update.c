/**
 * @file aegis_ops_update.c
 * @brief Update builder handlers: build_update, target_entity, set_field, add_hint.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 * 4 non-static functions.
 */

#include "ferrum/aegis/aegis_ops_update.h"
#include "ferrum/entity/entity_attrs.h"
#include <string.h>

/* ----------------------------------------------------------------------- */
/* build_update: initialize empty staging update                            */
/* ----------------------------------------------------------------------- */

bool aegis_op_build_update(aegis_register_t *dst,
                           aegis_state_update_t *staging) {
    memset(staging, 0, sizeof(*staging));
    memset(dst, 0, sizeof(*dst));
    dst->u32 = 0; /* Builder handle (single builder). */
    return true;
}

/* ----------------------------------------------------------------------- */
/* target_entity: set target entity ID                                      */
/* ----------------------------------------------------------------------- */

bool aegis_op_target_entity(aegis_state_update_t *staging,
                            const aegis_register_t *entity_reg) {
    staging->target = entity_reg->u32;
    return true;
}

/* ----------------------------------------------------------------------- */
/* set_field: add attribute write to staging update                         */
/* ----------------------------------------------------------------------- */

bool aegis_op_set_field(aegis_state_update_t *staging,
                        uint16_t key,
                        const aegis_register_t *val_reg) {
    staging->key = key;

    /* Infer type and size from well-known keys. */
    switch (key) {
    case SCRIPT_KEY_POS:
    case SCRIPT_KEY_ROT:
    case SCRIPT_KEY_SCALE:
        staging->type = SCRIPT_ATTR_VEC3;
        staging->size = 12;
        memcpy(staging->value, val_reg->vec3, 12);
        return true;

    case SCRIPT_KEY_TYPE:
    case SCRIPT_KEY_BODY_IDX:
        staging->type = SCRIPT_ATTR_U32;
        staging->size = 4;
        memcpy(staging->value, &val_reg->u32, 4);
        return true;

    default:
        /* User-defined or unknown: copy full register as blob. */
        staging->type = SCRIPT_ATTR_BLOB;
        staging->size = 16;
        memcpy(staging->value, val_reg->bytes, 16);
        return true;
    }
}

/* ----------------------------------------------------------------------- */
/* add_hint: set validation hint flag                                       */
/* ----------------------------------------------------------------------- */

bool aegis_op_add_hint(aegis_state_update_t *staging,
                       uint32_t hint_type) {
    staging->hints |= hint_type;
    return true;
}

/**
 * @file aegis_ops_entity_iter.c
 * @brief Entity iteration handlers: entity_count, entity_at.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 * 2 non-static functions.
 */

#include "ferrum/aegis/aegis_ops_entity.h"
#include "ferrum/editor/edit_script_env.h"
#include <string.h>

/* ----------------------------------------------------------------------- */
/* entity_count: number of active entities in snapshot                      */
/* ----------------------------------------------------------------------- */

bool aegis_op_entity_count(aegis_register_t *dst,
                           const struct script_entity_view *view) {
    if (!view) return false;

    const script_entity_view_t *v = (const script_entity_view_t *)view;

    memset(dst, 0, sizeof(*dst));
    dst->u32 = v->count;
    return true;
}

/* ----------------------------------------------------------------------- */
/* entity_at: entity handle at snapshot index                               */
/* ----------------------------------------------------------------------- */

bool aegis_op_entity_at(aegis_register_t *dst,
                        const aegis_register_t *idx_reg,
                        const struct script_entity_view *view) {
    if (!view) return false;

    const script_entity_view_t *v = (const script_entity_view_t *)view;
    uint32_t index = idx_reg->u32;

    /* Bounds check. */
    if (index >= v->count) return false;

    memset(dst, 0, sizeof(*dst));
    dst->u32 = index;
    return true;
}

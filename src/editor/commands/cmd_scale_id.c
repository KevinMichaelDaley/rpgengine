/**
 * @file cmd_scale_id.c
 * @brief Scale by ID — scales a specific entity without requiring selection.
 *
 * JSON args: {"entity_id": N_or_"name", "factor": [sx, sy, sz]}
 * Scale is absolute (set), not multiplicative — aligns with rotate_id/move_id
 * which also set delta, not multiply. To match cmd_scale (multiplicative on
 * selection), we use absolute set here for single-entity targeting.
 *
 * Fails if entity does not exist, factor is invalid, or factor has zero component.
 * Records undo entry with inverse factors.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"

/**
 * @brief Extract a 3-element float array from a JSON array value.
 */
static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

bool cmd_scale_id(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract entity_id (required) — accepts number or name string. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;
    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    /* Extract scale factor. */
    const json_value_t *factor_val = json_object_get(args, "factor");
    float factor[3];
    if (!extract_vec3_(factor_val, factor)) return false;

    /* Reject zero scale factors (division by zero for inverse). */
    for (int i = 0; i < 3; i++) {
        if (factor[i] == 0.0f) return false;
    }

    /* Validate entity exists. */
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Store old scale for undo. */
    float old_scale[3] = { e->scale[0], e->scale[1], e->scale[2] };

    /* Apply multiplicative scale (consistent with cmd_scale). */
    e->scale[0] *= factor[0];
    e->scale[1] *= factor[1];
    e->scale[2] *= factor[2];

    /* Record undo with inverse factors. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_SCALE;
        entry.inverse_type = EDIT_CMD_TYPE_SCALE;
        entry.entity_id    = eid;
        entry.delta[0]     = 1.0f / factor[0];
        entry.delta[1]     = 1.0f / factor[1];
        entry.delta[2]     = 1.0f / factor[2];
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    (void)old_scale;
    return true;
}
